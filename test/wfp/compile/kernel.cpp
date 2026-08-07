#include <ntddk.h>

#include <array>
#include <chrono>
#include <coroutine>
#include <cstddef>
#include <memory>
#include <span>
#include <type_traits>
#include <utility>

#include <ntl/driver>
#include <ntl/device_endpoint>
#include <ntl/net/http/inspection_policy>
#include <ntl/net/kernel/all>
#include <ntl/wfp/all>

namespace {

static_assert(ntl::wfp::injection_limits{}.valid());
static_assert(!ntl::wfp::injection_limits{0}.valid());
static_assert(std::is_move_constructible_v<
              ntl::wfp::transparent_udp_proxy_service>);
static_assert(!std::is_copy_constructible_v<
              ntl::wfp::transparent_udp_proxy_service>);
static_assert(ntl::net::kernel::executor_limits{}.valid());
static_assert(!ntl::net::kernel::executor_limits{0}.valid());

static_assert(ntl::net::native_execution_domain ==
              ntl::net::execution_domain::kernel);
static_assert(
    std::is_copy_constructible_v<ntl::device_endpoint<void>>);
static_assert(
    std::is_copy_assignable_v<ntl::device_endpoint<void>>);
static_assert(noexcept(
    std::declval<const ntl::device_endpoint<void> &>().close()));
static_assert(std::is_same_v<
              decltype(std::declval<ntl::net::http::inspection_policy &>()
                           .decisions_ref()),
              ntl::net::http::decision_policy &>);
static_assert(!std::is_constructible_v<
              ntl::net::http::inspection_context_view,
              ntl::net::http::protocol, std::uint64_t, std::uint64_t,
              ntl::net::http::message_direction,
              ntl::net::http::inspection_stage,
              const ntl::net::http::inspection_session_metadata &,
              const ntl::net::http::request_message &,
              const ntl::net::http::response_message *,
              std::span<const std::byte>>);

class compile_tls_observer final
    : public ntl::net::tls_client_hello_observer {
public:
  ntl::status on_server_name(std::string_view name) noexcept override {
    server_name_size = name.size();
    return ntl::status::ok();
  }
  ntl::status
  on_application_protocol(std::string_view protocol) noexcept override {
    alpn_size = protocol.size();
    return ntl::status::ok();
  }

  std::size_t server_name_size = 0;
  std::size_t alpn_size = 0;
};

class compile_qpack_sink final : public ntl::net::http3::qpack_field_sink {
public:
  ntl::status
  on_field(ntl::net::http3::qpack_field_view field) noexcept override {
    ++fields;
    valid = field.name == ":path" && field.value == "/index.html";
    return ntl::status::ok();
  }

  std::size_t fields = 0;
  bool valid = false;
};

ntl::status compile_inspect_stage(
    void *, const ntl::net::transform_context &,
    ntl::net::inspection::content_view content) noexcept {
  return content.empty() ? ntl::status{STATUS_DATA_ERROR}
                         : ntl::status::ok();
}

ntl::result<std::size_t> compile_rewrite_stage(
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

ntl::result<ntl::net::inspection::verdict> compile_verdict_stage(
    void *, const ntl::net::transform_context &,
    ntl::net::inspection::content_view) noexcept {
  return ntl::ok(ntl::net::inspection::verdict::permit);
}

ntl::status compile_offload_callback(
    void *, const ntl::net::offload::request_header &request,
    ntl::net::scatter_view input, std::span<std::byte> output,
    ntl::net::offload::response_header &response) noexcept {
  if (!request.protocol_features.contains(
          ntl::net::network_feature::grpc) ||
      request.content_kind !=
          ntl::net::inspection::content_kind::tcp_message ||
      request.direction != ntl::net::inspection::direction::inbound ||
      request.source_port != 1234 || request.destination_port != 443)
    return STATUS_BUFFER_TOO_SMALL;
  if (request.kind == ntl::net::offload::operation::inspect_content) {
    response = {
        .kind = request.kind,
        .request_id = request.request_id,
        .completion_status = STATUS_SUCCESS,
        .verdict = ntl::net::inspection::verdict::block,
    };
    return ntl::status::ok();
  }
  if (output.size() < input.size())
    return STATUS_BUFFER_TOO_SMALL;
  const ntl::status copied = input.copy_to(output.first(input.size()));
  if (!copied.is_ok())
    return copied;
  response = {
      .kind = request.kind,
      .request_id = request.request_id,
      .completion_status = STATUS_SUCCESS,
      .output_size = static_cast<std::uint32_t>(input.size()),
  };
  return ntl::status::ok();
}

[[maybe_unused]] ntl::status compile_kernel_protocol_core() noexcept {
  ntl::net::http::inspection_policy semantic_policy;
  semantic_policy.requests()
      .at_headers()
      .when(ntl::net::http::condition::method_is("POST"))
      .when(ntl::net::http::condition::path_is("/kernel-policy"))
      .when(ntl::net::http::condition::header_is(
          "x-ntl-policy", "inspect"))
      .when(ntl::net::http::condition::header_name_starts_with("CUSTOM-"))
      .when(ntl::net::http::condition::any_header(
          [](const ntl::net::http::header_field &header) noexcept {
            return header.name.starts_with("custom-") &&
                   header.value == "kernel";
          }))
      .when(ntl::net::http::condition::process_is(4))
      .when([](const ntl::net::http::inspection_context_view &context) noexcept {
        return context.authority() == "kernel.example.test" &&
               context.connection().process_id == 4;
      })
      .decide([](const ntl::net::http::inspection_context_view &) noexcept {
        return ntl::net::inspection::verdict::permit;
      });
  ntl::net::http::request_message semantic_request;
  semantic_request.method = "POST";
  semantic_request.authority = "kernel.example.test";
  semantic_request.path = "/kernel-policy";
  semantic_request.headers.set("x-ntl-policy", "inspect");
  semantic_request.headers.set("custom-mode", "kernel");
  ntl::net::http::inspection_session_metadata semantic_session;
  semantic_session.connection.process_id = 4;
  const auto semantic_context =
      ntl::net::http::inspection_context_view::for_request(
          ntl::net::http::protocol::http1, 0, 1,
          ntl::net::http::inspection_stage::headers, semantic_session,
          semantic_request);
  if (semantic_policy.decisions_ref().evaluate(semantic_context) !=
      ntl::net::inspection::verdict::permit)
    return STATUS_DATA_ERROR;

  constexpr std::array<std::byte, 8> grpc_payload{
      std::byte{'k'}, std::byte{'e'}, std::byte{'r'}, std::byte{'n'},
      std::byte{'e'}, std::byte{'l'}, std::byte{'!'}, std::byte{'!'}};
  std::array<std::byte, 32> wire{};
  auto grpc_size = ntl::net::grpc::encode_message_to(
      wire, grpc_payload, false, grpc_payload.size());
  if (!grpc_size)
    return grpc_size.status();
  const auto grpc = ntl::net::grpc::inspect_header(
      ntl::net::scatter_view::from_contiguous(
          std::span<const std::byte>(wire).first(*grpc_size)),
      grpc_payload.size());
  if (!grpc || grpc->payload_size != grpc_payload.size())
    return STATUS_DATA_ERROR;

  auto varint = ntl::net::http3::write_quic_varint(wire, 15293);
  if (!varint)
    return varint.status();
  const auto decoded_varint = ntl::net::http3::read_quic_varint(
      ntl::net::scatter_view::from_contiguous(
          std::span<const std::byte>(wire).first(*varint)));
  if (!decoded_varint || decoded_varint->value != 15293)
    return STATUS_DATA_ERROR;

  constexpr std::array<std::byte, 14> qpack_wire{
      std::byte{0x00}, std::byte{0x00}, std::byte{0x51}, std::byte{0x0b},
      std::byte{'/'}, std::byte{'i'}, std::byte{'n'}, std::byte{'d'},
      std::byte{'e'}, std::byte{'x'}, std::byte{'.'}, std::byte{'h'},
      std::byte{'t'}, std::byte{'m'}};
  // Add the final 'l' through a fragmented scatter view.
  constexpr std::array<std::byte, 1> qpack_tail{std::byte{'l'}};
  const std::array<std::span<const std::byte>, 2> qpack_segments{
      std::span<const std::byte>(qpack_wire),
      std::span<const std::byte>(qpack_tail)};
  std::array<std::byte, 64> qpack_scratch{};
  compile_qpack_sink qpack_sink;
  const auto qpack = ntl::net::http3::decode_static_qpack(
      ntl::net::scatter_view::from_segments(qpack_segments), qpack_scratch,
      qpack_sink, qpack_scratch.size());
  if (!qpack || qpack->field_count != 1 || !qpack_sink.valid)
    return STATUS_DATA_ERROR;

  constexpr std::array<std::byte, 3> datagram_payload{
      std::byte{1}, std::byte{2}, std::byte{3}};
  auto datagram = ntl::net::http::encode_http3_datagram_to(
      wire, 4, datagram_payload,
      {.maximum_payload_size = datagram_payload.size()});
  if (!datagram)
    return datagram.status();
  const auto datagram_view = ntl::net::http::http3_datagram_view::parse(
      ntl::net::scatter_view::from_contiguous(
          std::span<const std::byte>(wire).first(*datagram)),
      {.maximum_payload_size = datagram_payload.size()});
  if (!datagram_view || datagram_view->request_stream_id() != 4 ||
      datagram_view->payload().size() != datagram_payload.size())
    return STATUS_DATA_ERROR;

  constexpr ntl::net::runtime_descriptor offload_service{
      .domain = ntl::net::execution_domain::user,
      .path = ntl::net::execution_path::offloaded,
      .features = ntl::net::feature_set(
          ntl::net::network_feature::content_transform |
          ntl::net::network_feature::content_inspection),
  };
  const ntl::net::offload::request_header request{
      .kind = ntl::net::offload::operation::transform_content,
      .request_id = 1,
      .flow_id = 7,
      .input_size = static_cast<std::uint32_t>(grpc_payload.size()),
      .output_capacity = 32,
      .timeout_milliseconds = 1000,
      .protocol_features =
          ntl::net::feature_set(ntl::net::network_feature::grpc),
      .content_kind = ntl::net::inspection::content_kind::tcp_message,
      .direction = ntl::net::inspection::direction::inbound,
      .source_port = 1234,
      .destination_port = 443,
  };
  const ntl::status valid =
      ntl::net::offload::validate(request, offload_service);
  if (!valid.is_ok())
    return valid;

  auto offload_backend = std::make_shared<
      ntl::net::offload::borrowed_callback_backend>(
          offload_service,
          ntl::net::offload::callback{&compile_offload_callback, nullptr});
  ntl::net::offload::response_header offload_response{};
  std::array<std::byte, 32> offload_output{};
  const ntl::status offloaded = offload_backend->execute(
      request,
      ntl::net::scatter_view::from_contiguous(
          std::span<const std::byte>(grpc_payload).first(request.input_size)),
      offload_output, offload_response);
  if (!offloaded.is_ok() || offload_response.output_size != request.input_size)
    return STATUS_DATA_ERROR;

  ntl::net::offload::transform_adapter offload_transform(offload_backend);
  ntl::net::borrowed_transform_pipeline offload_pipeline;
  offload_pipeline.transform(offload_transform.stage());
  const auto offload_pipeline_result = offload_pipeline.run(
      {.network = {.kind = ntl::net::inspection::content_kind::tcp_message,
                   .flow_direction =
                       ntl::net::inspection::direction::inbound,
                   .flow_id = 11,
                   .source_port = 1234,
                   .destination_port = 443},
       .protocol_features =
           ntl::net::feature_set(ntl::net::network_feature::grpc)},
      ntl::net::inspection::content_view(grpc_payload), offload_output);
  if (!offload_pipeline_result ||
      offload_pipeline_result->path != ntl::net::execution_path::offloaded ||
      offload_pipeline_result->output_size != grpc_payload.size())
    return STATUS_DATA_ERROR;

  ntl::net::offload::inspect_adapter offload_inspect(offload_backend);
  ntl::net::borrowed_transform_pipeline inspection_pipeline;
  inspection_pipeline.decide(offload_inspect.stage());
  const auto inspection_result = inspection_pipeline.run(
      {.network = {.kind = ntl::net::inspection::content_kind::tcp_message,
                   .flow_direction =
                       ntl::net::inspection::direction::inbound,
                   .flow_id = 12,
                   .source_port = 1234,
                   .destination_port = 443},
       .protocol_features =
           ntl::net::feature_set(ntl::net::network_feature::grpc)},
      ntl::net::inspection::content_view(grpc_payload));
  if (!inspection_result ||
      inspection_result->path != ntl::net::execution_path::offloaded ||
      inspection_result->verdict != ntl::net::inspection::verdict::block)
    return STATUS_DATA_ERROR;

  ntl::net::offload::borrowed_callback_async_backend async_offload(
      offload_service, nullptr, {});
  if (async_offload.runtime().path !=
      ntl::net::execution_path::offloaded)
    return STATUS_DATA_ERROR;

  ntl::net::quic::borrowed_callback_transport quic_provider(
      nullptr, {}, {.available = true},
      {.domain = ntl::net::execution_domain::kernel,
       .path = ntl::net::execution_path::direct,
       .features = ntl::net::feature_set(
           ntl::net::network_feature::quic_transport)});
  if (quic_provider.runtime().domain !=
          ntl::net::execution_domain::kernel ||
      !quic_provider.capabilities().available)
    return STATUS_DATA_ERROR;

  ntl::net::io::borrowed_callback_transport byte_provider(
      nullptr, {}, {.available = true, .full_duplex = true},
      {.domain = ntl::net::execution_domain::kernel,
       .path = ntl::net::execution_path::direct,
       .features = ntl::net::feature_set(
           ntl::net::network_feature::byte_transport)});
  if (!byte_provider.capabilities().full_duplex ||
      byte_provider.runtime().domain !=
          ntl::net::execution_domain::kernel)
    return STATUS_DATA_ERROR;

  ntl::net::borrowed_transform_pipeline pipeline;
  pipeline
      .inspect({&compile_inspect_stage, nullptr})
      .transform({&compile_rewrite_stage, nullptr,
                  ntl::net::execution_path::direct})
      .decide({&compile_verdict_stage, nullptr});
  std::array<std::byte, grpc_payload.size()> transformed{};
  const auto pipeline_result = pipeline.run(
      {.network = {.kind = ntl::net::inspection::content_kind::tcp_message},
       .protocol_features =
           ntl::net::feature_set(ntl::net::network_feature::grpc)},
      ntl::net::inspection::content_view(grpc_payload), transformed);
  return pipeline_result && pipeline_result->transformed &&
                 pipeline_result->output_size == transformed.size()
             ? ntl::status::ok()
             : ntl::status{STATUS_DATA_ERROR};
}

using connect_layer = ntl::wfp::layers::ale_auth_connect_v4;
using redirect_layer =
    ntl::wfp::layers::ale_connect_redirect_v4;
using redirect_layer_v6 =
    ntl::wfp::layers::ale_connect_redirect_v6;
using bind_redirect_layer =
    ntl::wfp::layers::ale_bind_redirect_v4;
using bind_redirect_layer_v6 =
    ntl::wfp::layers::ale_bind_redirect_v6;
using flow_layer = ntl::wfp::layers::ale_flow_established_v4;
using stream_layer = ntl::wfp::layers::stream_v4;
using datagram_layer = ntl::wfp::layers::datagram_data_v4;
using transport_layer = ntl::wfp::layers::outbound_transport_v4;

constexpr GUID connect_guid = {
    0xca664e90, 0x1f3d, 0x44bd, {0x88, 0xe7, 0x2c, 0xd8, 0xcf, 0xb0, 0xa6, 0x01}};
constexpr GUID flow_guid = {
    0xca664e91, 0x1f3d, 0x44bd, {0x88, 0xe7, 0x2c, 0xd8, 0xcf, 0xb0, 0xa6, 0x01}};
constexpr GUID stream_guid = {
    0xca664e92, 0x1f3d, 0x44bd, {0x88, 0xe7, 0x2c, 0xd8, 0xcf, 0xb0, 0xa6, 0x01}};
constexpr GUID datagram_guid = {
    0xca664e93, 0x1f3d, 0x44bd, {0x88, 0xe7, 0x2c, 0xd8, 0xcf, 0xb0, 0xa6, 0x01}};
constexpr GUID redirect_guid = {
    0xca664e94, 0x1f3d, 0x44bd, {0x88, 0xe7, 0x2c, 0xd8, 0xcf, 0xb0, 0xa6, 0x01}};
constexpr GUID bind_redirect_guid = {
    0xca664e95, 0x1f3d, 0x44bd, {0x88, 0xe7, 0x2c, 0xd8, 0xcf, 0xb0, 0xa6, 0x01}};

constexpr ntl::wfp::terminating_callout_key<connect_layer> connect_key(connect_guid);
constexpr ntl::wfp::inspection_callout_key<flow_layer> flow_key(flow_guid);
constexpr ntl::wfp::stream_callout_key<stream_layer> stream_key(stream_guid);
constexpr ntl::wfp::terminating_callout_key<datagram_layer> datagram_key(datagram_guid);
constexpr ntl::wfp::inspection_callout_key<datagram_layer>
    datagram_inspection_key(datagram_guid);
constexpr ntl::wfp::terminating_callout_key<redirect_layer> redirect_key(redirect_guid);
constexpr ntl::wfp::terminating_callout_key<bind_redirect_layer>
    bind_redirect_key(bind_redirect_guid);

template <class Key, class Callback>
concept accepts_inspection_registration =
    requires(ntl::wfp::callout_driver<> &callouts, Key key,
             Callback callback) {
      callouts.add_inspection(key, callback);
    };

template <class Key, class Callback>
concept accepts_terminating_registration =
    requires(ntl::wfp::callout_driver<> &callouts, Key key,
             Callback callback) {
      callouts.add_terminating(key, callback);
    };

template <class Key, class Callback>
concept accepts_arbitrating_registration =
    requires(ntl::wfp::callout_driver<> &callouts, Key key,
             Callback callback) {
      callouts.add_arbitrating(key, callback);
    };

struct stream_context {
  std::uint64_t inspected = 0;
  ~stream_context() noexcept = default;
};

struct proxy_context {
  std::uint64_t packets = 0;
  ~proxy_context() noexcept = default;
};

template <class Key, class Callback>
concept accepts_flow_context_inspection_registration =
    requires(ntl::wfp::callout_driver<> &callouts, Key key,
             std::shared_ptr<int> state, Callback callback) {
      callouts.add_flow_context_inspection<proxy_context>(key, state,
                                                          callback);
    };

constexpr auto decide =
    +[](const ntl::wfp::classify_event<connect_layer> &event) noexcept {
      return event.value(connect_layer::field::protocol)
                     .uint8()
                     .has_value()
                  ? ntl::wfp::terminating_decision::permit
                  : ntl::wfp::terminating_decision::block;
    };

constexpr auto observe_flow =
    +[](const ntl::wfp::classify_event<flow_layer> &event) noexcept {
      (void)event;
    };

constexpr auto inspect_stream =
    +[](const ntl::wfp::stream_event<stream_layer, stream_context> &event)
         noexcept {
           auto *context = event.context();
           if (!context)
             return ntl::wfp::stream_result::drop_connection();
           context->inspected += event.data().size();
           return ntl::wfp::stream_result::permit(event.data().size());
         };

constexpr auto proxy_datagram =
    +[](const ntl::wfp::classify_event<datagram_layer> &event,
        proxy_context *context) noexcept {
      if (!context || !event.packet())
        return ntl::wfp::terminating_decision::permit;
      ++context->packets;
      return ntl::wfp::terminating_decision::block_and_absorb;
    };

constexpr auto observe_datagram_context =
    +[](int &, const ntl::wfp::classify_event<datagram_layer> &,
        proxy_context *) noexcept {};

constexpr auto invalid_inspection_result =
    +[](int &, const ntl::wfp::classify_event<datagram_layer> &,
        proxy_context *) noexcept {
      return ntl::wfp::arbitration_decision::continue_classification;
    };

constexpr auto arbitrate_flow =
    +[](const ntl::wfp::classify_event<flow_layer> &) noexcept {
      return ntl::wfp::arbitration_decision::continue_classification;
    };

static_assert(accepts_terminating_registration<
              decltype(connect_key), decltype(decide)>);
static_assert(!accepts_inspection_registration<
              decltype(connect_key), decltype(observe_flow)>);
static_assert(accepts_inspection_registration<
              decltype(flow_key), decltype(observe_flow)>);
static_assert(!accepts_terminating_registration<
              decltype(flow_key), decltype(decide)>);
static_assert(!accepts_terminating_registration<
              decltype(connect_key), decltype(observe_flow)>);
static_assert(!accepts_arbitrating_registration<
              decltype(flow_key), decltype(arbitrate_flow)>);
static_assert(accepts_arbitrating_registration<
              ntl::wfp::arbitrating_callout_key<flow_layer>,
              decltype(arbitrate_flow)>);
static_assert(!accepts_inspection_registration<
              ntl::wfp::arbitrating_callout_key<flow_layer>,
              decltype(observe_flow)>);
static_assert(!accepts_inspection_registration<
              decltype(flow_key), decltype(arbitrate_flow)>);
static_assert(accepts_flow_context_inspection_registration<
              decltype(datagram_inspection_key),
              decltype(observe_datagram_context)>);
static_assert(!accepts_flow_context_inspection_registration<
              decltype(datagram_inspection_key),
              decltype(invalid_inspection_result)>);

struct owning_policy_state {
  std::uint64_t callbacks = 0;
};

[[maybe_unused]] ntl::status compile_owning_callout_callbacks(
    ntl::wfp::callout_driver<> &callouts,
    const std::shared_ptr<owning_policy_state> &state) noexcept {
  ntl::status result = callouts.add_terminating(
      connect_key, state,
      [](owning_policy_state &policy,
         const ntl::wfp::classify_event<connect_layer> &) noexcept {
        ++policy.callbacks;
        return ntl::wfp::terminating_decision::permit;
      });
  if (!result.is_ok())
    return result;

  auto stream = callouts.add_stream<stream_context>(
      stream_key, state,
      [](owning_policy_state &policy,
         const ntl::wfp::stream_event<stream_layer, stream_context> &event)
          noexcept {
        ++policy.callbacks;
        return ntl::wfp::stream_result::permit(event.data().size());
      });
  if (!stream)
    return stream.status();

  auto datagram = callouts.add_flow_context_terminating<proxy_context>(
      datagram_key, state,
      [](owning_policy_state &policy,
         const ntl::wfp::classify_event<datagram_layer> &,
         proxy_context *) noexcept {
        ++policy.callbacks;
        return ntl::wfp::terminating_decision::permit;
      });
  return datagram ? ntl::status::ok() : datagram.status();
}

void compile_trace_sink(
    void *context,
    const ntl::wfp::operational_trace_record &record) noexcept {
  if (context)
    *static_cast<ntl::wfp::operational_trace_record *>(context) = record;
}

[[maybe_unused]] bool compile_operational_telemetry() noexcept {
  ntl::wfp::operational_trace_record last{};
  ntl::wfp::operational_telemetry telemetry(
      {&compile_trace_sink, &last});
  telemetry.record_classify(connect_layer::runtime_id);
  telemetry.record_permit(connect_layer::runtime_id);
  telemetry.record_block(connect_layer::runtime_id);
  telemetry.record_absorb(connect_layer::runtime_id);
  telemetry.record_queue_saturated(datagram_layer::runtime_id);
  telemetry.record_verdict_timeout(stream_layer::runtime_id);
  telemetry.record_user_verdict(stream_layer::runtime_id, 25);
  telemetry.record_clone_failure(
      datagram_layer::runtime_id, STATUS_INSUFFICIENT_RESOURCES);
  telemetry.record_injection_failure(
      datagram_layer::runtime_id, STATUS_CANCELLED);
  telemetry.record_cancellation(stream_layer::runtime_id);
  telemetry.record_unload_race(stream_layer::runtime_id);
  telemetry.record_bfe_state(FWPM_SERVICE_RUNNING);
  const auto snapshot = telemetry.snapshot();
  return snapshot.classify == 1 && snapshot.permitted == 1 &&
         snapshot.blocked == 1 && snapshot.absorbed == 1 &&
         snapshot.queue_saturated == 1 &&
         snapshot.verdict_timeouts == 1 &&
         snapshot.user_verdicts == 1 &&
         snapshot.user_verdict_latency_100ns == 25 &&
         snapshot.maximum_user_verdict_latency_100ns == 25 &&
         snapshot.clone_failures == 1 &&
         snapshot.injection_failures == 1 &&
         snapshot.cancellations == 1 &&
         snapshot.unload_races == 1 &&
         snapshot.bfe_state_changes == 1 &&
         last.event == ntl::wfp::operational_event::bfe_state_change;
}

static_assert(!std::is_convertible_v<
              ntl::wfp::terminating_callout_key<connect_layer>, GUID>);
static_assert(!std::is_constructible_v<
              ntl::wfp::terminating_callout_key<connect_layer>,
              ntl::wfp::inspection_callout_key<flow_layer>>);
static_assert(!std::is_copy_constructible_v<
              ntl::wfp::classify_event<connect_layer>>);
static_assert(!std::is_copy_constructible_v<
              ntl::wfp::stream_event<stream_layer, stream_context>>);
static_assert(!std::is_copy_constructible_v<ntl::wfp::cloned_packet>);
static_assert(!std::is_copy_constructible_v<ntl::wfp::network_injector>);
static_assert(!std::is_copy_constructible_v<ntl::wfp::transport_injector>);
static_assert(
    !std::is_copy_constructible_v<ntl::wfp::transport_send_request>);
static_assert(
    std::is_nothrow_move_constructible_v<ntl::wfp::transport_send_request>);
static_assert(
    std::is_nothrow_move_assignable_v<ntl::wfp::transport_send_request>);
static_assert(!std::is_copy_constructible_v<ntl::wfp::stream_injector>);
static_assert(!std::is_copy_constructible_v<ntl::wfp::cloned_stream_data>);
static_assert(!std::is_copy_constructible_v<ntl::wfp::injected_stream_data>);
static_assert(!std::is_copy_constructible_v<ntl::net::owned_bytes>);
struct compile_network_workspace {
  std::array<std::byte, 4096> scratch{};
};
using compile_network_workspace_pool =
    ntl::net::kernel::workspace_pool<compile_network_workspace,
                                     ntl::pool_tag("cNwN")>;
static_assert(!std::is_copy_constructible_v<compile_network_workspace_pool>);
static_assert(!std::is_copy_constructible_v<
              compile_network_workspace_pool::lease>);
static_assert(std::is_move_constructible_v<
              compile_network_workspace_pool::lease>);
static_assert(!std::is_copy_constructible_v<
              ntl::net::async_byte_stream>);
static_assert(std::is_trivially_copyable_v<ntl::net::scatter_view>);
static_assert(ntl::wfp::detail::decision_layer<datagram_layer>);
static_assert(ntl::wfp::detail::decision_layer<transport_layer>);
static_assert(ntl::wfp::detail::stream_layer<stream_layer>);
static_assert(ntl::wfp::detail::connect_redirect_layer<redirect_layer>);
static_assert(
    ntl::wfp::detail::connect_redirect_layer<redirect_layer_v6>);
static_assert(
    ntl::wfp::detail::bind_redirect_layer<bind_redirect_layer>);
static_assert(
    ntl::wfp::detail::bind_redirect_layer<bind_redirect_layer_v6>);
static_assert(ntl::wfp::detail::packet_decision_layer<
              ntl::wfp::layers::inbound_mac_frame_ethernet>);
static_assert(ntl::wfp::detail::packet_decision_layer<
              ntl::wfp::layers::egress_vswitch_ethernet>);
static_assert(ntl::wfp::detail::decision_layer<
              ntl::wfp::layers::ale_endpoint_closure_v6>);
static_assert(ntl::wfp::detail::decision_layer<
              ntl::wfp::layers::name_resolution_cache_v4>);
static_assert(ntl::wfp::detail::management_only_layer<
              ntl::wfp::layers::ipsec_v6>);
static_assert(!ntl::wfp::detail::callout_layer<
              ntl::wfp::layers::ipsec_v6>);
static_assert(!std::is_copy_constructible_v<
              ntl::wfp::connect_redirector>);

template <class Packet>
concept accepted_by_network_send =
    requires(ntl::wfp::network_injector &injector, Packet packet) {
      injector.inject_send(std::move(packet), COMPARTMENT_ID{});
    };
template <class Packet>
concept accepted_by_network_receive =
    requires(ntl::wfp::network_injector &injector, Packet packet) {
      injector.inject_receive(std::move(packet), COMPARTMENT_ID{}, 0, 0);
    };
template <class Packet>
concept accepted_by_transport_send =
    requires(ntl::wfp::transport_injector &injector, Packet packet) {
      injector.inject_send(std::move(packet));
    };
template <class Packet>
concept accepted_by_transport_receive =
    requires(ntl::wfp::transport_injector &injector, Packet packet) {
      injector.inject_receive(std::move(packet), AF_INET, COMPARTMENT_ID{},
                              0, 0);
    };

static_assert(accepted_by_network_send<ntl::wfp::network_send_packet>);
static_assert(!accepted_by_network_send<ntl::wfp::network_receive_packet>);
static_assert(!accepted_by_network_send<ntl::wfp::cloned_packet>);
static_assert(accepted_by_network_receive<ntl::wfp::network_receive_packet>);
static_assert(!accepted_by_network_receive<ntl::wfp::network_send_packet>);
static_assert(accepted_by_transport_send<ntl::wfp::transport_send_packet>);
static_assert(!accepted_by_transport_send<ntl::wfp::transport_receive_packet>);
static_assert(
    accepted_by_transport_receive<ntl::wfp::transport_receive_packet>);
static_assert(
    !accepted_by_transport_receive<ntl::wfp::transport_send_packet>);

[[maybe_unused]] ntl::status compile_kernel_workspace(
    compile_network_workspace_pool &workspaces) noexcept {
  auto acquired = workspaces.try_acquire();
  if (!acquired)
    return acquired.status();
  auto workspace = std::move(*acquired);
  workspace->scratch.front() = std::byte{0x2a};
  return workspace->scratch.front() == std::byte{0x2a}
             ? ntl::status::ok()
             : ntl::status{STATUS_DATA_ERROR};
}

[[maybe_unused]] ntl::status compile_flow_association(
    const ntl::wfp::flow_target<stream_layer, stream_context> &target,
    std::uint64_t flow_handle) noexcept {
  auto context = std::unique_ptr<stream_context>(
      new (std::nothrow) stream_context{});
  if (!context)
    return STATUS_INSUFFICIENT_RESOURCES;
  return target.associate(flow_handle, std::move(context));
}

[[maybe_unused]] ntl::status compile_network_injection(
    ntl::wfp::network_injector &injector, ntl::wfp::cloned_packet packet,
    COMPARTMENT_ID compartment) noexcept {
  return injector.inject_send(
      ntl::wfp::network_send_packet(std::move(packet)), compartment);
}

[[maybe_unused]] ntl::status compile_transport_injection(
    ntl::wfp::transport_injector &injector, ntl::wfp::cloned_packet packet,
    ADDRESS_FAMILY family, COMPARTMENT_ID compartment) noexcept {
  return injector.inject_receive(
      ntl::wfp::transport_receive_packet(std::move(packet)), family,
      compartment, 0, 0);
}

[[maybe_unused]] ntl::status compile_transport_send_injection(
    ntl::wfp::transport_injector &injector, ntl::wfp::cloned_packet packet,
    UINT64 endpoint, ADDRESS_FAMILY family, COMPARTMENT_ID compartment,
    std::span<const std::byte> remote_address,
    std::span<const std::byte> control_data) noexcept {
  auto request = ntl::wfp::transport_send_request::try_copy(
      endpoint, family, compartment, remote_address, SCOPE_ID{}, control_data,
      ntl::net::buffer_limits{4096}, ntl::pool_tag("tCwN"));
  if (!request)
    return request.status();
  return injector.inject_send(ntl::wfp::transport_send_packet(
      std::move(packet), std::move(*request)));
}

[[maybe_unused]] ntl::status compile_owned_transport_send_injection(
    ntl::wfp::transport_injector &injector,
    ntl::wfp::owned_injection_packet packet,
    ntl::wfp::transport_send_request request) noexcept {
  return injector.inject_send(ntl::wfp::transport_send_packet(
      std::move(packet), std::move(request)));
}

ntl::status validate_transport_send_request_contract() noexcept {
  std::array<std::byte, 4> remote_address{
      std::byte{192}, std::byte{0}, std::byte{2}, std::byte{1}};
  std::array<std::byte, 4> control_data{
      std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
  auto request = ntl::wfp::transport_send_request::try_copy(
      1, AF_INET, static_cast<COMPARTMENT_ID>(1), remote_address, SCOPE_ID{},
      control_data, ntl::net::buffer_limits{control_data.size()},
      ntl::pool_tag("rCwN"));
  if (!request)
    return request.status();

  remote_address.fill(std::byte{0});
  control_data.fill(std::byte{0});
  auto moved = std::move(*request);
  if (!moved || moved.family() != AF_INET ||
      moved.remote_address().size() != 4 ||
      moved.remote_address().front() != std::byte{192} ||
      moved.control_data().size() != 4 ||
      moved.control_data().back() != std::byte{4})
    return STATUS_DATA_ERROR;

  auto raw = ntl::wfp::transport_send_request::raw_packet(
      2, AF_INET6, static_cast<COMPARTMENT_ID>(2));
  if (!raw || !*raw || raw->has_send_parameters())
    return STATUS_DATA_ERROR;
  *raw = std::move(moved);
  return *raw && raw->remote_address().front() == std::byte{192}
             ? ntl::status::ok()
             : ntl::status{STATUS_DATA_ERROR};
}

[[maybe_unused]] ntl::result<ntl::wfp::pended_operation>
compile_ale_pend(ntl::wfp::metadata_view metadata) noexcept {
  return ntl::wfp::pended_operation::try_create(metadata);
}

[[maybe_unused]] ntl::result<ntl::wfp::cloned_stream_data>
compile_stream_clone(ntl::wfp::stream_data_view data) noexcept {
  return ntl::wfp::cloned_stream_data::try_create(data);
}

[[maybe_unused]] ntl::status compile_stream_injection(
    ntl::wfp::stream_injector &injector,
    ntl::wfp::stream_injection_site<stream_layer> site,
    ntl::wfp::cloned_stream_data data) noexcept {
  return injector.inject(site, std::move(data));
}

[[maybe_unused]] ntl::status compile_stream_replacement(
    ntl::wfp::stream_injector &injector,
    ntl::wfp::stream_injection_site<stream_layer> site) noexcept {
  constexpr char replacement[] = "replacement";
  auto data = injector.try_make_data(
      replacement, sizeof(replacement) - 1, FWPS_STREAM_FLAG_SEND);
  if (!data)
    return data.status();
  return injector.inject(site, std::move(*data));
}

[[maybe_unused]] ntl::result<ntl::net::owned_bytes>
compile_packet_copy(ntl::wfp::borrowed_packet packet) noexcept {
  ntl::net::borrowed_byte_cursor cursor(packet.bytes());
  const auto source = cursor.read_be16();
  const auto destination = cursor.read_be16();
  if (!source || !destination)
    return ntl::unexpected(STATUS_BUFFER_TOO_SMALL);
  return packet.try_copy(ntl::net::buffer_limits{64 * 1024});
}

[[maybe_unused]] ntl::status
compile_packet_edit(ntl::wfp::cloned_packet &packet) noexcept {
  auto bytes = packet.edit_bytes();
  const ntl::status port = bytes.write_be16(2, 443);
  if (!port.is_ok())
    return port;
  return bytes.write_be16(6, 0);
}

[[maybe_unused]] ntl::wfp::terminating_decision compile_connect_redirect(
    const ntl::wfp::connect_redirector &redirector,
    const ntl::wfp::classify_event<redirect_layer> &event) noexcept {
  return redirector.redirect(
      event, ntl::wfp::local_proxy_target{1234, 8080});
}

[[maybe_unused]] ntl::wfp::terminating_decision compile_connect_redirect_v6(
    const ntl::wfp::connect_redirector &redirector,
    const ntl::wfp::classify_event<redirect_layer_v6> &event) noexcept {
  return redirector.redirect(
      event, ntl::wfp::local_proxy_target{1234, 8080});
}

[[maybe_unused]] ntl::wfp::terminating_decision compile_bind_redirect(
    const ntl::wfp::classify_event<bind_redirect_layer> &event) noexcept {
  return ntl::wfp::bind_redirector::redirect(
      event, ntl::wfp::local_bind_target_v4{
                 0x7f000001u, 9000, 0});
}

[[maybe_unused]] ntl::wfp::terminating_decision compile_bind_redirect_v6(
    const ntl::wfp::classify_event<bind_redirect_layer_v6> &event) noexcept {
  ntl::wfp::local_bind_target_v6 target{};
  target.address[15] = 1;
  target.port = 9000;
  return ntl::wfp::bind_redirector::redirect(event, target);
}

template <class Layer>
inline constexpr auto observe_specialized =
    +[](const ntl::wfp::classify_event<Layer> &) noexcept {};

template <class Layer>
ntl::status compile_specialized_registration(
  ntl::wfp::callout_driver<> &callouts,
    const GUID &guid) noexcept {
  return callouts.add_inspection(ntl::wfp::inspection_callout_key<Layer>(guid),
                                 observe_specialized<Layer>);
}

[[maybe_unused]] ntl::status compile_all_specialized_layers(
    ntl::wfp::callout_driver<> &callouts) noexcept {
  constexpr GUID keys[] = {
      {0xca664ea0, 0x1f3d, 0x44bd,
       {0x88, 0xe7, 0x2c, 0xd8, 0xcf, 0xb0, 0xa6, 0x01}},
      {0xca664ea1, 0x1f3d, 0x44bd,
       {0x88, 0xe7, 0x2c, 0xd8, 0xcf, 0xb0, 0xa6, 0x01}},
      {0xca664ea2, 0x1f3d, 0x44bd,
       {0x88, 0xe7, 0x2c, 0xd8, 0xcf, 0xb0, 0xa6, 0x01}},
      {0xca664ea3, 0x1f3d, 0x44bd,
       {0x88, 0xe7, 0x2c, 0xd8, 0xcf, 0xb0, 0xa6, 0x01}},
      {0xca664ea4, 0x1f3d, 0x44bd,
       {0x88, 0xe7, 0x2c, 0xd8, 0xcf, 0xb0, 0xa6, 0x01}},
      {0xca664ea5, 0x1f3d, 0x44bd,
       {0x88, 0xe7, 0x2c, 0xd8, 0xcf, 0xb0, 0xa6, 0x01}},
      {0xca664ea6, 0x1f3d, 0x44bd,
       {0x88, 0xe7, 0x2c, 0xd8, 0xcf, 0xb0, 0xa6, 0x01}},
      {0xca664ea7, 0x1f3d, 0x44bd,
       {0x88, 0xe7, 0x2c, 0xd8, 0xcf, 0xb0, 0xa6, 0x01}},
  };
  ntl::status result =
      compile_specialized_registration<
          ntl::wfp::layers::ale_endpoint_closure_v4>(
          callouts, keys[0]);
  if (!result.is_ok())
    return result;
  result = compile_specialized_registration<
      ntl::wfp::layers::ale_endpoint_closure_v6>(
      callouts, keys[1]);
  if (!result.is_ok())
    return result;
  result = compile_specialized_registration<
      ntl::wfp::layers::name_resolution_cache_v4>(
      callouts, keys[2]);
  if (!result.is_ok())
    return result;
  result = compile_specialized_registration<
      ntl::wfp::layers::name_resolution_cache_v6>(
      callouts, keys[3]);
  if (!result.is_ok())
    return result;
  result = compile_specialized_registration<
      ntl::wfp::layers::inbound_mac_frame_ethernet>(
      callouts, keys[4]);
  if (!result.is_ok())
    return result;
  result = compile_specialized_registration<
      ntl::wfp::layers::outbound_mac_frame_ethernet>(
      callouts, keys[5]);
  if (!result.is_ok())
    return result;
  result = compile_specialized_registration<
      ntl::wfp::layers::ingress_vswitch_ethernet>(
      callouts, keys[6]);
  if (!result.is_ok())
    return result;
  result = compile_specialized_registration<
      ntl::wfp::layers::egress_vswitch_ethernet>(
      callouts, keys[7]);
  return result;
}

#if NTL_HAS_COROUTINE_SUPPORT
struct compile_coroutine {
  struct promise_type {
    compile_coroutine get_return_object() const noexcept { return {}; }
    std::suspend_never initial_suspend() const noexcept { return {}; }
    std::suspend_never final_suspend() const noexcept { return {}; }
    void return_void() const noexcept {}
    void unhandled_exception() const noexcept {}
  };
};

struct message_header {
  std::uint16_t kind;
  std::uint16_t length;
};

[[maybe_unused]] compile_coroutine
compile_stream_reader(ntl::net::async_byte_stream &stream) {
  auto header = co_await stream.read_exactly<message_header>(
      {std::chrono::milliseconds(250)});
  if (!header)
    co_return;

  const std::size_t body_length =
      RtlUshortByteSwap(header->length);
  auto body = ntl::net::owned_bytes::try_allocate(
      body_length, ntl::net::buffer_limits{4096});
  if (!body)
    co_return;

  const ntl::status read =
      co_await stream.read_exactly_borrowed(body->span());
  if (!read.is_ok())
    co_return;
}

[[maybe_unused]] ntl::status compile_wfp_stream_adapter(
    ntl::wfp::stream_reader &reader,
    const ntl::wfp::stream_event<stream_layer, stream_context>
        &event) noexcept {
  return reader.ingest(event);
}
#endif

} // namespace

ntl::status ntl::main(ntl::driver &driver, const std::wstring &) {
  const ntl::status send_request_contract =
      validate_transport_send_request_contract();
  if (!send_request_contract.is_ok())
    return send_request_contract;

  [[maybe_unused]] auto replacement_injector =
      ntl::wfp::stream_injector::try_create(
          driver.native_handle(), AF_INET);
#if NTL_HAS_COROUTINE_SUPPORT
  [[maybe_unused]] auto reader =
      ntl::net::async_byte_stream::try_create(4096);
#endif
  auto network_executor =
      std::make_shared<ntl::net::kernel::executor>();
  ntl::wfp::callout_driver<> callouts(driver);
  auto callback_state = std::make_shared<owning_policy_state>();

  ntl::status result = callouts.add_terminating(
      connect_key, callback_state,
      [](owning_policy_state &state,
         const ntl::wfp::classify_event<connect_layer> &event) noexcept {
        ++state.callbacks;
        return decide(event);
      });
  if (!result.is_ok())
    return result;

  result = callouts.add_inspection(
      flow_key, callback_state,
      [](owning_policy_state &state,
         const ntl::wfp::classify_event<flow_layer> &event) noexcept {
        ++state.callbacks;
        observe_flow(event);
      });
  if (!result.is_ok())
    return result;

  auto stream = callouts.add_stream<stream_context>(
      stream_key, callback_state,
      [](owning_policy_state &state,
         const ntl::wfp::stream_event<stream_layer, stream_context> &event)
          noexcept {
        ++state.callbacks;
        return inspect_stream(event);
      });
  if (!stream)
    return stream.status();

  auto datagram =
      callouts.add_flow_context_terminating<proxy_context>(
      datagram_key, callback_state,
      [](owning_policy_state &state,
         const ntl::wfp::classify_event<datagram_layer> &event,
         proxy_context *context) noexcept {
        ++state.callbacks;
        return proxy_datagram(event, context);
      });
  if (!datagram)
    return datagram.status();

  driver.on_unload([callouts, network_executor] {
    const ntl::status result = callouts.close();
    NT_ASSERT(result.is_ok());
    network_executor->stop_accepting();
    const ntl::status drained = network_executor->drain();
    NT_ASSERT(drained.is_ok());
  });
  return ntl::status::ok();
}
