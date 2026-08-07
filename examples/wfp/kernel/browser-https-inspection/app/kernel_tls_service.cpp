#include "kernel_tls_service.hpp"

#include <algorithm>
#include <stdexcept>
#include <system_error>

namespace crtsys::wfp_kernel_browser_https {
namespace {

namespace contract = wfp_kernel_browser_https_inspection;

template <class Output>
Output query(HANDLE device, unsigned long code, const void *input = nullptr,
             DWORD input_size = 0) {
  Output result{};
  DWORD bytes = 0;
  if (!::DeviceIoControl(device, code, const_cast<void *>(input), input_size,
                         &result, sizeof(result), &bytes, nullptr) ||
      bytes != sizeof(result))
    throw std::system_error(::GetLastError(), std::system_category(),
                            "query kernel browser service");
  return result;
}

} // namespace

device_handle::device_handle()
    : value_(::CreateFileW(contract::user_device_path,
                           GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, 0, nullptr)) {
  if (value_ == INVALID_HANDLE_VALUE)
    throw std::system_error(::GetLastError(), std::system_category(),
                            "open kernel browser inspection device");
}

device_handle::~device_handle() {
  if (value_ != INVALID_HANDLE_VALUE)
    (void)::CloseHandle(value_);
}

contract::service_info query_service(HANDLE device) {
  auto result =
      query<contract::service_info>(device, contract::query_service_ioctl);
  if (!contract::valid_service_info_abi(result))
    throw std::runtime_error("kernel browser service ABI mismatch");
  return result;
}

contract::inspection_read_result read_inspection(
    HANDLE device, std::uint64_t after_sequence) {
  const contract::sequence_cursor input{after_sequence};
  return query<contract::inspection_read_result>(
      device, contract::read_inspection_ioctl, &input, sizeof(input));
}

contract::identity_request_read_result read_identity_request(
    HANDLE device, std::uint64_t after_sequence) {
  const contract::sequence_cursor input{after_sequence};
  return query<contract::identity_request_read_result>(
      device, contract::read_identity_request_ioctl, &input, sizeof(input));
}

contract::quic_telemetry query_quic_telemetry(HANDLE device) {
  auto result =
      query<contract::quic_telemetry>(device, contract::query_telemetry_ioctl);
  if (result.version != contract::telemetry_version ||
      result.size != sizeof(result))
    throw std::runtime_error("kernel browser telemetry ABI mismatch");
  return result;
}

void configure_identity(HANDLE device,
                        const contract::certificate_config &identity) {
  auto input = identity;
  DWORD bytes = 0;
  if (!::DeviceIoControl(device, contract::configure_identity_ioctl,
                         &input, sizeof(input), nullptr, 0, &bytes, nullptr))
    throw std::system_error(::GetLastError(), std::system_category(),
                            "configure kernel browser identity");
}

void configure_origin_security(
    HANDLE device, std::string_view server_name,
    std::span<const std::byte> client_sha1_thumbprint,
    std::span<const std::byte> origin_leaf_der) {
  if (client_sha1_thumbprint.size() !=
          contract::certificate_thumbprint_size ||
      server_name.empty() ||
      server_name.size() > contract::maximum_server_name_size ||
      origin_leaf_der.empty() ||
      origin_leaf_der.size() > contract::maximum_certificate_der_size)
    throw std::invalid_argument("invalid kernel origin security material");
  contract::origin_security_config input{};
  input.action = contract::origin_security_action::install;
  input.server_name_size = static_cast<std::uint32_t>(server_name.size());
  std::copy(server_name.begin(), server_name.end(), input.server_name.begin());
  std::copy(client_sha1_thumbprint.begin(), client_sha1_thumbprint.end(),
            input.client_sha1_thumbprint.begin());
  input.origin_leaf_der_size =
      static_cast<std::uint32_t>(origin_leaf_der.size());
  std::copy(origin_leaf_der.begin(), origin_leaf_der.end(),
            input.origin_leaf_der.begin());
  DWORD bytes = 0;
  if (!::DeviceIoControl(device, contract::configure_origin_security_ioctl,
                         &input, sizeof(input), nullptr, 0, &bytes, nullptr))
    throw std::system_error(::GetLastError(), std::system_category(),
                            "configure kernel browser origin security");
}

void remove_origin_security(HANDLE device, std::string_view server_name) {
  if (server_name.empty() ||
      server_name.size() > contract::maximum_server_name_size)
    throw std::invalid_argument("invalid kernel origin security host");
  contract::origin_security_config input{};
  input.action = contract::origin_security_action::remove;
  input.server_name_size = static_cast<std::uint32_t>(server_name.size());
  std::copy(server_name.begin(), server_name.end(), input.server_name.begin());
  DWORD bytes = 0;
  if (!::DeviceIoControl(device, contract::configure_origin_security_ioctl,
                         &input, sizeof(input), nullptr, 0, &bytes, nullptr))
    throw std::system_error(::GetLastError(), std::system_category(),
                            "remove kernel browser origin security");
}

} // namespace crtsys::wfp_kernel_browser_https
