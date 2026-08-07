#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <array>
#include <iostream>
#include <span>
#include <stdexcept>
#include <vector>

#include <ntl/net/buffer/scatter_view>
#include <ntl/net/http/datagram>
#include <ntl/net/http3/webtransport>

#include "webtransport_capsule_stream.hpp"

namespace {

using crtsys::wfp_kernel_browser_https::driver::
    webtransport_capsule_stream;

std::vector<std::byte> capsule(std::uint64_t type,
                               std::span<const std::byte> payload = {}) {
  auto encoded = ntl::net::http::encode_capsule(
      type, payload,
      {.maximum_payload_size =
           webtransport_capsule_stream::maximum_payload_size});
  if (!encoded)
    throw std::runtime_error("test capsule encoding failed");
  return std::move(*encoded);
}

bool split_and_coalesced_contract(std::uint64_t &observed_capsules) {
  const auto first = capsule(
      ntl::net::http3::webtransport::wt_drain_session);
  const std::array<std::byte, 2> unknown_payload{
      std::byte{0x11}, std::byte{0x22}};
  const auto second = capsule(0x21, unknown_payload);
  const std::array<std::byte, 4> close_payload{};
  const auto third = capsule(
      ntl::net::http3::webtransport::wt_close_session, close_payload);
  std::vector<std::byte> wire;
  wire.insert(wire.end(), first.begin(), first.end());
  wire.insert(wire.end(), second.begin(), second.end());
  wire.insert(wire.end(), third.begin(), third.end());

  webtransport_capsule_stream stream;
  std::uint64_t consumed = 0;
  const auto inspect = [&consumed](
                           const ntl::net::http::capsule_view &value) {
    auto parsed = ntl::net::http3::webtransport::inspect_capsule(value);
    if (!parsed)
      return parsed.status();
    ++consumed;
    return ntl::status::ok();
  };
  const std::size_t split1 = 1;
  const std::size_t split2 = first.size() + second.size() + 1;
  if (!stream
           .consume(ntl::net::scatter_view::from_contiguous(
                        std::span(wire).first(split1)),
                    inspect)
           .is_ok() ||
      consumed != 0 || stream.pending_bytes() != split1)
    return false;
  if (!stream
           .consume(ntl::net::scatter_view::from_contiguous(
                        std::span(wire).subspan(split1, split2 - split1)),
                    inspect)
           .is_ok() ||
      consumed != 2 || stream.pending_bytes() != 1)
    return false;
  if (!stream
           .consume(ntl::net::scatter_view::from_contiguous(
                        std::span(wire).subspan(split2)),
                    inspect)
           .is_ok() ||
      consumed != 3 || stream.pending_bytes() != 0 ||
      stream.capsule_count() != 3 || !stream.finish().is_ok())
    return false;
  observed_capsules = stream.capsule_count();

  webtransport_capsule_stream truncated;
  if (!truncated
           .consume(ntl::net::scatter_view::from_contiguous(
                        std::span(third).first(third.size() - 1)),
                    inspect)
           .is_ok())
    return false;
  return static_cast<NTSTATUS>(truncated.finish()) == STATUS_END_OF_FILE;
}

bool boundary_and_overflow_contract() {
  webtransport_capsule_stream stream;
  std::uint64_t consumed = 0;
  const auto inspect = [&consumed](const ntl::net::http::capsule_view &) {
    ++consumed;
    return ntl::status::ok();
  };
  const std::span<const std::byte> empty;
  if (!stream
           .consume(ntl::net::scatter_view::from_contiguous(empty), inspect)
           .is_ok() ||
      consumed != 0)
    return false;

  std::vector<std::byte> maximum_payload(
      webtransport_capsule_stream::maximum_payload_size, std::byte{0x5a});
  const auto maximum = capsule(0x21, maximum_payload);
  if (!stream
           .consume(ntl::net::scatter_view::from_contiguous(maximum), inspect)
           .is_ok() ||
      consumed != 1 || !stream.finish().is_ok())
    return false;

  std::vector<std::byte> oversized_header;
  if (!ntl::net::http3::append_quic_varint(oversized_header, 0x21).is_ok() ||
      !ntl::net::http3::append_quic_varint(
           oversized_header,
           webtransport_capsule_stream::maximum_payload_size + 1)
           .is_ok())
    return false;
  webtransport_capsule_stream oversized;
  const auto oversized_status = oversized.consume(
      ntl::net::scatter_view::from_contiguous(oversized_header), inspect);
  if (static_cast<NTSTATUS>(oversized_status) != STATUS_BUFFER_OVERFLOW)
    return false;

  const std::array<std::byte, 1> truncated_varint{std::byte{0xff}};
  webtransport_capsule_stream truncated_header;
  if (!truncated_header
           .consume(ntl::net::scatter_view::from_contiguous(
                        truncated_varint),
                    inspect)
           .is_ok() ||
      static_cast<NTSTATUS>(truncated_header.finish()) != STATUS_END_OF_FILE)
    return false;

  const auto small = capsule(
      ntl::net::http3::webtransport::wt_drain_session);
  std::vector<std::byte> coalesced;
  while (coalesced.size() <=
         webtransport_capsule_stream::maximum_buffered_bytes) {
    coalesced.insert(coalesced.end(), small.begin(), small.end());
  }
  webtransport_capsule_stream bounded;
  std::uint64_t bounded_consumed = 0;
  const auto bounded_status = bounded.consume(
      ntl::net::scatter_view::from_contiguous(coalesced),
      [&bounded_consumed](const ntl::net::http::capsule_view &) {
        ++bounded_consumed;
        return ntl::status::ok();
      });
  return static_cast<NTSTATUS>(bounded_status) == STATUS_BUFFER_OVERFLOW &&
         bounded_consumed == 0;
}

} // namespace

int main() {
  try {
    std::uint64_t capsule_count = 0;
    const bool stream = split_and_coalesced_contract(capsule_count);
    const bool boundaries = boundary_and_overflow_contract();
    if (!stream || !boundaries) {
      std::cerr << "WebTransport Capsule stream contract failed\n";
      return 1;
    }
    std::cout << "WebTransport Capsule stream PASS: capsules="
              << capsule_count << " split=" << (stream ? "pass" : "fail")
              << " coalesced=" << (stream ? "pass" : "fail")
              << " final_tail=" << (stream ? "pass" : "fail")
              << " empty=" << (boundaries ? "pass" : "fail")
              << " maximum_payload=" << (boundaries ? "pass" : "fail")
              << " oversized_length=" << (boundaries ? "pass" : "fail")
              << " truncated_varint=" << (boundaries ? "pass" : "fail")
              << " bounded_overflow=" << (boundaries ? "pass" : "fail")
              << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "WebTransport Capsule stream failed: " << error.what()
              << '\n';
    return 1;
  }
}
