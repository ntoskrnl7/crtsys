#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <ntl/net/inspection/content_decoder>
#include <ntl/net/websocket/permessage_deflate>

#include "http1_support.hpp"

namespace crtsys::wfp_sample::browser_https::http1_detail {

struct rewritten_browser_request {
  std::vector<std::byte> wire;
  std::string websocket_extensions;
  bool websocket_upgrade = false;
};

rewritten_browser_request rewrite_browser_request(
    std::span<const std::byte> wire);

void inspect_http1_response(
    parsed_http_response &parsed,
    std::string_view offered_websocket_extensions,
    const ntl::net::inspection::content_decoder_registry &decoders,
    ntl::net::websocket::permessage_deflate_parameters
        &websocket_compression);

} // namespace crtsys::wfp_sample::browser_https::http1_detail
