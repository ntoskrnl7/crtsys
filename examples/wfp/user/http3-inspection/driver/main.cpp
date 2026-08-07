#include <ntddk.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <type_traits>

#include <ntl/device_endpoint>
#include <ntl/driver>
#include <ntl/except>
#include <ntl/ioctl>
#include <ntl/wfp/all>

#include "http3_wfp_gate_contract.hpp"

namespace {

namespace contract = wfp_user_http3_inspection;
using query_telemetry =
    ntl::ioctl_from_contract<contract::query_telemetry_contract>;

class gate_telemetry_state {
public:
  template <class Layer>
  bool record(const ntl::wfp::classify_event<Layer> &event) noexcept {
    static_assert(std::is_same_v<Layer, contract::layer_v4> ||
                  std::is_same_v<Layer, contract::layer_v6>);
    auto &state = [&]() noexcept -> layer_state & {
      if constexpr (std::is_same_v<Layer, contract::layer_v4>)
        return ipv4_;
      else
        return ipv6_;
    }();

    ::InterlockedIncrement64(&state.classify_hits);
    if (event.action_write_available())
      ::InterlockedIncrement64(&state.action_write_available);
    else
      ::InterlockedIncrement64(&state.action_write_missing);

    const auto protocol = event.value(Layer::field::protocol).uint8();
    if (!protocol || *protocol != IPPROTO_UDP) {
      ::InterlockedIncrement64(&state.invalid_protocol);
      return false;
    }

    exchange64(state.last_filter_id, event.filter().id());
    exchange32(state.last_filter_flags, event.filter().flags());
    exchange32(state.last_protocol, *protocol);
    const auto process_id = event.metadata().process_id();
    exchange64(state.last_process_id, process_id ? *process_id : 0);
    const auto remote_port =
        event.value(Layer::field::remote_port).uint16();
    exchange32(state.last_remote_port, remote_port ? *remote_port : 0);

    const auto *const application =
        event.value(Layer::field::app_id).byte_blob();
    if (application && application->data && application->size != 0) {
      exchange64(
          state.last_application_id_hash,
          contract::hash_application_id(
              application->data, application->size));
      exchange32(state.last_application_id_size, application->size);
    } else {
      exchange64(state.last_application_id_hash, 0);
      exchange32(state.last_application_id_size, 0);
    }

    if constexpr (std::is_same_v<Layer, contract::layer_v4>) {
      const auto remote_address =
          event.value(Layer::field::remote_address).uint32();
      exchange32(
          state.last_remote_address_v4,
          remote_address ? *remote_address : 0);
      exchange32(state.address_family, AF_INET);
    } else {
      const auto *const remote_address =
          event.value(Layer::field::remote_address).byte_array16();
      for (std::size_t index = 0; index != 4; ++index) {
        std::uint32_t word = 0;
        if (remote_address) {
          std::memcpy(
              &word,
              remote_address->byteArray16 + index * sizeof(word),
              sizeof(word));
        }
        exchange32(state.last_remote_address_v6[index], word);
      }
      exchange32(state.address_family, AF_INET6);
    }

    ::InterlockedIncrement64(&state.permit_decisions);
    return true;
  }

  contract::gate_telemetry snapshot() const noexcept {
    contract::gate_telemetry result{};
    result.version = contract::telemetry_version;
    result.size = sizeof(result);
    copy(ipv4_, result.ipv4);
    copy(ipv6_, result.ipv6);
    return result;
  }

private:
  struct alignas(8) layer_state {
    volatile LONG64 classify_hits = 0;
    volatile LONG64 permit_decisions = 0;
    volatile LONG64 invalid_protocol = 0;
    volatile LONG64 action_write_available = 0;
    volatile LONG64 action_write_missing = 0;
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
      volatile LONG64 &target, std::uint64_t value) noexcept {
    (void)::InterlockedExchange64(&target, static_cast<LONG64>(value));
  }

  static void exchange32(
      volatile LONG &target, std::uint32_t value) noexcept {
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

  static void copy(
      const layer_state &source,
      contract::layer_telemetry &target) noexcept {
    target.classify_hits = read64(source.classify_hits);
    target.permit_decisions = read64(source.permit_decisions);
    target.invalid_protocol = read64(source.invalid_protocol);
    target.action_write_available =
        read64(source.action_write_available);
    target.action_write_missing = read64(source.action_write_missing);
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
    target.last_remote_port = static_cast<std::uint16_t>(
        read32(source.last_remote_port));
    target.last_filter_flags = static_cast<std::uint16_t>(
        read32(source.last_filter_flags));
    target.last_protocol =
        static_cast<std::uint8_t>(read32(source.last_protocol));
    target.address_family =
        static_cast<std::uint8_t>(read32(source.address_family));
  }

  layer_state ipv4_;
  layer_state ipv6_;
};

template <class Layer>
ntl::wfp::terminating_decision permit_selected_http3(
    gate_telemetry_state &telemetry,
    const ntl::wfp::classify_event<Layer> &event) noexcept {
  if (!telemetry.record(event))
    return ntl::wfp::terminating_decision::block;
  return ntl::wfp::terminating_decision::permit;
}

} // namespace

ntl::status ntl::main(ntl::driver &driver, const std::wstring &) {
  auto telemetry = std::make_shared<gate_telemetry_state>();
  auto endpoint_result = ntl::try_create_device_endpoint<void>(
      driver,
      ntl::device_options()
          .name(contract::device_name)
          .type(FILE_DEVICE_UNKNOWN)
          .exclusive(false)
          .security_descriptor(
              L"D:P(A;;GA;;;SY)(A;;GR;;;BA)",
              contract::device_class_guid));
  if (!endpoint_result)
    return endpoint_result.status();
  auto endpoint = std::move(*endpoint_result);
  const ntl::status query_route =
      endpoint.on_ioctl<contract::query_telemetry_contract>(
          [telemetry](contract::gate_telemetry &output) noexcept {
            output = telemetry->snapshot();
            return ntl::status::ok();
          });
  if (!query_route.is_ok())
    return query_route;

  ntl::wfp::callout_driver<> callouts(driver);
  ntl::status status = callouts.add_terminating(
      contract::callout_key_v4, telemetry,
      [](gate_telemetry_state &owned_telemetry,
         const ntl::wfp::classify_event<contract::layer_v4> &event) noexcept {
        return permit_selected_http3(owned_telemetry, event);
      });
  if (status.is_ok())
    status = callouts.add_terminating(
        contract::callout_key_v6, telemetry,
        [](gate_telemetry_state &owned_telemetry,
           const ntl::wfp::classify_event<contract::layer_v6> &event)
            noexcept {
          return permit_selected_http3(owned_telemetry, event);
        });
  if (!status.is_ok())
    return status;

  driver.on_unload([callouts, endpoint] {
    const ntl::status closed = endpoint.close();
    NT_ASSERT(closed.is_ok());
    const ntl::status reset = callouts.close();
    NT_ASSERT(reset.is_ok());
  });
  return ntl::status::ok();
}
