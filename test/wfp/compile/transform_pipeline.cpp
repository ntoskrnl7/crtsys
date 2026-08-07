#include <ntl/net/http/inspection_conditions>
#include <ntl/net/http/inspection_policy>
#include <ntl/net/offload/inspect_adapter>
#include <ntl/net/transform_pipeline>

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <span>
#include <string_view>
#include <utility>

namespace {

struct byte_policy {
  std::string_view blocked_text;
};

ntl::status inspect_content(
    void *, const ntl::net::transform_context &,
    ntl::net::inspection::content_view input) noexcept {
  return input ? ntl::status::ok()
               : ntl::status{STATUS_INVALID_PARAMETER};
}

ntl::result<std::size_t> copy_content(
    void *, const ntl::net::transform_context &,
    ntl::net::inspection::content_view input,
    std::span<std::byte> output) noexcept {
  if (output.size() < input.size())
    return ntl::unexpected(STATUS_BUFFER_TOO_SMALL);
  const ntl::status copied =
      input.bytes().copy_to(output.first(input.size()));
  if (!copied.is_ok())
    return ntl::unexpected(copied);
  return ntl::ok(input.size());
}

ntl::result<ntl::net::inspection::verdict> decide_content(
    void *opaque, const ntl::net::transform_context &,
    ntl::net::inspection::content_view input) noexcept {
  const auto &policy = *static_cast<const byte_policy *>(opaque);
  const auto blocked = input.contains(policy.blocked_text);
  if (!blocked)
    return ntl::unexpected(blocked.status());
  return ntl::ok(*blocked ? ntl::net::inspection::verdict::block
                          : ntl::net::inspection::verdict::permit);
}

ntl::result<ntl::net::transform_result> apply_byte_policy(
    const ntl::net::transform_context &context,
    ntl::net::inspection::content_view input,
    std::span<std::byte> caller_owned_output,
    byte_policy &policy) noexcept {
  ntl::net::borrowed_transform_pipeline pipeline;
  pipeline
      .inspect({&inspect_content, &policy})
      .transform({&copy_content, &policy,
                  ntl::net::execution_path::direct})
      .decide({&decide_content, &policy});
  return pipeline.run(context, input, caller_owned_output);
}

ntl::net::inspection::verdict decide_with_service(
    std::shared_ptr<ntl::net::offload::backend> service,
    const ntl::net::transform_context &context,
    ntl::net::inspection::content_view input) noexcept {
  ntl::net::offload::inspect_adapter service_policy(
      std::move(service), 2'000);
  ntl::net::borrowed_transform_pipeline pipeline;
  pipeline.decide(service_policy.stage());

  const auto decision = pipeline.run(context, input);
  if (!decision)
    return service_policy.failure_verdict();
  return decision->verdict;
}

void configure_http_policy(ntl::net::http::inspection_policy &policy) {
  namespace condition = ntl::net::http::condition;
  policy.requests()
      .at_headers()
      .when(condition::method_is("POST"))
      .when(condition::path_is("/inspect"))
      .decide([](const ntl::net::http::inspection_context_view &) {
        return ntl::net::inspection::verdict::block;
      });
}

bool run_byte_policy_contract(std::string_view text,
                              ntl::net::inspection::verdict expected) {
  byte_policy policy{"blocked"};
  const auto bytes = std::as_bytes(
      std::span<const char>(text.data(), text.size()));
  std::array<std::byte, 32> output{};
  const ntl::net::transform_context context{
      .network = {
          .kind = ntl::net::inspection::content_kind::tcp_message,
          .flow_direction = ntl::net::inspection::direction::outbound,
          .flow_id = 42,
          .source_port = 50'000,
          .destination_port = 443,
      },
      .protocol_features =
          ntl::net::feature_set(ntl::net::network_feature::content_transform),
  };

  const auto result = apply_byte_policy(
      context, ntl::net::inspection::content_view(bytes), output, policy);
  return result && result->verdict == expected && result->transformed &&
         !result->forward_original && result->output_size == bytes.size() &&
         result->path == ntl::net::execution_path::direct &&
         std::equal(bytes.begin(), bytes.end(), output.begin());
}

} // namespace

int main() noexcept {
  const auto service_decider = &decide_with_service;
  (void)service_decider;

  ntl::net::http::inspection_policy http_policy;
  configure_http_policy(http_policy);

  if (!run_byte_policy_contract(
          "contains blocked text",
          ntl::net::inspection::verdict::block))
    return 1;
  if (!run_byte_policy_contract(
          "allowed text", ntl::net::inspection::verdict::permit))
    return 2;
  return 0;
}
