/**
 * @file abi_win8_consumer.cpp
 * @brief Requires the Windows 8 view to match the Windows 7 ABI provider.
 */
#include <ntl/flt/driver>

#include <cstddef>

namespace crtsys_flt_abi_test {

template <std::size_t DriverSize, std::size_t RegistrationSize>
void require_layout() noexcept;

} // namespace crtsys_flt_abi_test

extern "C" void crtsys_flt_require_cross_target_abi() noexcept {
  crtsys_flt_abi_test::require_layout<sizeof(ntl::flt::driver),
                                      sizeof(ntl::flt::registration)>();
}
