/**
 * @file abi_win7_provider.cpp
 * @brief Defines the Windows 7 side of the prebuilt minifilter ABI check.
 */
#include <ntl/flt/driver>

#include <cstddef>

namespace crtsys_flt_abi_test {

template <std::size_t DriverSize, std::size_t RegistrationSize>
void require_layout() noexcept {}

template void
require_layout<sizeof(ntl::flt::driver),
               sizeof(ntl::flt::registration)>() noexcept;

} // namespace crtsys_flt_abi_test
