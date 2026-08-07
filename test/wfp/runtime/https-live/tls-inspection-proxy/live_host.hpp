#pragma once

#include <string_view>

namespace crtsys::wfp_sample::tls_inspection {

int run_live_host_sample(std::wstring_view host,
                         bool allow_unavailable_revocation);

} // namespace crtsys::wfp_sample::tls_inspection
