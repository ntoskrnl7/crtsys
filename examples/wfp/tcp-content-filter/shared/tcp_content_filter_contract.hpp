#pragma once

#include <cstdint>
#include <vector>

#include <guiddef.h>
#include <ntl/rpc/common>
#include <ntl/wfp/layers>

struct wfp_tcp_content_request {
  std::uint64_t id = 0;
  std::uint16_t source_port = 0;
  std::uint16_t destination_port = 0;
  std::uint32_t content_offset = 0;
  std::uint32_t content_size = 0;
  std::vector<std::uint8_t> frame;

  friend zpp::serializer::access;
  template <typename Archive, typename Self>
  static void serialize(Archive &archive, Self &self) {
    archive(self.id, self.source_port, self.destination_port,
            self.content_offset, self.content_size, self.frame);
  }
};

struct wfp_tcp_content_filter_stats {
  std::uint64_t queued = 0;
  std::uint64_t permitted = 0;
  std::uint64_t blocked = 0;
  std::uint64_t timed_out = 0;
  std::uint64_t failed = 0;

  friend zpp::serializer::access;
  template <typename Archive, typename Self>
  static void serialize(Archive &archive, Self &self) {
    archive(self.queued, self.permitted, self.blocked, self.timed_out,
            self.failed);
  }
};

namespace wfp_tcp_content_filter {

using flow_layer = ntl::wfp::layers::ale_flow_established_v4;
using stream_layer = ntl::wfp::layers::stream_v4;

inline constexpr wchar_t service_name[] =
    L"crtsys_wfp_tcp_content_filter";
inline constexpr wchar_t endpoint_name[] =
    L"crtsys_wfp_tcp_content_filter";

constexpr std::uint32_t contract_version = 1;
constexpr std::size_t maximum_content_size = 4096;

// This is the application protocol selected by this sample. It is not a TCP
// header or TCP standard. Replace the framer for a real application protocol.
constexpr std::size_t sample_u32_be_prefix_size = 4;
constexpr std::size_t maximum_frame_size =
    sample_u32_be_prefix_size + maximum_content_size;
constexpr std::size_t maximum_pending_requests = 64;
constexpr std::uint64_t verdict_timeout_100ns =
    2ull * 10ull * 1000ull * 1000ull;

enum class wire_verdict : std::uint8_t {
  permit = 1,
  block = 2,
};

namespace capabilities {
constexpr std::uint64_t complete_tcp_message = 1ull << 0;
constexpr std::uint64_t fail_closed = 1ull << 1;
constexpr std::uint64_t coroutine_policy = 1ull << 2;
constexpr std::uint64_t sample_u32_be_framing = 1ull << 3;
constexpr std::uint64_t tcp_flow_blocking = 1ull << 4;
constexpr std::uint64_t current =
    complete_tcp_message | fail_closed | coroutine_policy |
    sample_u32_be_framing | tcp_flow_blocking;
} // namespace capabilities

constexpr auto inspection_requests =
    ntl::rpc::notification<0x4201, wfp_tcp_content_request>{}
        .max_response_size<16 * 1024>()
        .max_decode_allocation<32 * 1024>();

constexpr auto submit_verdict =
    ntl::rpc::method<0xC11,
                     std::int32_t(std::uint64_t, std::uint8_t)>{};
constexpr auto query_stats =
    ntl::rpc::method<0xC12, wfp_tcp_content_filter_stats()>{};

// {6BE65365-B03D-4C5D-84DD-95AC3E4BB505}
inline constexpr ntl::wfp::provider_key provider_key{GUID{
    0x6be65365,
    0xb03d,
    0x4c5d,
    {0x84, 0xdd, 0x95, 0xac, 0x3e, 0x4b, 0xb5, 0x05}}};

// {DA89C40E-3455-4D6A-8FF4-A0EFB044CB24}
inline constexpr ntl::wfp::sublayer_key sublayer_key{GUID{
    0xda89c40e,
    0x3455,
    0x4d6a,
    {0x8f, 0xf4, 0xa0, 0xef, 0xb0, 0x44, 0xcb, 0x24}}};

// {5CA8827E-0603-4CD0-A766-B9AE08BA535D}
inline constexpr ntl::wfp::callout_key<flow_layer>
    flow_callout_key{GUID{
        0x5ca8827e,
        0x0603,
        0x4cd0,
        {0xa7, 0x66, 0xb9, 0xae, 0x08, 0xba, 0x53, 0x5d}}};

// {9668C244-B861-4B9C-AC71-9DD3C84E9244}
inline constexpr ntl::wfp::callout_key<stream_layer>
    stream_callout_key{GUID{
        0x9668c244,
        0xb861,
        0x4b9c,
        {0xac, 0x71, 0x9d, 0xd3, 0xc8, 0x4e, 0x92, 0x44}}};

// {D91B100A-5BC5-463A-AE81-A5C81C7ADDC9}
inline constexpr ntl::wfp::filter_key<flow_layer>
    flow_filter_key{GUID{
        0xd91b100a,
        0x5bc5,
        0x463a,
        {0xae, 0x81, 0xa5, 0xc8, 0x1c, 0x7a, 0xdd, 0xc9}}};

// {D1377B03-D5C2-4F07-9807-5774FF772B75}
inline constexpr ntl::wfp::filter_key<stream_layer>
    stream_filter_key{GUID{
        0xd1377b03,
        0xd5c2,
        0x4f07,
        {0x98, 0x07, 0x57, 0x74, 0xff, 0x77, 0x2b, 0x75}}};

} // namespace wfp_tcp_content_filter
