#include <ntddk.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <type_traits>

#include <ntl/device_endpoint>
#include <ntl/driver>
#include <ntl/except>
#include <ntl/ioctl>
#include <ntl/wfp/all>

#include "browser_https_inspection_contract.hpp"
namespace inspection_contract = wfp_browser_https_inspection;

namespace {

ntl::wfp::connect_redirector *g_redirector = nullptr;

using query_telemetry = ntl::ioctl_from_contract<
    inspection_contract::query_telemetry_contract>;

class quic_telemetry_state {
public:
  template <class Layer>
  void record(
      const ntl::wfp::classify_event<Layer> &event) noexcept {
    static_assert(
        std::is_same_v<Layer, inspection_contract::quic_layer_v4> ||
        std::is_same_v<Layer, inspection_contract::quic_layer_v6>);
    auto &state =
        [&]() noexcept -> layer_state & {
          if constexpr (std::is_same_v<
                            Layer,
                            inspection_contract::quic_layer_v4>)
            return ipv4_;
          else
            return ipv6_;
        }();

    ::InterlockedIncrement64(&state.classify_hits);
    ::InterlockedIncrement64(&state.block_decisions);
    if (event.action_write_available())
      ::InterlockedIncrement64(&state.action_write_available);
    else
      ::InterlockedIncrement64(&state.action_write_missing);
    if (event.current_action() == FWP_ACTION_PERMIT)
      ::InterlockedIncrement64(&state.initial_permit);

    exchange64(state.last_filter_id, event.filter().id());
    exchange32(
        state.last_filter_flags, event.filter().flags());
    const auto process_id = event.metadata().process_id();
    exchange64(
        state.last_process_id,
        process_id ? *process_id : 0);

    const auto protocol =
        event.value(Layer::field::protocol).uint8();
    exchange32(
        state.last_protocol,
        protocol ? *protocol : 0);
    const auto remote_port =
        event.value(Layer::field::remote_port).uint16();
    exchange32(
        state.last_remote_port,
        remote_port ? *remote_port : 0);

    const auto *const application =
        event.value(Layer::field::app_id).byte_blob();
    if (application && application->data &&
        application->size != 0) {
      exchange64(
          state.last_application_id_hash,
          inspection_contract::hash_application_id(
              application->data, application->size));
      exchange32(
          state.last_application_id_size,
          application->size);
    } else {
      exchange64(state.last_application_id_hash, 0);
      exchange32(state.last_application_id_size, 0);
    }

    if constexpr (std::is_same_v<
                      Layer,
                      inspection_contract::quic_layer_v4>) {
      const auto address =
          event.value(Layer::field::remote_address).uint32();
      exchange32(
          state.last_remote_address_v4,
          address ? *address : 0);
      exchange32(state.address_family, AF_INET);
    } else {
      const auto *const address =
          event.value(
                   Layer::field::remote_address)
              .byte_array16();
      for (std::size_t index = 0; index != 4; ++index) {
        std::uint32_t word = 0;
        if (address) {
          std::memcpy(
              &word, address->byteArray16 +
                         index * sizeof(word),
              sizeof(word));
        }
        exchange32(
            state.last_remote_address_v6[index],
            word);
      }
      exchange32(state.address_family, AF_INET6);
    }
  }

  inspection_contract::quic_telemetry snapshot() const noexcept {
    inspection_contract::quic_telemetry result{};
    result.version = inspection_contract::telemetry_version;
    result.size = sizeof(result);
    copy(ipv4_, result.ipv4);
    copy(ipv6_, result.ipv6);
    return result;
  }

private:
  struct alignas(8) layer_state {
    volatile LONG64 classify_hits = 0;
    volatile LONG64 block_decisions = 0;
    volatile LONG64 action_write_available = 0;
    volatile LONG64 action_write_missing = 0;
    volatile LONG64 initial_permit = 0;
    volatile LONG64 last_filter_id = 0;
    volatile LONG64 last_process_id = 0;
    volatile LONG64 last_application_id_hash = 0;
    volatile LONG last_application_id_size = 0;
    volatile LONG last_remote_address_v4 = 0;
    volatile LONG last_remote_address_v6[4]{};
    volatile LONG last_remote_port = 0;
    volatile LONG last_filter_flags = 0;
    volatile LONG last_protocol = 0;
    volatile LONG address_family = 0;
  };

  static void exchange64(
      volatile LONG64 &target,
      std::uint64_t value) noexcept {
    (void)::InterlockedExchange64(
        &target, static_cast<LONG64>(value));
  }

  static void exchange32(
      volatile LONG &target,
      std::uint32_t value) noexcept {
    (void)::InterlockedExchange(
        &target, static_cast<LONG>(value));
  }

  static std::uint64_t read64(
      const volatile LONG64 &value) noexcept {
    return static_cast<std::uint64_t>(
        ::InterlockedCompareExchange64(
            const_cast<volatile LONG64 *>(&value), 0, 0));
  }

  static std::uint32_t read32(
      const volatile LONG &value) noexcept {
    return static_cast<std::uint32_t>(
        ::InterlockedCompareExchange(
            const_cast<volatile LONG *>(&value), 0, 0));
  }

  static void copy(
      const layer_state &source,
      inspection_contract::quic_layer_telemetry &target) noexcept {
    target.classify_hits = read64(source.classify_hits);
    target.block_decisions = read64(source.block_decisions);
    target.action_write_available =
        read64(source.action_write_available);
    target.action_write_missing =
        read64(source.action_write_missing);
    target.initial_permit = read64(source.initial_permit);
    target.last_filter_id = read64(source.last_filter_id);
    target.last_process_id = read64(source.last_process_id);
    target.last_application_id_hash =
        read64(source.last_application_id_hash);
    target.last_application_id_size =
        read32(source.last_application_id_size);
    target.last_remote_address_v4 =
        read32(source.last_remote_address_v4);
    for (std::size_t index = 0; index != 4; ++index)
      target.last_remote_address_v6[index] =
          read32(source.last_remote_address_v6[index]);
    target.last_remote_port =
        static_cast<std::uint16_t>(
            read32(source.last_remote_port));
    target.last_filter_flags =
        static_cast<std::uint16_t>(
            read32(source.last_filter_flags));
    target.last_protocol =
        static_cast<std::uint8_t>(
            read32(source.last_protocol));
    target.address_family =
        static_cast<std::uint8_t>(
            read32(source.address_family));
  }

  layer_state ipv4_;
  layer_state ipv6_;
};

quic_telemetry_state *g_quic_telemetry = nullptr;

template <class Layer>
ntl::wfp::decision redirect_selected_connection(
    const ntl::wfp::classify_event<Layer> &event) noexcept {
      auto *const redirector = g_redirector;
      const auto protocol =
          event.value(
                   Layer::field::protocol)
              .uint8();
      if (!redirector || !protocol ||
          (*protocol != IPPROTO_TCP &&
           *protocol != IPPROTO_UDP))
        return ntl::wfp::decision::block;

      const auto target =
          ntl::wfp::local_proxy_target::from_filter_context(
              event.filter().context());
      return redirector->redirect(event, target);
}

constexpr auto redirect_selected_connection_v4 =
    &redirect_selected_connection<inspection_contract::layer_v4>;
constexpr auto redirect_selected_connection_v6 =
    &redirect_selected_connection<inspection_contract::layer_v6>;

template <class Layer>
ntl::wfp::decision block_selected_quic(
    const ntl::wfp::classify_event<Layer> &event) noexcept {
  if (auto *const telemetry = g_quic_telemetry)
    telemetry->record(event);
  return ntl::wfp::decision::block;
}

constexpr auto block_selected_quic_v4 =
    &block_selected_quic<inspection_contract::quic_layer_v4>;
constexpr auto block_selected_quic_v6 =
    &block_selected_quic<inspection_contract::quic_layer_v6>;

} // namespace

ntl::status ntl::main(ntl::driver &driver, const std::wstring &) {
  auto telemetry =
      std::make_shared<quic_telemetry_state>();
  auto endpoint_options =
      ntl::device_options()
          .name(inspection_contract::device_name)
          .type(FILE_DEVICE_UNKNOWN)
          .exclusive(false)
          .security_descriptor(
              L"D:P(A;;GA;;;SY)(A;;GR;;;BA)",
              inspection_contract::device_class_guid);
  auto endpoint_result =
      ntl::try_create_device_endpoint<void>(
          driver, endpoint_options);
  if (!endpoint_result)
    return endpoint_result.status();
  auto endpoint =
      std::make_shared<ntl::device_endpoint<void>>(
          std::move(*endpoint_result));
  auto telemetry_device = endpoint->device();
  if (!telemetry_device)
    return STATUS_INVALID_DEVICE_STATE;

  telemetry_device->on_create(
      [](ntl::irp &request) { request.succeed(); });
  telemetry_device->on_close(
      [](ntl::irp &request) { request.succeed(); });
  telemetry_device->on_device_control(
      [telemetry](
          const ntl::device_control::code &code,
          const ntl::device_control::in_buffer &,
          ntl::device_control::out_buffer &out) {
        if (!ntl::is_ioctl<query_telemetry>(code)) {
          out.clear();
          throw ntl::exception(
              STATUS_INVALID_DEVICE_REQUEST,
              "unknown browser-inspection IOCTL");
        }
        if (!ntl::ioctl_write_output<query_telemetry>(
                out, telemetry->snapshot()))
          throw ntl::exception(
              STATUS_BUFFER_TOO_SMALL,
              "browser-inspection telemetry output is too small");
      });

  auto created =
      ntl::wfp::connect_redirector::try_create(
          inspection_contract::provider_key);
  if (!created)
    return created.status();

  auto redirector = std::make_shared<ntl::wfp::connect_redirector>(
      std::move(*created));
  auto callouts =
      std::make_shared<ntl::wfp::callout_driver<>>(driver);

  g_redirector = redirector.get();
  g_quic_telemetry = telemetry.get();
  const ntl::status registered_v4 =
      callouts->add<redirect_selected_connection_v4>(
          inspection_contract::callout_key_v4);
  if (!registered_v4.is_ok()) {
    g_redirector = nullptr;
    g_quic_telemetry = nullptr;
    return registered_v4;
  }
  const ntl::status registered_v6 =
      callouts->add<redirect_selected_connection_v6>(
          inspection_contract::callout_key_v6);
  if (!registered_v6.is_ok()) {
    (void)callouts->reset();
    g_redirector = nullptr;
    g_quic_telemetry = nullptr;
    return registered_v6;
  }
  const ntl::status registered_quic_v4 =
      callouts->add<block_selected_quic_v4>(
          inspection_contract::quic_callout_key_v4);
  if (!registered_quic_v4.is_ok()) {
    (void)callouts->reset();
    g_redirector = nullptr;
    g_quic_telemetry = nullptr;
    return registered_quic_v4;
  }
  const ntl::status registered_quic_v6 =
      callouts->add<block_selected_quic_v6>(
          inspection_contract::quic_callout_key_v6);
  if (!registered_quic_v6.is_ok()) {
    (void)callouts->reset();
    g_redirector = nullptr;
    g_quic_telemetry = nullptr;
    return registered_quic_v6;
  }

  driver.on_unload(
      [callouts, redirector, telemetry, endpoint] {
        endpoint->link().reset();
        const ntl::status result = callouts->reset();
        NT_ASSERT(result.is_ok());
        g_redirector = nullptr;
        g_quic_telemetry = nullptr;
        redirector->reset();
        endpoint->reset();
      });
  return ntl::status::ok();
}
