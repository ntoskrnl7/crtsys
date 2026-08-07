#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>

#include <ntl/net/http/inspection_policy>

namespace crtsys::wfp_sample::tls_inspection {

/** The only sample-specific data-path policy used by the proxy service. */
ntl::net::http::inspection_policy make_inspection_policy();

} // namespace crtsys::wfp_sample::tls_inspection
