#include "../shared/runtime_test.hpp"

#include <ntl/flt/communication_client>

#include <iostream>

#ifndef CRTSYS_SECURITY_PROBE_EXPECT_ALLOWED
#error "The security probe must declare its expected access result."
#endif

int wmain() {
  bool connected = false;
  try {
    auto client = ntl::flt::communication_client::connect(
        crtsys_flt_runtime_test::security_administrators_port_name);
    (void)client;
    connected = true;
  } catch (const std::exception &) {
  }

#if CRTSYS_SECURITY_PROBE_EXPECT_ALLOWED
  if (!connected) {
    std::cerr << "security probe expected Administrators access\n";
    return 1;
  }
  std::cout << "security probe PASS: Administrators access allowed\n";
#else
  if (connected) {
    std::cerr << "security probe expected non-Administrators denial\n";
    return 1;
  }
  std::cout << "security probe PASS: non-Administrators denied\n";
#endif
  return 0;
}
