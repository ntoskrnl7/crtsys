#pragma once

#include <cstdint>
#include <vector>

#include <guiddef.h>
#include <ntl/rpc/common>
#include <ntl/wfp/layers>

#include "content_filter_record.hpp"

struct wfp_udp_content_request {
  std::uint64_t id = 0;
  std::uint16_t address_family = 0;
  std::uint16_t source_port = 0;
  std::uint16_t destination_port = 0;
  std::vector<std::uint8_t> payload;

  friend zpp::serializer::access;
  template <typename Archive, typename Self>
  static void serialize(Archive &archive, Self &self) {
    archive(self.id, self.address_family, self.source_port,
            self.destination_port, self.payload);
  }
};

struct wfp_udp_content_filter_stats {
  std::uint64_t queued = 0;
  std::uint64_t permitted = 0;
  std::uint64_t blocked = 0;
  std::uint64_t timed_out = 0;
  std::uint64_t cancelled = 0;
  std::uint64_t malformed = 0;
  std::uint64_t failed = 0;
  std::uint64_t injection_completion_failures = 0;
  std::uint32_t last_injection_status = 0;

  friend zpp::serializer::access;
  template <typename Archive, typename Self>
  static void serialize(Archive &archive, Self &self) {
    archive(self.queued, self.permitted, self.blocked, self.timed_out,
            self.cancelled, self.malformed, self.failed,
            self.injection_completion_failures,
            self.last_injection_status);
  }
};

namespace wfp_udp_content_filter {

using layer_v4 = ntl::wfp::layers::datagram_data_v4;
using layer_v6 = ntl::wfp::layers::datagram_data_v6;

inline constexpr wchar_t service_name[] = L"crtsys_wfp_udp_content_filter";
inline constexpr wchar_t endpoint_name[] = L"crtsys_wfp_udp_content_filter";

constexpr std::uint32_t contract_version = 2;
constexpr std::size_t maximum_record_body_size = 4096;
constexpr std::size_t maximum_record_size =
    crtsys::examples::wfp::content_filter::wire_size(
        maximum_record_body_size);
constexpr std::size_t maximum_transport_control_data_size = 4096;
constexpr std::size_t maximum_pending_requests = 64;
constexpr std::uint64_t verdict_timeout_100ns =
    2ull * 10ull * 1000ull * 1000ull;

enum class wire_verdict : std::uint8_t {
  permit = 1,
  block = 2,
  malformed = 3,
};

namespace capabilities {
constexpr std::uint64_t complete_udp_datagram = 1ull << 0;
constexpr std::uint64_t fail_closed = 1ull << 1;
constexpr std::uint64_t coroutine_policy = 1ull << 2;
constexpr std::uint64_t structured_policy_record = 1ull << 3;
constexpr std::uint64_t reinjection_metadata = 1ull << 4;
constexpr std::uint64_t current =
    complete_udp_datagram | fail_closed | coroutine_policy |
    structured_policy_record | reinjection_metadata;
} // namespace capabilities

constexpr auto inspection_requests =
    ntl::rpc::notification<0x4301, wfp_udp_content_request>{}
        .max_response_size<16 * 1024>()
        .max_decode_allocation<32 * 1024>();

constexpr auto submit_verdict =
    ntl::rpc::method<0xC21, std::int32_t(std::uint64_t, std::uint8_t)>{};
constexpr auto query_stats =
    ntl::rpc::method<0xC22, wfp_udp_content_filter_stats()>{};

// {0DB16A9E-E99F-4311-9F0D-B1923B3B7DE1}
inline constexpr ntl::wfp::provider_key provider_key{
    GUID{0x0db16a9e,
         0xe99f,
         0x4311,
         {0x9f, 0x0d, 0xb1, 0x92, 0x3b, 0x3b, 0x7d, 0xe1}}};

// {5B8FF1E5-847B-4528-A7DD-ADE40199C414}
inline constexpr ntl::wfp::sublayer_key sublayer_key{
    GUID{0x5b8ff1e5,
         0x847b,
         0x4528,
         {0xa7, 0xdd, 0xad, 0xe4, 0x01, 0x99, 0xc4, 0x14}}};

// {4A379420-3B0F-4F18-A901-730C53E3F522}
inline constexpr ntl::wfp::terminating_callout_key<layer_v4> callout_key_v4{
    GUID{0x4a379420,
         0x3b0f,
         0x4f18,
         {0xa9, 0x01, 0x73, 0x0c, 0x53, 0xe3, 0xf5, 0x22}}};

// {67160B99-3CF7-403B-B4C4-496B0AEB31BE}
inline constexpr ntl::wfp::filter_key<layer_v4> filter_key_v4{
    GUID{0x67160b99,
         0x3cf7,
         0x403b,
         {0xb4, 0xc4, 0x49, 0x6b, 0x0a, 0xeb, 0x31, 0xbe}}};

// {A291183B-963C-456E-85FB-533C161D7F8C}
inline constexpr ntl::wfp::terminating_callout_key<layer_v6> callout_key_v6{
    GUID{0xa291183b,
         0x963c,
         0x456e,
         {0x85, 0xfb, 0x53, 0x3c, 0x16, 0x1d, 0x7f, 0x8c}}};

// {4CDE2C02-7151-4860-B577-A42D6CE099F7}
inline constexpr ntl::wfp::filter_key<layer_v6> filter_key_v6{
    GUID{0x4cde2c02,
         0x7151,
         0x4860,
         {0xb5, 0x77, 0xa4, 0x2d, 0x6c, 0xe0, 0x99, 0xf7}}};

} // namespace wfp_udp_content_filter
