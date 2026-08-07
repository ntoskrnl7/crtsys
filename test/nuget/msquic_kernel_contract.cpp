#include <ntl/net/kernel/msquic>

#include <type_traits>

static_assert(std::is_move_constructible_v<ntl::net::kernel::msquic_provider>,
              "the NuGet package must expose its pinned kernel MsQuic ABI");
static_assert(requires(ntl::net::kernel::msquic_configuration &configuration,
                       std::span<const std::byte> thumbprint) {
  configuration.load_client_certificate(thumbprint, "MY", true, true);
});

void crtsys_nuget_msquic_kernel_header_contract() {}
