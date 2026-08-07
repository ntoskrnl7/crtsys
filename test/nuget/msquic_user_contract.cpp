#include <ntl/net/http3/msquic_backend>

static_assert(ntl::net::http3::msquic_backend::capabilities.available,
              "the NuGet package must expose its pinned user MsQuic ABI");

void crtsys_nuget_msquic_user_header_contract() {}
