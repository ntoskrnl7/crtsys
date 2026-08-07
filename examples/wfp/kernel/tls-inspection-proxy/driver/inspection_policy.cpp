#include "inspection_policy.hpp"

#include <algorithm>

#include "tls_inspection_policy.hpp"

namespace crtsys::wfp_kernel_tls {

bool supports_application_protocol(std::string_view value) noexcept {
  return std::find(inbound_application_protocols.begin(),
                   inbound_application_protocols.end(), value) !=
         inbound_application_protocols.end();
}

ntl::net::http::inspection_policy make_inspection_policy() {
  return crtsys::examples::wfp::tls_inspection::make_inspection_policy();
}

ntl::net::inspection::verdict evaluate_request(
    const ntl::net::http::inspection_policy &policy,
    ntl::net::http::protocol protocol, std::uint64_t stream_id,
    std::uint64_t exchange_id,
    const ntl::net::http::inspection_session_metadata &session,
    const ntl::net::http::request_message &request) noexcept {
  constexpr std::array stages{
      ntl::net::http::inspection_stage::headers,
      ntl::net::http::inspection_stage::body_chunk,
      ntl::net::http::inspection_stage::message_complete};
  for (const auto stage : stages) {
    if (stage == ntl::net::http::inspection_stage::body_chunk &&
        request.body.empty())
      continue;
    const auto chunk =
        stage == ntl::net::http::inspection_stage::body_chunk
            ? std::span<const std::byte>(request.body)
            : std::span<const std::byte>{};
    const auto context = ntl::net::http::inspection_context_view::for_request(
        protocol, stream_id, exchange_id, stage, session, request, chunk);
    const auto verdict = policy.decisions_ref().evaluate(context);
    if (verdict != ntl::net::inspection::verdict::permit)
      return verdict;
  }
  return ntl::net::inspection::verdict::permit;
}

} // namespace crtsys::wfp_kernel_tls
