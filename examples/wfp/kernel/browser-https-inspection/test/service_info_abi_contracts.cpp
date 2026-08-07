#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winioctl.h>

#include "browser_https_inspection_contract.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <type_traits>

namespace contract = wfp_kernel_browser_https_inspection;

static_assert(std::is_standard_layout_v<contract::service_info>);
static_assert(std::is_trivially_copyable_v<contract::service_info>);
static_assert(offsetof(contract::service_info, version) == 0);
static_assert(offsetof(contract::service_info, size) == sizeof(std::uint32_t));
static_assert(sizeof(contract::service_info) <=
              (std::numeric_limits<std::uint32_t>::max)());

int main() {
  contract::service_info value{};
  value.version = contract::service_info_version;
  value.size = static_cast<std::uint32_t>(sizeof(value));
  value.workspace_lifetime_passed = 1;
  if (!contract::valid_service_info_abi(value))
    return 1;

  ++value.version;
  if (contract::valid_service_info_abi(value))
    return 2;
  value.version = contract::service_info_version;
  --value.size;
  if (contract::valid_service_info_abi(value))
    return 3;

  std::cout << "Kernel browser service ABI contract PASS: version="
            << contract::service_info_version
            << " size=" << sizeof(contract::service_info) << '\n';
  return 0;
}
