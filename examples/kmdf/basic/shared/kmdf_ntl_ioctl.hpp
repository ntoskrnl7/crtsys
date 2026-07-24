#pragma once

#include <cstdint>

#ifndef CTL_CODE
#error Include <wdm.h> in kernel mode or <winioctl.h> in user mode before this header.
#endif

namespace kmdf_ntl_sample {

inline constexpr wchar_t device_name[] = L"CrtSysKmdfNtlSample";

struct transform_request {
  std::uint32_t value;
};

struct transform_reply {
  std::uint32_t value;
  std::uint32_t server_irql;
  char message[64];
};

struct manual_wait_request {
  std::uint32_t value;
};

struct manual_wait_reply {
  std::uint32_t value;
  std::uint32_t server_irql;
};

struct manual_queue_stats {
  std::uint32_t queued_requests;
  std::uint32_t released_requests;
  std::uint32_t canceled_requests;
  std::uint32_t forward_progress_requests;
};

struct transform_ioctl_contract {
  using input_type = transform_request;
  using output_type = transform_reply;

  static constexpr unsigned long device_type = FILE_DEVICE_UNKNOWN;
  static constexpr unsigned long function = 0x9B1;
  static constexpr unsigned long method = METHOD_BUFFERED;
  static constexpr unsigned long access = FILE_READ_DATA | FILE_WRITE_DATA;
  static constexpr unsigned long code =
      CTL_CODE(device_type, function, method, access);
};

inline constexpr unsigned long transform_ioctl_code =
    transform_ioctl_contract::code;

struct manual_wait_ioctl_contract {
  using input_type = manual_wait_request;
  using output_type = manual_wait_reply;

  static constexpr unsigned long device_type = FILE_DEVICE_UNKNOWN;
  static constexpr unsigned long function = 0x9B2;
  static constexpr unsigned long method = METHOD_BUFFERED;
  static constexpr unsigned long access = FILE_READ_DATA | FILE_WRITE_DATA;
  static constexpr unsigned long code =
      CTL_CODE(device_type, function, method, access);
};

struct manual_release_ioctl_contract {
  static constexpr unsigned long device_type = FILE_DEVICE_UNKNOWN;
  static constexpr unsigned long function = 0x9B3;
  static constexpr unsigned long method = METHOD_BUFFERED;
  static constexpr unsigned long access = FILE_READ_DATA | FILE_WRITE_DATA;
  static constexpr unsigned long code =
      CTL_CODE(device_type, function, method, access);
};

struct manual_stats_ioctl_contract {
  using output_type = manual_queue_stats;

  static constexpr unsigned long device_type = FILE_DEVICE_UNKNOWN;
  static constexpr unsigned long function = 0x9B4;
  static constexpr unsigned long method = METHOD_BUFFERED;
  static constexpr unsigned long access = FILE_READ_DATA | FILE_WRITE_DATA;
  static constexpr unsigned long code =
      CTL_CODE(device_type, function, method, access);
};

inline constexpr unsigned long manual_wait_ioctl_code =
    manual_wait_ioctl_contract::code;
inline constexpr unsigned long manual_release_ioctl_code =
    manual_release_ioctl_contract::code;
inline constexpr unsigned long manual_stats_ioctl_code =
    manual_stats_ioctl_contract::code;

} // namespace kmdf_ntl_sample
