#include <ntl/driver>
#include <ntl/net/kernel/all>
#include <ntl/net/kernel/msquic>
#include <ntl/wfp/all>

#include <span>
#include <type_traits>

static_assert(
    std::is_move_constructible_v<ntl::net::kernel::msquic_provider>);
static_assert(
    requires(ntl::net::kernel::msquic_configuration &configuration,
             std::span<const std::byte> thumbprint) {
      configuration.load_client_certificate(
          thumbprint, "MY", true, true);
    });

ntl::status ntl::main(ntl::driver &driver,
                      const std::wstring &registry_path) {
  (void)driver;
  (void)registry_path;
  return ntl::status::ok();
}
