#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include <ntl/net/inspection/content_decoder>
#include <ntl/net/http3/framing>
#include <ntl/net/buffer/scatter_view>
#include <ntl/status>

#include "browser_log.hpp"

namespace crtsys::wfp_sample::browser_https {

enum class http3_inspection_direction {
  browser_to_origin,
  origin_to_browser,
};

/**
 * @brief Browser policy above one decrypted HTTP/3 stream direction.
 *
 * A QUIC provider owns TLS 1.3, packet recovery, stream scheduling, and
 * forwarding. It gives each direction its own QPACK decoder and calls
 * consume_stream() with arbitrary plaintext splits and the QUIC FIN.
 */
class browser_http3_inspector {
public:
  browser_http3_inspector(
      http3_inspection_direction direction,
      ntl::net::http3::qpack_decoder &qpack,
      std::wstring server_name,
      const ntl::net::inspection::content_decoder_registry &decoders,
      browser_html_logger &logger);

  browser_http3_inspector(
      const browser_http3_inspector &) = delete;
  browser_http3_inspector &
  operator=(const browser_http3_inspector &) = delete;
  browser_http3_inspector(
      browser_http3_inspector &&) = delete;
  browser_http3_inspector &
  operator=(browser_http3_inspector &&) = delete;

  ~browser_http3_inspector();

  ntl::status consume_stream(
      std::uint64_t stream_id,
      ntl::net::scatter_view plaintext,
      bool final) noexcept;

  void reset() noexcept;
  unsigned last_status() const noexcept;
  std::optional<std::filesystem::path>
  html_path() const;

private:
  class implementation;
  std::unique_ptr<implementation> implementation_;
};

} // namespace crtsys::wfp_sample::browser_https
