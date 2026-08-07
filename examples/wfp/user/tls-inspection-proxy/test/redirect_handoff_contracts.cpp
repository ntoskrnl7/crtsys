#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>

#include <ntl/wfp/connect_redirect>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <type_traits>

namespace {

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

void preserves_opaque_process_identity() {
  using ntl::wfp::detail::connect_redirect_context;
  connect_redirect_context driver_record{};
  driver_record.process_id = 0x12345678u;
  driver_record.flow_id = 0x1122334455667788ull;
  constexpr std::array<std::uint8_t, 11> application_id{
      0x5c, 0x00, 0x44, 0x00, 0x65, 0x00,
      0x76, 0x00, 0x00, 0xfe, 0x7f};
  driver_record.application_id_size =
      static_cast<std::uint16_t>(application_id.size());
  std::memcpy(driver_record.application_id.data(), application_id.data(),
              application_id.size());

  std::array<std::byte, sizeof(connect_redirect_context)> wire{};
  std::memcpy(wire.data(), &driver_record, sizeof(driver_record));
  connect_redirect_context service_record{};
  std::memcpy(&service_record, wire.data(), wire.size());

  require(ntl::wfp::detail::valid_connect_redirect_context(service_record),
          "service rejected the versioned redirect identity record");
  require(service_record.process_id == driver_record.process_id &&
              service_record.flow_id == driver_record.flow_id,
          "redirect handoff changed the process or flow identity");
  require(service_record.application_id_size == application_id.size() &&
              std::memcmp(service_record.application_id.data(),
                          application_id.data(), application_id.size()) == 0,
          "redirect handoff did not preserve opaque ALE_APP_ID bytes");
}

void rejects_incomplete_or_oversized_identity() {
  ntl::wfp::detail::connect_redirect_context record{};
  require(!ntl::wfp::detail::valid_connect_redirect_context(record),
          "zero process/application identity was not rejected");
  record.process_id = 1;
  record.application_id_size = static_cast<std::uint16_t>(
      record.application_id.size() + 1);
  require(!ntl::wfp::detail::valid_connect_redirect_context(record),
          "oversized ALE_APP_ID was not rejected fail closed");
}

} // namespace

int main() {
  try {
    static_assert(std::is_trivially_copyable_v<
                  ntl::wfp::detail::connect_redirect_context>);
    static_assert(std::is_standard_layout_v<
                  ntl::wfp::detail::connect_redirect_context>);
    static_assert(sizeof(ntl::wfp::detail::connect_redirect_context) == 2336);
    preserves_opaque_process_identity();
    rejects_incomplete_or_oversized_identity();
    std::cout << "redirect handoff contracts passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "redirect handoff contracts failed: " << error.what()
              << '\n';
    return 1;
  }
}
