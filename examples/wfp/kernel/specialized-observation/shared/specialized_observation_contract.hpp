#pragma once

#include <array>
#include <cstdint>
#include <guiddef.h>
#include <ntl/wfp/layers>

#ifndef CTL_CODE
#error Include <wdm.h> in kernel mode or <winioctl.h> in user mode first.
#endif

namespace wfp_specialized_observation {

using endpoint_v4 = ntl::wfp::layers::ale_endpoint_closure_v4;
using endpoint_v6 = ntl::wfp::layers::ale_endpoint_closure_v6;
using mac_in = ntl::wfp::layers::inbound_mac_frame_ethernet;
using mac_out = ntl::wfp::layers::outbound_mac_frame_ethernet;
using vswitch_in = ntl::wfp::layers::ingress_vswitch_ethernet;
using vswitch_out = ntl::wfp::layers::egress_vswitch_ethernet;
using fast_in = ntl::wfp::layers::inbound_transport_fast;
using fast_out = ntl::wfp::layers::outbound_transport_fast;
using ipsec_v4 = ntl::wfp::layers::ipsec_v4;
using ipsec_v6 = ntl::wfp::layers::ipsec_v6;

inline constexpr wchar_t service_name[] =
    L"crtsys_wfp_specialized_observation";
inline constexpr wchar_t device_name[] =
    L"CrtSysWfpSpecializedObservation";
inline constexpr wchar_t user_device_path[] =
    L"\\\\.\\CrtSysWfpSpecializedObservation";

enum class counter : std::size_t {
  endpoint_v4,
  endpoint_v6,
  mac_in,
  mac_out,
  vswitch_in,
  vswitch_out,
  count,
};

inline constexpr std::size_t counter_count =
    static_cast<std::size_t>(counter::count);
inline constexpr std::uint32_t all_layers_mask =
    (1u << counter_count) - 1u;

struct observation_stats {
  std::uint32_t version;
  std::uint32_t registered_mask;
  std::array<std::uint64_t, counter_count> indications;
};

struct query_stats_contract {
  static constexpr unsigned long device_type = FILE_DEVICE_UNKNOWN;
  static constexpr unsigned long function = 0x97a;
  static constexpr unsigned long method = METHOD_BUFFERED;
  static constexpr unsigned long access = FILE_READ_DATA;
  using input_type = void;
  using output_type = observation_stats;
};

inline constexpr unsigned long query_stats_ioctl =
    CTL_CODE(query_stats_contract::device_type,
             query_stats_contract::function,
             query_stats_contract::method,
             query_stats_contract::access);

constexpr GUID make_guid(std::uint32_t value) noexcept {
  return GUID{value,
              0x7eac,
              0x4dde,
              {0xa2, 0x81, 0x67, 0x51, 0xc3, 0x19, 0x58, 0x40}};
}

inline constexpr GUID device_class_guid = make_guid(0xdaf81000);
inline constexpr ntl::wfp::provider_key provider_key{
    make_guid(0xdaf81001)};
inline constexpr ntl::wfp::sublayer_key sublayer_key{
    make_guid(0xdaf81002)};

#define NTL_WFP_SPECIALIZED_KEYS(name, layer, value)                            \
  inline constexpr ntl::wfp::inspection_callout_key<layer> name##_callout_key{              \
      make_guid(value)};                                                         \
  inline constexpr ntl::wfp::filter_key<layer> name##_filter_key{               \
      make_guid((value) + 1)}

NTL_WFP_SPECIALIZED_KEYS(endpoint_v4, endpoint_v4, 0xdaf81010);
NTL_WFP_SPECIALIZED_KEYS(endpoint_v6, endpoint_v6, 0xdaf81020);
NTL_WFP_SPECIALIZED_KEYS(mac_in, mac_in, 0xdaf81030);
NTL_WFP_SPECIALIZED_KEYS(mac_out, mac_out, 0xdaf81040);
NTL_WFP_SPECIALIZED_KEYS(vswitch_in, vswitch_in, 0xdaf81050);
NTL_WFP_SPECIALIZED_KEYS(vswitch_out, vswitch_out, 0xdaf81060);

#undef NTL_WFP_SPECIALIZED_KEYS

} // namespace wfp_specialized_observation
