#include <ntddk.h>

#include <memory>
#include <string>
#include <type_traits>

#include <ntl/device_endpoint>
#include <ntl/driver>
#include <ntl/ioctl>
#include <ntl/wfp/all>

#include "http3_inspection_contract.hpp"
#include "http3_service.hpp"

namespace {

namespace contract = wfp_kernel_http3_inspection;
using crtsys::wfp_kernel_http3::driver::http3_service;

template <class Layer>
ntl::wfp::terminating_decision permit_http3(
    http3_service &service,
    const ntl::wfp::classify_event<Layer> &event) noexcept {
  const auto protocol = event.value(Layer::field::protocol).uint8();
  if (!protocol || *protocol != IPPROTO_UDP)
    return ntl::wfp::terminating_decision::permit;
  if constexpr (std::is_same_v<Layer, contract::layer_v4>)
    service.record_wfp_v4();
  else
    service.record_wfp_v6();
  return ntl::wfp::terminating_decision::permit;
}

} // namespace

ntl::status ntl::main(ntl::driver &driver, const std::wstring &) {
  auto service = std::make_shared<http3_service>();
  auto options = ntl::device_options()
                     .name(contract::device_name)
                     .type(FILE_DEVICE_UNKNOWN)
                     .exclusive(false)
                     .security_descriptor(L"D:P(A;;GA;;;SY)(A;;GA;;;BA)",
                                          contract::device_class_guid);
  auto endpoint_result = ntl::try_create_device_endpoint<void>(driver, options);
  if (!endpoint_result)
    return endpoint_result.status();
  auto endpoint = std::move(*endpoint_result);

  ntl::status route = endpoint.on_ioctl<contract::configure_contract>(
      [service](const contract::certificate_config &configuration) noexcept {
        return service->configure(configuration);
      });
  if (route.is_ok())
    route = endpoint.on_ioctl<contract::query_contract>(
        [service](contract::service_info &output) noexcept {
          output = service->snapshot();
          return ntl::status::ok();
        });
  if (route.is_ok())
    route = endpoint.on_ioctl<contract::capture_contract>(
        [service](contract::inspection_record &output) noexcept {
          service->capture(output);
          return ntl::status::ok();
        });
  if (!route.is_ok())
    return route;

  ntl::wfp::callout_driver<> callouts(driver);
  ntl::status status = callouts.add_terminating(
      contract::callout_key_v4, service,
      [](http3_service &owned_service,
         const ntl::wfp::classify_event<contract::layer_v4> &event) noexcept {
        return permit_http3(owned_service, event);
      });
  if (status.is_ok())
    status = callouts.add_terminating(
        contract::callout_key_v6, service,
        [](http3_service &owned_service,
           const ntl::wfp::classify_event<contract::layer_v6> &event)
            noexcept { return permit_http3(owned_service, event); });
  if (!status.is_ok())
    return status;

  driver.on_unload([service, endpoint, callouts]() mutable {
    const ntl::status closed = endpoint.close();
    NT_ASSERT(closed.is_ok());
    const ntl::status reset = callouts.close();
    NT_ASSERT(reset.is_ok());
    service->shutdown();
  });
  return ntl::status::ok();
}
