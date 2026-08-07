#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include <guiddef.h>
#include <ntl/wfp/layers>

namespace wfp_stream_edit {

using flow_layer = ntl::wfp::layers::ale_flow_established_v4;
using stream_layer = ntl::wfp::layers::stream_v4;

inline constexpr wchar_t service_name[] =
    L"crtsys_wfp_stream_edit";
inline constexpr char token[] = "BLOCKME";
inline constexpr char replacement[] = "REDACT!";
static_assert(sizeof(token) == sizeof(replacement));
inline constexpr char oob_token[] = "OOBBLOCK";
inline constexpr char oob_replacement[] = "[OOB-REDACTED]";
inline constexpr std::size_t maximum_oob_bytes = 64 * 1024;
inline constexpr std::size_t maximum_oob_pending = 8;

class oob_pending_budget {
public:
  bool try_acquire() noexcept {
    std::size_t current = pending_.load(std::memory_order_acquire);
    while (current < maximum_oob_pending) {
      if (pending_.compare_exchange_weak(
              current, current + 1, std::memory_order_acq_rel,
              std::memory_order_acquire))
        return true;
    }
    rejections_.fetch_add(1, std::memory_order_relaxed);
    return false;
  }

  bool release() noexcept {
    std::size_t current = pending_.load(std::memory_order_acquire);
    while (current != 0) {
      if (pending_.compare_exchange_weak(
              current, current - 1, std::memory_order_acq_rel,
              std::memory_order_acquire))
        return true;
    }
    return false;
  }

  std::size_t pending() const noexcept {
    return pending_.load(std::memory_order_acquire);
  }

  std::uint64_t rejections() const noexcept {
    return rejections_.load(std::memory_order_relaxed);
  }

private:
  std::atomic<std::size_t> pending_{0};
  std::atomic<std::uint64_t> rejections_{0};
};

// {8DACA5AF-79C0-414C-BF6B-B2B1854674E3}
inline constexpr ntl::wfp::provider_key provider_key{GUID{
    0x8daca5af,
    0x79c0,
    0x414c,
    {0xbf, 0x6b, 0xb2, 0xb1, 0x85, 0x46, 0x74, 0xe3}}};

// {D015DFF3-7FE2-49B5-8E2D-37E3E954DC1C}
inline constexpr ntl::wfp::sublayer_key sublayer_key{GUID{
    0xd015dff3,
    0x7fe2,
    0x49b5,
    {0x8e, 0x2d, 0x37, 0xe3, 0xe9, 0x54, 0xdc, 0x1c}}};

// {1732A2A7-7CCD-4467-8DD9-B092148BF366}
inline constexpr ntl::wfp::inspection_callout_key<flow_layer> flow_callout_key{GUID{
    0x1732a2a7,
    0x7ccd,
    0x4467,
    {0x8d, 0xd9, 0xb0, 0x92, 0x14, 0x8b, 0xf3, 0x66}}};

// {1770DE83-00AE-4FB0-AD5A-72D192742829}
inline constexpr ntl::wfp::stream_callout_key<stream_layer>
    stream_callout_key{GUID{
        0x1770de83,
        0x00ae,
        0x4fb0,
        {0xad, 0x5a, 0x72, 0xd1, 0x92, 0x74, 0x28, 0x29}}};

// {B6875B57-2C0C-46B7-BB34-383BBD1C362E}
inline constexpr ntl::wfp::filter_key<flow_layer> flow_filter_key{GUID{
    0xb6875b57,
    0x2c0c,
    0x46b7,
    {0xbb, 0x34, 0x38, 0x3b, 0xbd, 0x1c, 0x36, 0x2e}}};

// {50AF185D-090C-41A1-91B0-0E730DB76CF7}
inline constexpr ntl::wfp::filter_key<stream_layer>
    stream_filter_key{GUID{
        0x50af185d,
        0x090c,
        0x41a1,
        {0x91, 0xb0, 0x0e, 0x73, 0x0d, 0xb7, 0x6c, 0xf7}}};

} // namespace wfp_stream_edit
