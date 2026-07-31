#include <ntddk.h>

#include <array>
#include <chrono>
#include <coroutine>
#include <cstddef>
#include <memory>
#include <span>
#include <type_traits>
#include <utility>

#include <ntl/net/io/async_byte_stream>
#include <ntl/driver>
#include <ntl/net/buffer/owned_bytes>
#include <ntl/wfp/all>

namespace {

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

constexpr ntl::wfp::callout_key<connect_layer> connect_key(connect_guid);
constexpr ntl::wfp::callout_key<flow_layer> flow_key(flow_guid);
constexpr ntl::wfp::callout_key<stream_layer> stream_key(stream_guid);
constexpr ntl::wfp::callout_key<datagram_layer> datagram_key(datagram_guid);
constexpr ntl::wfp::callout_key<redirect_layer> redirect_key(redirect_guid);
constexpr ntl::wfp::callout_key<bind_redirect_layer>
    bind_redirect_key(bind_redirect_guid);

struct stream_context {
  std::uint64_t inspected = 0;
  ~stream_context() noexcept = default;
};

struct proxy_context {
  std::uint64_t packets = 0;
  ~proxy_context() noexcept = default;
};

constexpr auto decide =
    +[](const ntl::wfp::classify_event<connect_layer> &event) noexcept {
      return event.value(connect_layer::field::protocol)
                     .uint8()
                     .has_value()
                 ? ntl::wfp::decision::permit
                 : ntl::wfp::decision::continue_classification;
    };

constexpr auto observe_flow =
    +[](const ntl::wfp::classify_event<flow_layer> &event) noexcept {
      return event.metadata().flow_handle()
                     ? ntl::wfp::decision::permit
                     : ntl::wfp::decision::continue_classification;
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
        return ntl::wfp::decision::continue_classification;
      ++context->packets;
      return ntl::wfp::decision::block_and_absorb;
    };

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
              ntl::wfp::callout_key<connect_layer>, GUID>);
static_assert(!std::is_constructible_v<
              ntl::wfp::callout_key<connect_layer>,
              ntl::wfp::callout_key<flow_layer>>);
static_assert(!std::is_copy_constructible_v<
              ntl::wfp::classify_event<connect_layer>>);
static_assert(!std::is_copy_constructible_v<
              ntl::wfp::stream_event<stream_layer, stream_context>>);
static_assert(!std::is_copy_constructible_v<ntl::wfp::cloned_packet>);
static_assert(!std::is_copy_constructible_v<ntl::wfp::network_injector>);
static_assert(!std::is_copy_constructible_v<ntl::wfp::transport_injector>);
static_assert(!std::is_copy_constructible_v<ntl::wfp::stream_injector>);
static_assert(!std::is_copy_constructible_v<ntl::wfp::cloned_stream_data>);
static_assert(!std::is_copy_constructible_v<ntl::wfp::injected_stream_data>);
static_assert(!std::is_copy_constructible_v<ntl::net::owned_bytes>);
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
  return injector.inject_send(std::move(packet), compartment);
}

[[maybe_unused]] ntl::status compile_transport_injection(
    ntl::wfp::transport_injector &injector, ntl::wfp::cloned_packet packet,
    ADDRESS_FAMILY family, COMPARTMENT_ID compartment) noexcept {
  return injector.inject_receive(std::move(packet), family, compartment, 0,
                                 0);
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
  ntl::net::byte_cursor cursor(packet.bytes());
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

[[maybe_unused]] ntl::wfp::decision compile_connect_redirect(
    const ntl::wfp::connect_redirector &redirector,
    const ntl::wfp::classify_event<redirect_layer> &event) noexcept {
  return redirector.redirect(
      event, ntl::wfp::local_proxy_target{1234, 8080});
}

[[maybe_unused]] ntl::wfp::decision compile_connect_redirect_v6(
    const ntl::wfp::connect_redirector &redirector,
    const ntl::wfp::classify_event<redirect_layer_v6> &event) noexcept {
  return redirector.redirect(
      event, ntl::wfp::local_proxy_target{1234, 8080});
}

[[maybe_unused]] ntl::wfp::decision compile_bind_redirect(
    const ntl::wfp::classify_event<bind_redirect_layer> &event) noexcept {
  return ntl::wfp::bind_redirector::redirect(
      event, ntl::wfp::local_bind_target_v4{
                 0x7f000001u, 9000, 0});
}

[[maybe_unused]] ntl::wfp::decision compile_bind_redirect_v6(
    const ntl::wfp::classify_event<bind_redirect_layer_v6> &event) noexcept {
  ntl::wfp::local_bind_target_v6 target{};
  target.address[15] = 1;
  target.port = 9000;
  return ntl::wfp::bind_redirector::redirect(event, target);
}

template <class Layer>
inline constexpr auto observe_specialized =
    +[](const ntl::wfp::classify_event<Layer> &) noexcept {
      return ntl::wfp::decision::continue_classification;
    };

template <class Layer>
ntl::status compile_specialized_registration(
    ntl::wfp::callout_driver<> &callouts,
    const GUID &guid) noexcept {
  return callouts.add<observe_specialized<Layer>>(
      ntl::wfp::callout_key<Layer>(guid));
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
      co_await stream.read_exactly(body->span());
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
  [[maybe_unused]] auto replacement_injector =
      ntl::wfp::stream_injector::try_create(
          driver.native_handle(), AF_INET);
#if NTL_HAS_COROUTINE_SUPPORT
  [[maybe_unused]] auto reader =
      ntl::net::async_byte_stream::try_create(4096);
#endif
  auto callouts = std::make_shared<ntl::wfp::callout_driver<>>(driver);

  ntl::status result = callouts->add<decide>(connect_key);
  if (!result.is_ok())
    return result;

  result = callouts->add<observe_flow>(flow_key);
  if (!result.is_ok())
    return result;

  auto stream =
      callouts->add_stream<stream_context, inspect_stream>(stream_key);
  if (!stream)
    return stream.status();

  auto datagram =
      callouts->add_flow_context<proxy_context, proxy_datagram>(
          datagram_key);
  if (!datagram)
    return datagram.status();

  driver.on_unload([callouts] {
    const ntl::status result = callouts->reset();
    NT_ASSERT(result.is_ok());
  });
  return ntl::status::ok();
}
