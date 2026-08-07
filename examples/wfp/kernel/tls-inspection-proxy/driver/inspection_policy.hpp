#pragma once

#if !defined(_KERNEL_MODE) && !defined(_KERNEL32_)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>
#endif

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include <ntl/net/http/inspection_policy>

namespace crtsys::wfp_kernel_tls {

inline constexpr std::array<std::string_view, 2>
    inbound_application_protocols{"h2", "http/1.1"};
inline constexpr std::array<std::string_view, 1>
    http1_application_protocol{"http/1.1"};
inline constexpr std::array<std::string_view, 1>
    http2_application_protocol{"h2"};

bool supports_application_protocol(std::string_view value) noexcept;

/** Same semantic policy used by the paired user-runtime example. */
ntl::net::http::inspection_policy make_inspection_policy();

/** Runtime-adapter hook for the sample's deliberately small custom sessions. */
ntl::net::inspection::verdict evaluate_request(
    const ntl::net::http::inspection_policy &policy,
    ntl::net::http::protocol protocol, std::uint64_t stream_id,
    std::uint64_t exchange_id,
    const ntl::net::http::inspection_session_metadata &session,
    const ntl::net::http::request_message &request) noexcept;

} // namespace crtsys::wfp_kernel_tls
