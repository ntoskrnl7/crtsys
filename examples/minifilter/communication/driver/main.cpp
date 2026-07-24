#include <fltKernel.h>

#include <ntl/flt/all>

#include <string_view>
#include <utility>

#include "communication.hpp"

ntl::status ntl::flt::main(ntl::flt::driver &driver, std::wstring_view) {
  const auto port_status =
      crtsys_minifilter_communication_sample::add_communication_port(driver);
  if (port_status.is_err())
    return port_status;

  ntl::flt::registration callbacks;
  return driver.start(std::move(callbacks));
}
