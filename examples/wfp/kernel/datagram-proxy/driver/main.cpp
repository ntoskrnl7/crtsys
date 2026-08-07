#include <ntddk.h>

#include <memory>

#include <ntl/device_endpoint>
#include <ntl/driver>
#include <ntl/ioctl>
#include <ntl/wfp/all>

#include "datagram_proxy_contract.hpp"

ntl::status ntl::main(ntl::driver &driver, const std::wstring &) {
  auto created = ntl::wfp::transparent_udp_proxy_service::try_create(
      driver, wfp_datagram_proxy::proxy_keys);
  if (!created)
    return created.status();
  auto proxy = std::make_shared<ntl::wfp::transparent_udp_proxy_service>(
      std::move(*created));

  auto endpoint_result = ntl::try_create_device_endpoint<void>(
      driver,
      ntl::device_options()
          .name(wfp_datagram_proxy::device_name)
          .type(FILE_DEVICE_UNKNOWN)
          .exclusive(false)
          .security_descriptor(L"D:P(A;;GA;;;SY)(A;;GA;;;BA)",
                               wfp_datagram_proxy::device_class_guid));
  if (!endpoint_result)
    return endpoint_result.status();
  auto endpoint = std::move(*endpoint_result);
  const ntl::status query_route = endpoint.on_ioctl<
      wfp_datagram_proxy::query_statistics_contract>(
      [proxy](wfp_datagram_proxy::proxy_statistics &output) noexcept {
        const auto value = proxy->statistics();
        output = {value.outbound_packets, value.inbound_packets,
                  value.mapping_updates, value.mapping_misses,
                  value.injection_failures, value.quota_rejections,
                  value.asynchronous_injection_failures,
                  value.last_asynchronous_injection_status, 0};
        return ntl::status::ok();
      });
  if (!query_route.is_ok())
    return query_route;

  driver.on_unload(
      [proxy, endpoint = std::move(endpoint)]() mutable noexcept {
        const ntl::status endpoint_result = endpoint.close();
        NT_ASSERT(endpoint_result.is_ok());
        proxy->close();
      });
  return ntl::status::ok();
}
