#pragma once

#include <cstdint>

#if !defined(CTL_CODE) || !defined(GUID_DEFINED)
#error Include <ntddk.h> in kernel mode or <windows.h> and <winioctl.h> in user mode before this header.
#endif

namespace kmdf_wmi_ntl_sample {

inline constexpr GUID device_interface_guid = {
    0x1be43d84,
    0x19d0,
    0x41e7,
    {0xa9, 0xc3, 0x26, 0x01, 0x92, 0x64, 0x48, 0xce}};

inline constexpr GUID data_guid = {
    0xcc8be7f6,
    0x37d9,
    0x4818,
    {0xb8, 0x9e, 0x0d, 0x2d, 0x11, 0x0f, 0x11, 0xa0}};

inline constexpr GUID event_guid = {
    0x6e3f0489,
    0x13a3,
    0x423a,
    {0x97, 0x59, 0x2a, 0xde, 0x54, 0x86, 0x73, 0x88}};

inline constexpr wchar_t hardware_id[] = L"Root\\CrtSysKmdfNtlWmiSample";
inline constexpr wchar_t data_class_name[] = L"CrtSys_KmdfWmiSample";
inline constexpr wchar_t event_class_name[] = L"CrtSys_KmdfWmiEvent";
inline constexpr wchar_t mof_resource_name[] = L"CrtSysKmdfWmi";

struct data_block {
  std::uint32_t value;
  std::uint32_t query_count;
  std::uint32_t set_count;
  std::uint32_t method_count;
  std::uint32_t event_count;
};

inline constexpr std::uint32_t value_item_id = 1;
inline constexpr std::uint32_t transform_method_id = 1;

struct transform_input {
  std::uint32_t value;
};

struct transform_output {
  std::uint32_t value;
};

struct event_data {
  std::uint32_t sequence;
};

struct trigger_event_reply {
  std::uint32_t sequence;
};

struct trigger_event_ioctl {
  using input_type = void;
  using output_type = trigger_event_reply;

  static constexpr unsigned long device_type = FILE_DEVICE_UNKNOWN;
  static constexpr unsigned long function = 0x9b3;
  static constexpr unsigned long method = METHOD_BUFFERED;
  static constexpr unsigned long access = FILE_READ_DATA | FILE_WRITE_DATA;
  static constexpr unsigned long code =
      CTL_CODE(device_type, function, method, access);
};

inline constexpr unsigned long trigger_event_ioctl_code =
    trigger_event_ioctl::code;

} // namespace kmdf_wmi_ntl_sample
