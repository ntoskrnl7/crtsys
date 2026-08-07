#include <ntddk.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <type_traits>

#include <ntl/device_endpoint>
#include <ntl/driver>
#include <ntl/except>
#include <ntl/ioctl>
#include <ntl/wfp/all>

#include "browser_https_inspection_contract.hpp"
#include "http3_service.hpp"
#include "tcp_service.hpp"

namespace contract = wfp_kernel_browser_https_inspection;
namespace browser_driver = crtsys::wfp_kernel_browser_https::driver;

namespace {

using configure_identity =
    ntl::ioctl_from_contract<contract::configure_identity_contract>;
using query_service =
    ntl::ioctl_from_contract<contract::query_service_contract>;
using read_inspection =
    ntl::ioctl_from_contract<contract::read_inspection_contract>;
using read_identity_request =
    ntl::ioctl_from_contract<contract::read_identity_request_contract>;
using query_telemetry =
    ntl::ioctl_from_contract<contract::query_telemetry_contract>;
using configure_origin_security =
    ntl::ioctl_from_contract<contract::configure_origin_security_contract>;
using arm_origin_security_rollback_test = ntl::ioctl_from_contract<
    contract::arm_origin_security_rollback_test_contract>;

class telemetry_state final : public ntl::wfp::transparent_udp_proxy_observer {
public:
  template <class Layer>
  void record(const ntl::wfp::classify_event<Layer> &event) noexcept {
    static_assert(std::is_same_v<Layer, contract::quic_layer_v4> ||
                  std::is_same_v<Layer, contract::quic_layer_v6>);
    layer_state &state = [&]() noexcept -> layer_state & {
      if constexpr (std::is_same_v<Layer, contract::quic_layer_v4>)
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
    exchange32(state.last_filter_flags, event.filter().flags());
    const auto process_id = event.metadata().process_id();
    exchange64(state.last_process_id, process_id ? *process_id : 0);
    if constexpr (std::is_same_v<Layer, contract::quic_datagram_layer_v4> ||
                  std::is_same_v<Layer, contract::quic_datagram_layer_v6>) {
      const auto protocol = event.value(Layer::field::protocol).uint8();
      exchange32(state.last_protocol, protocol ? *protocol : 0);
      const auto remote_port =
          event.value(Layer::field::remote_port).uint16();
      exchange32(state.last_remote_port, remote_port ? *remote_port : 0);
    } else {
      exchange32(state.last_protocol, IPPROTO_UDP);
      exchange32(state.last_remote_port, 0);
    }
    const auto *const application =
        event.value(Layer::field::app_id).byte_blob();
    if (application && application->data && application->size != 0) {
      exchange64(
          state.last_application_id_hash,
          contract::hash_application_id(application->data, application->size));
      exchange32(state.last_application_id_size, application->size);
    } else {
      exchange64(state.last_application_id_hash, 0);
      exchange32(state.last_application_id_size, 0);
    }
    if constexpr (std::is_same_v<Layer, contract::quic_layer_v4>) {
      const auto address = event.value(Layer::field::remote_address).uint32();
      exchange32(state.last_remote_address_v4, address ? *address : 0);
      exchange32(state.address_family, AF_INET);
    } else {
      const auto *const address =
          event.value(Layer::field::remote_address).byte_array16();
      for (std::size_t index = 0; index != 4; ++index) {
        std::uint32_t word = 0;
        if (address) {
          std::memcpy(&word, address->byteArray16 + index * sizeof(word),
                      sizeof(word));
        }
        exchange32(state.last_remote_address_v6[index], word);
      }
      exchange32(state.address_family, AF_INET6);
    }
  }

  template <class Layer>
  void record_udp_packet(
      const ntl::wfp::classify_event<Layer> &event) noexcept {
    static_assert(
        std::is_same_v<Layer, contract::quic_datagram_layer_v4> ||
        std::is_same_v<Layer, contract::quic_datagram_layer_v6> ||
        std::is_same_v<Layer, contract::quic_reverse_layer_v4> ||
        std::is_same_v<Layer, contract::quic_reverse_layer_v6>);
    layer_state &state = [&]() noexcept -> layer_state & {
      if constexpr (std::is_same_v<Layer,
                                   contract::quic_datagram_layer_v4> ||
                    std::is_same_v<Layer,
                                   contract::quic_reverse_layer_v4>)
        return ipv4_;
      else
        return ipv6_;
    }();
    ::InterlockedIncrement64(&state.classify_hits);
    if (event.action_write_available())
      ::InterlockedIncrement64(&state.action_write_available);
    else
      ::InterlockedIncrement64(&state.action_write_missing);
    if (event.current_action() == FWP_ACTION_PERMIT)
      ::InterlockedIncrement64(&state.initial_permit);
    exchange64(state.last_filter_id, event.filter().id());
    exchange32(state.last_filter_flags, event.filter().flags());
    if constexpr (std::is_same_v<Layer, contract::quic_datagram_layer_v4> ||
                  std::is_same_v<Layer, contract::quic_datagram_layer_v6>) {
      const auto protocol = event.value(Layer::field::protocol).uint8();
      exchange32(state.last_protocol, protocol ? *protocol : 0);
      const auto remote_port =
          event.value(Layer::field::remote_port).uint16();
      exchange32(state.last_remote_port, remote_port ? *remote_port : 0);
    } else {
      exchange32(state.last_protocol, IPPROTO_UDP);
      exchange32(state.last_remote_port, 0);
    }
    exchange32(state.address_family,
               (std::is_same_v<Layer, contract::quic_datagram_layer_v4> ||
                std::is_same_v<Layer, contract::quic_reverse_layer_v4>)
                   ? AF_INET
                   : AF_INET6);
  }

  void on_outbound(const ntl::wfp::classify_event<
                   contract::quic_datagram_layer_v4> &event) noexcept override {
    record_udp_packet(event);
  }
  void on_outbound(const ntl::wfp::classify_event<
                   contract::quic_datagram_layer_v6> &event) noexcept override {
    record_udp_packet(event);
  }
  void on_inbound(const ntl::wfp::classify_event<
                  contract::quic_reverse_layer_v4> &event) noexcept override {
    record_udp_packet(event);
  }
  void on_inbound(const ntl::wfp::classify_event<
                  contract::quic_reverse_layer_v6> &event) noexcept override {
    record_udp_packet(event);
  }

  contract::quic_telemetry snapshot(
      const ntl::wfp::transparent_udp_proxy_statistics &translation = {})
      const noexcept {
    contract::quic_telemetry result{};
    result.version = contract::telemetry_version;
    result.size = static_cast<std::uint32_t>(sizeof(result));
    copy(ipv4_, result.ipv4);
    copy(ipv6_, result.ipv6);
    result.translation = {
        translation.outbound_packets,
        translation.inbound_packets,
        translation.mapping_updates,
        translation.mapping_misses,
        translation.injection_failures,
        translation.quota_rejections,
        translation.last_mapping_family,
        translation.last_mapping_source_port,
        translation.last_mapping_destination_port,
        translation.last_mapping_proxy_port,
        translation.last_resolution_peer_family,
        translation.last_resolution_peer_port,
        translation.last_resolution_proxy_family,
        translation.last_resolution_proxy_port,
        translation.last_resolution_status,
        0};
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
  static void exchange64(volatile LONG64 &target,
                         std::uint64_t value) noexcept {
    (void)::InterlockedExchange64(&target, static_cast<LONG64>(value));
  }
  static void exchange32(volatile LONG &target, std::uint32_t value) noexcept {
    (void)::InterlockedExchange(&target, static_cast<LONG>(value));
  }
  static std::uint64_t read64(const volatile LONG64 &value) noexcept {
    return static_cast<std::uint64_t>(::InterlockedCompareExchange64(
        const_cast<volatile LONG64 *>(&value), 0, 0));
  }
  static std::uint32_t read32(const volatile LONG &value) noexcept {
    return static_cast<std::uint32_t>(::InterlockedCompareExchange(
        const_cast<volatile LONG *>(&value), 0, 0));
  }
  static void copy(const layer_state &source,
                   contract::quic_layer_telemetry &target) noexcept {
    target.classify_hits = read64(source.classify_hits);
    target.block_decisions = read64(source.block_decisions);
    target.action_write_available = read64(source.action_write_available);
    target.action_write_missing = read64(source.action_write_missing);
    target.initial_permit = read64(source.initial_permit);
    target.last_filter_id = read64(source.last_filter_id);
    target.last_process_id = read64(source.last_process_id);
    target.last_application_id_hash = read64(source.last_application_id_hash);
    target.last_application_id_size = read32(source.last_application_id_size);
    target.last_remote_address_v4 = read32(source.last_remote_address_v4);
    for (std::size_t index = 0; index != 4; ++index)
      target.last_remote_address_v6[index] =
          read32(source.last_remote_address_v6[index]);
    target.last_remote_port =
        static_cast<std::uint16_t>(read32(source.last_remote_port));
    target.last_filter_flags =
        static_cast<std::uint16_t>(read32(source.last_filter_flags));
    target.last_protocol =
        static_cast<std::uint8_t>(read32(source.last_protocol));
    target.address_family =
        static_cast<std::uint8_t>(read32(source.address_family));
  }
  layer_state ipv4_{};
  layer_state ipv6_{};
};

struct browser_callout_state {
  std::shared_ptr<ntl::wfp::connect_redirector> redirector;
  std::shared_ptr<telemetry_state> telemetry;
};

template <class Layer>
ntl::wfp::terminating_decision redirect_browser_transport(
    browser_callout_state &state,
    const ntl::wfp::classify_event<Layer> &event) noexcept {
  const auto protocol = event.value(Layer::field::protocol).uint8();
  if (!protocol || *protocol != IPPROTO_TCP)
    return ntl::wfp::terminating_decision::block;
  return state.redirector->redirect(
      event, ntl::wfp::local_proxy_target::from_filter_context(
                 event.filter().context()));
}

template <class Layer>
void observe_browser_quic(
    browser_callout_state &state,
    const ntl::wfp::classify_event<Layer> &event) noexcept {
  state.telemetry->record(event);
}

class driver_runtime {
  struct state {
    state(std::shared_ptr<browser_driver::tcp_service> service,
          std::shared_ptr<browser_driver::http3_service> http3,
          std::shared_ptr<telemetry_state> telemetry,
          std::shared_ptr<ntl::wfp::transparent_udp_proxy_service> udp_proxy,
          ntl::device_endpoint<void> endpoint,
          ntl::wfp::callout_driver<> callouts,
          std::shared_ptr<browser_callout_state> wfp_state) noexcept
        : service(std::move(service)), http3(std::move(http3)),
          telemetry(std::move(telemetry)), udp_proxy(std::move(udp_proxy)),
          endpoint(std::move(endpoint)),
          callouts(std::move(callouts)), wfp_state(std::move(wfp_state)) {
      KeInitializeEvent(&close_complete, NotificationEvent, FALSE);
    }

    ~state() { (void)close(); }

    ntl::status close() noexcept {
      const LONG previous =
          ::InterlockedCompareExchange(&close_state, 1, 0);
      if (previous == 2)
        return ntl::status{close_status};
      if (previous == 1) {
        (void)KeWaitForSingleObject(&close_complete, Executive, KernelMode,
                                    FALSE, nullptr);
        return ntl::status{close_status};
      }

      // Stop and drain the public control plane before any state reachable by
      // an IOCTL handler begins shutting down.
      ntl::status result = endpoint.close();
      udp_proxy->close();
      const ntl::status callout_status = callouts.close();
      if (result.is_ok() && callout_status.is_err())
        result = callout_status;
      http3->shutdown();
      service->shutdown();

      close_status = static_cast<NTSTATUS>(result);
      KeMemoryBarrier();
      (void)::InterlockedExchange(&close_state, 2);
      KeSetEvent(&close_complete, IO_NO_INCREMENT, FALSE);
      return result;
    }

    std::shared_ptr<browser_driver::tcp_service> service;
    std::shared_ptr<browser_driver::http3_service> http3;
    std::shared_ptr<telemetry_state> telemetry;
    std::shared_ptr<ntl::wfp::transparent_udp_proxy_service> udp_proxy;
    ntl::device_endpoint<void> endpoint;
    ntl::wfp::callout_driver<> callouts;
    std::shared_ptr<browser_callout_state> wfp_state;
    KEVENT close_complete{};
    volatile LONG close_state = 0;
    NTSTATUS close_status = STATUS_SUCCESS;
  };

public:
  driver_runtime(std::shared_ptr<browser_driver::tcp_service> service,
                 std::shared_ptr<browser_driver::http3_service> http3,
                 std::shared_ptr<telemetry_state> telemetry,
                 std::shared_ptr<ntl::wfp::transparent_udp_proxy_service>
                     udp_proxy,
                 ntl::device_endpoint<void> endpoint,
                 ntl::wfp::callout_driver<> callouts,
                 std::shared_ptr<browser_callout_state> wfp_state)
      : state_(std::make_shared<state>(
            std::move(service), std::move(http3), std::move(telemetry),
            std::move(udp_proxy),
            std::move(endpoint), std::move(callouts),
            std::move(wfp_state))) {}

  ntl::status close() const noexcept { return state_->close(); }

private:
  std::shared_ptr<state> state_;
};

} // namespace

ntl::status ntl::main(ntl::driver &driver, const std::wstring &) {
  auto service = std::make_shared<browser_driver::tcp_service>();
  ntl::status status = service->start();
  if (!status.is_ok())
    return status;
  auto telemetry = std::make_shared<telemetry_state>();
  auto redirected =
      ntl::wfp::connect_redirector::try_create(contract::provider_key);
  if (!redirected) {
    service->shutdown();
    return redirected.status();
  }
  auto redirector =
      std::make_shared<ntl::wfp::connect_redirector>(std::move(*redirected));
  auto udp_created = ntl::wfp::transparent_udp_proxy_service::try_create(
      driver, contract::udp_proxy_keys, {}, telemetry);
  if (!udp_created) {
    service->shutdown();
    return udp_created.status();
  }
  auto udp_proxy =
      std::make_shared<ntl::wfp::transparent_udp_proxy_service>(
      std::move(*udp_created));
  auto http3 = std::make_shared<browser_driver::http3_service>(
      service, udp_proxy->share_routes());
  auto endpoint_result = ntl::try_create_device_endpoint<void>(
      driver, ntl::device_options()
                  .name(contract::device_name)
                  .type(FILE_DEVICE_UNKNOWN)
                  .exclusive(false)
                  .security_descriptor(L"D:P(A;;GA;;;SY)(A;;GA;;;BA)",
                                       contract::device_class_guid));
  if (!endpoint_result) {
    service->shutdown();
    return endpoint_result.status();
  }
  auto endpoint = std::move(*endpoint_result);
  status = endpoint.on_ioctl<contract::configure_identity_contract>(
      [service, http3](const contract::certificate_config &value) noexcept {
        browser_driver::fast_mutex_guard transaction(
            service->configuration_transaction_lock());
        auto configured = service->configure(value);
        if (!configured)
          return configured.status();
        const ntl::status http3_configured = http3->configure(value);
        if (!http3_configured.is_ok()) {
          http3->record_failure();
          service->rollback(std::move(*configured));
        }
        return http3_configured;
      });
  if (status.is_ok())
    status = endpoint.on_ioctl<
        contract::configure_origin_security_contract>(
        [service, http3](const contract::origin_security_config &value)
            noexcept {
          browser_driver::fast_mutex_guard transaction(
              service->configuration_transaction_lock());
          auto configured = service->configure_origin_security(value);
          if (!configured)
            return configured.status();
          const ntl::status http3_configured =
              http3->configure_origin_security(value);
          if (!http3_configured.is_ok())
            service->rollback(std::move(*configured));
          return http3_configured;
        });
  if (status.is_ok())
    status = endpoint.on_ioctl<
        contract::arm_origin_security_rollback_test_contract>(
        [service, http3]() noexcept {
          browser_driver::fast_mutex_guard transaction(
              service->configuration_transaction_lock());
          http3->arm_origin_security_rollback_test();
          return ntl::status::ok();
        });
  if (status.is_ok())
    status = endpoint.on_ioctl<contract::query_service_contract>(
        [service, http3, telemetry, udp_proxy](
            contract::service_info &output) noexcept {
          output = service->snapshot(telemetry->snapshot(
              udp_proxy->statistics()));
          http3->contribute(output);
          return ntl::status::ok();
        });
  if (status.is_ok())
    status = endpoint.on_ioctl<contract::read_inspection_contract>(
        [service](const contract::sequence_cursor &cursor,
                  contract::inspection_read_result &output) noexcept {
          service->read_inspection_after(cursor.after_sequence, output);
          return ntl::status::ok();
        });
  if (status.is_ok())
    status = endpoint.on_ioctl<contract::read_identity_request_contract>(
        [service](const contract::sequence_cursor &cursor,
                  contract::identity_request_read_result &output) noexcept {
          service->read_identity_request_after(cursor.after_sequence, output);
          return ntl::status::ok();
        });
  if (status.is_ok())
    status = endpoint.on_ioctl<contract::query_telemetry_contract>(
        [telemetry, udp_proxy](contract::quic_telemetry &output) noexcept {
          output = telemetry->snapshot(udp_proxy->statistics());
          return ntl::status::ok();
        });
  if (!status.is_ok()) {
    udp_proxy->close();
    service->shutdown();
    return status;
  }

  auto wfp_state = std::make_shared<browser_callout_state>(
      browser_callout_state{redirector, telemetry});
  ntl::wfp::callout_driver<> callouts(driver);
  status = callouts.add_terminating(
      contract::redirect_callout_key_v4, wfp_state,
      &redirect_browser_transport<contract::redirect_layer_v4>);
  if (status.is_ok())
    status = callouts.add_terminating(
        contract::redirect_callout_key_v6, wfp_state,
        &redirect_browser_transport<contract::redirect_layer_v6>);
  if (status.is_ok())
    status = callouts.add_inspection(
        contract::callout_key_v4, wfp_state,
        &observe_browser_quic<contract::quic_layer_v4>);
  if (status.is_ok())
    status = callouts.add_inspection(
        contract::callout_key_v6, wfp_state,
        &observe_browser_quic<contract::quic_layer_v6>);
  if (!status.is_ok()) {
    udp_proxy->close();
    service->shutdown();
    return status;
  }

  driver_runtime runtime(service, http3, telemetry, udp_proxy,
                         std::move(endpoint), callouts, wfp_state);
  driver.on_unload([runtime]() noexcept {
    const ntl::status result = runtime.close();
    NT_ASSERT(result.is_ok());
  });
  return ntl::status::ok();
}
