#pragma once

#include <cstdint>
#include <vector>

#include <guiddef.h>
#include <ntl/rpc/common>
#include <ntl/wfp/layers>

struct wfp_udp_content_request {
  std::uint64_t id = 0;
  std::uint16_t source_port = 0;
  std::uint16_t destination_port = 0;
  std::vector<std::uint8_t> payload;

  friend zpp::serializer::access;
  template <typename Archive, typename Self>
  static void serialize(Archive &archive, Self &self) {
    archive(self.id, self.source_port, self.destination_port,
            self.payload);
  }
};

struct wfp_udp_content_filter_stats {
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

namespace wfp_udp_content_filter {

using layer = ntl::wfp::layers::datagram_data_v4;

inline constexpr wchar_t service_name[] =
    L"crtsys_wfp_udp_content_filter";
inline constexpr wchar_t endpoint_name[] =
    L"crtsys_wfp_udp_content_filter";

constexpr std::uint32_t contract_version = 1;
constexpr std::size_t maximum_payload_size = 4096;
constexpr std::size_t maximum_pending_requests = 64;
constexpr std::uint64_t verdict_timeout_100ns =
    2ull * 10ull * 1000ull * 1000ull;

enum class wire_verdict : std::uint8_t {
  permit = 1,
  block = 2,
};

namespace capabilities {
constexpr std::uint64_t complete_udp_datagram = 1ull << 0;
constexpr std::uint64_t fail_closed = 1ull << 1;
constexpr std::uint64_t coroutine_policy = 1ull << 2;
constexpr std::uint64_t current =
    complete_udp_datagram | fail_closed | coroutine_policy;
} // namespace capabilities

constexpr auto inspection_requests =
    ntl::rpc::notification<0x4301, wfp_udp_content_request>{}
        .max_response_size<16 * 1024>()
        .max_decode_allocation<32 * 1024>();

constexpr auto submit_verdict =
    ntl::rpc::method<0xC21,
                     std::int32_t(std::uint64_t, std::uint8_t)>{};
constexpr auto query_stats =
    ntl::rpc::method<0xC22, wfp_udp_content_filter_stats()>{};

// {0DB16A9E-E99F-4311-9F0D-B1923B3B7DE1}
inline constexpr ntl::wfp::provider_key provider_key{GUID{
    0x0db16a9e,
    0xe99f,
    0x4311,
    {0x9f, 0x0d, 0xb1, 0x92, 0x3b, 0x3b, 0x7d, 0xe1}}};

// {5B8FF1E5-847B-4528-A7DD-ADE40199C414}
inline constexpr ntl::wfp::sublayer_key sublayer_key{GUID{
    0x5b8ff1e5,
    0x847b,
    0x4528,
    {0xa7, 0xdd, 0xad, 0xe4, 0x01, 0x99, 0xc4, 0x14}}};

// {4A379420-3B0F-4F18-A901-730C53E3F522}
inline constexpr ntl::wfp::callout_key<layer> callout_key{GUID{
    0x4a379420,
    0x3b0f,
    0x4f18,
    {0xa9, 0x01, 0x73, 0x0c, 0x53, 0xe3, 0xf5, 0x22}}};

// {67160B99-3CF7-403B-B4C4-496B0AEB31BE}
inline constexpr ntl::wfp::filter_key<layer> filter_key{GUID{
    0x67160b99,
    0x3cf7,
    0x403b,
    {0xb4, 0xc4, 0x49, 0x6b, 0x0a, 0xeb, 0x31, 0xbe}}};

} // namespace wfp_udp_content_filter
