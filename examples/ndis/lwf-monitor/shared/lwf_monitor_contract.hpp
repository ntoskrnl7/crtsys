#pragma once

#include <cstdint>
#include <guiddef.h>

#ifndef CTL_CODE
#error Include <wdm.h> in kernel mode or <winioctl.h> in user mode first.
#endif

namespace ndis_lwf_monitor {

inline constexpr wchar_t service_name[] =
    L"crtsys_ndis_lwf_monitor";
inline constexpr wchar_t component_id[] =
    L"crtsys_ntl_lwf_monitor";
inline constexpr wchar_t device_name[] =
    L"CrtSysNdisLwfMonitor";
inline constexpr wchar_t user_device_path[] =
    L"\\\\.\\CrtSysNdisLwfMonitor";
inline constexpr wchar_t filter_friendly_name[] =
    L"crtsys NTL NDIS LWF monitor";
inline constexpr wchar_t filter_unique_name[] =
    L"{F620ED22-5CC9-498A-97AE-391880901A67}";

struct monitor_stats {
  std::uint64_t modules_attached;
  std::uint64_t modules_detached;
  std::uint64_t restarts;
  std::uint64_t pauses;
  std::uint64_t send_lists;
  std::uint64_t send_completions;
  std::uint64_t receive_lists;
  std::uint64_t send_bytes;
  std::uint64_t receive_bytes;
  std::uint64_t checksum_metadata;
  std::uint64_t large_send_metadata;
  std::uint64_t receive_coalescing_metadata;
  std::uint64_t vlan_metadata;
  std::uint64_t receive_hash_metadata;
};

struct query_stats_contract {
  static constexpr unsigned long device_type = FILE_DEVICE_UNKNOWN;
  static constexpr unsigned long function = 0x960;
  static constexpr unsigned long method = METHOD_BUFFERED;
  static constexpr unsigned long access = FILE_READ_DATA;
  using input_type = void;
  using output_type = monitor_stats;
};

inline constexpr unsigned long query_stats_ioctl =
    CTL_CODE(query_stats_contract::device_type,
             query_stats_contract::function,
             query_stats_contract::method,
             query_stats_contract::access);

// {354791CF-9812-4164-84EF-A080AFA9A0A1}
inline constexpr GUID device_class_guid{
    0x354791cf,
    0x9812,
    0x4164,
    {0x84, 0xef, 0xa0, 0x80, 0xaf, 0xa9, 0xa0, 0xa1}};

} // namespace ndis_lwf_monitor
