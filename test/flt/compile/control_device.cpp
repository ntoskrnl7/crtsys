#include <ntl/flt/driver>

namespace {

ntl::status configure_compile_only_device(
    ntl::device_endpoint<void> &endpoint) noexcept {
  endpoint.on_create(
      [](ntl::irp &request) noexcept { request.succeed(FILE_OPENED); });
  return STATUS_SUCCESS;
}

} // namespace

ntl::status compile_control_device_registration(
    ntl::flt::driver &driver) noexcept {
  auto options = ntl::device_options()
                     .name(L"CrtSysFltCompileOnlyCdo")
                     .type(FILE_DEVICE_UNKNOWN);
  return driver.add_control_device<void>(
      std::move(options), configure_compile_only_device);
}
