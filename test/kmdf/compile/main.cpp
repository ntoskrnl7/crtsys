#include <ntddk.h>

#include <ntl/kmdf/all>

namespace {

constexpr auto unload =
    +[](ntl::kmdf::driver) noexcept {};

} // namespace

ntl::status ntl::kmdf::main(driver_builder &builder,
                            const std::wstring &registry_path) {
  UNREFERENCED_PARAMETER(registry_path);

  kmdf::driver_config config;
  config.non_pnp().on_unload<unload>();
  const auto driver = builder.try_create(config);
  return driver ? ntl::status::ok() : driver.status();
}
