#include <ntl/net/kernel/wsk_transport>

#include <type_traits>

static_assert(std::is_move_constructible_v<ntl::net::kernel::wsk_provider>);
static_assert(requires(ntl::net::kernel::wsk_provider &provider) {
  provider.open();
  provider.close();
});

// Emitting the inline open path gives the NuGet link contract a relocation
// for both WSK registration (netio.lib) and NPI_WSK_INTERFACE_ID (uuid.lib).
NTSTATUS crtsys_nuget_wsk_link_contract() {
  ntl::net::kernel::wsk_provider provider;
  const ntl::status opened = provider.open();
  provider.close();
  return static_cast<NTSTATUS>(opened);
}
