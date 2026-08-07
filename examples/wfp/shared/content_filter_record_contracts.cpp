#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "content_filter_record.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>

namespace record = crtsys::examples::wfp::content_filter;

int main() {
  constexpr std::array body{
      std::byte{'B'}, std::byte{'L'}, std::byte{'O'}, std::byte{'C'},
      std::byte{'K'}, std::byte{'M'}, std::byte{'E'},
  };
  std::array<std::byte, record::wire_header_size + body.size()> bytes{};
  if (!record::encode(bytes, record::classification::ordinary, 0x10203040,
                      body, body.size())
           .is_ok())
    return 1;

  auto parsed = record::parse(
      ntl::net::inspection::content_view(std::span<const std::byte>(bytes)),
      body.size());
  if (!parsed || parsed->category() != record::classification::ordinary ||
      parsed->rule_id() != 0x10203040 || parsed->body().size() != body.size())
    return 2;

  std::array<std::span<const std::byte>, 4> segments{
      std::span<const std::byte>(bytes).first(3),
      std::span<const std::byte>(bytes).subspan(3, 6),
      std::span<const std::byte>(bytes).subspan(9, 8),
      std::span<const std::byte>(bytes).subspan(17),
  };
  parsed = record::parse(
      ntl::net::inspection::content_view(
          ntl::net::scatter_view::from_segments(segments)),
      body.size());
  if (!parsed || parsed->body().size() != body.size())
    return 3;

  const auto original = bytes;
  bytes[0] = std::byte{'X'};
  if (record::parse(ntl::net::inspection::content_view(
                        std::span<const std::byte>(bytes)),
                    body.size()))
    return 4;
  bytes = original;
  bytes[5] = std::byte{0xff};
  if (record::parse(ntl::net::inspection::content_view(
                        std::span<const std::byte>(bytes)),
                    body.size()))
    return 5;
  bytes = original;
  bytes[12] = std::byte{0};
  bytes[13] = std::byte{0};
  bytes[14] = std::byte{0};
  bytes[15] = std::byte{1};
  if (record::parse(ntl::net::inspection::content_view(
                        std::span<const std::byte>(bytes)),
                    body.size()))
    return 6;
  bytes = original;
  if (record::parse(ntl::net::inspection::content_view(
                        std::span<const std::byte>(bytes)),
                    body.size() - 1))
    return 7;

  std::array<std::byte, record::wire_header_size - 1> truncated{};
  if (record::parse(ntl::net::inspection::content_view(
                        std::span<const std::byte>(truncated)),
                    body.size()))
    return 8;
  std::cout << "WFP content-filter record contracts passed: contiguous, "
               "scatter, malformed, truncated\n";
  return 0;
}
