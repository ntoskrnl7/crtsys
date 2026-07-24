#include <fltKernel.h>

#include <ntl/flt/all>

#include <string_view>
#include <utility>

#include "xor_filter.hpp"

ntl::status ntl::flt::main(ntl::flt::driver &driver, std::wstring_view) {
  ntl::flt::registration callbacks;
  crtsys_minifilter_swap_buffers_sample::add_xor_callbacks(callbacks);
  return driver.start(std::move(callbacks));
}
