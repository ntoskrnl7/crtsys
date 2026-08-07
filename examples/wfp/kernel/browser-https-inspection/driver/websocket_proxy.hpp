#pragma once

#include "tcp_common.hpp"

namespace crtsys::wfp_kernel_browser_https::driver {

class browser_websocket_policy {
public:
  ntl::net::websocket::rewrite_result transform(
      std::uint32_t, ntl::net::http2::connection_direction,
      ntl::net::websocket::message &message) const {
    if (message.operation != ntl::net::websocket::opcode::text)
      return ntl::net::websocket::rewrite_result::unchanged();
    constexpr std::string_view blocked = "BLOCKME";
    if (std::search(
            message.payload.begin(), message.payload.end(),
            reinterpret_cast<const std::byte *>(blocked.data()),
            reinterpret_cast<const std::byte *>(blocked.data() +
                                                 blocked.size())) !=
        message.payload.end())
      return ntl::net::websocket::rewrite_result::block();
    constexpr std::string_view marker = " [ntl-kernel]";
    message.payload.insert(
        message.payload.end(),
        reinterpret_cast<const std::byte *>(marker.data()),
        reinterpret_cast<const std::byte *>(marker.data() + marker.size()));
    return ntl::net::websocket::rewrite_result::replace(
        std::move(message.payload));
  }
};

/** Example policy only; NTL owns framing, relay, cancellation, and drain. */
inline ntl::net::websocket::message_transform_pipeline
make_websocket_pipeline() {
  ntl::net::websocket::message_transform_pipeline pipeline(
      {.maximum_wire_frame_bytes = 1024 * 1024,
       .maximum_decoded_message_bytes = 4 * 1024 * 1024,
       .maximum_encoded_message_bytes = 4 * 1024 * 1024,
       .maximum_output_frame_payload = 64 * 1024,
       .validate_text_utf8 = true});
  pipeline.transform([](ntl::net::websocket::message &message) {
    if (message.operation != ntl::net::websocket::opcode::text)
      return ntl::net::websocket::rewrite_result::unchanged();
    constexpr std::string_view blocked = "BLOCKME";
    if (std::search(message.payload.begin(), message.payload.end(),
                    reinterpret_cast<const std::byte *>(blocked.data()),
                    reinterpret_cast<const std::byte *>(blocked.data() +
                                                         blocked.size())) !=
        message.payload.end())
      return ntl::net::websocket::rewrite_result::block();
    constexpr std::string_view marker = " [ntl-kernel]";
    message.payload.insert(
        message.payload.end(),
        reinterpret_cast<const std::byte *>(marker.data()),
        reinterpret_cast<const std::byte *>(marker.data() + marker.size()));
    return ntl::net::websocket::rewrite_result::replace(
        std::move(message.payload));
  });
  return pipeline;
}

inline std::array<std::byte, 4> websocket_mask() noexcept {
  std::array<std::byte, 4> result{};
  const NTSTATUS status = BCryptGenRandom(
      nullptr, reinterpret_cast<PUCHAR>(result.data()),
      static_cast<ULONG>(result.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
  if (!NT_SUCCESS(status)) {
    static std::atomic<std::uint32_t> sequence{1};
    LARGE_INTEGER system_time{};
    KeQuerySystemTimePrecise(&system_time);
    const std::uint32_t fallback =
        static_cast<std::uint32_t>(system_time.QuadPart) ^
        sequence.fetch_add(1, std::memory_order_relaxed);
    std::memcpy(result.data(), &fallback, result.size());
  }
  return result;
}

} // namespace crtsys::wfp_kernel_browser_https::driver
