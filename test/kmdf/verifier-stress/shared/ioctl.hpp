#pragma once

#include <cstdint>

#ifndef CTL_CODE
#error Include <wdm.h> in kernel mode or <winioctl.h> in user mode first.
#endif

namespace kmdf_verifier_stress {

inline constexpr wchar_t device_name[] = L"CrtSysKmdfVerifierStress";

struct transform_request {
  std::uint32_t value;
};

struct transform_reply {
  std::uint32_t value;
  std::uint32_t server_irql;
};

struct wait_request {
  std::uint32_t value;
};

struct wait_reply {
  std::uint32_t value;
  std::uint32_t server_irql;
};

struct queue_stats {
  std::uint32_t queued_requests;
  std::uint32_t released_requests;
  std::uint32_t canceled_requests;
  std::uint32_t transformed_requests;
  std::uint32_t open_files;
  std::uint32_t forward_progress_requests;
};

inline constexpr unsigned long transform_ioctl =
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0xA10, METHOD_BUFFERED,
             FILE_READ_DATA | FILE_WRITE_DATA);
inline constexpr unsigned long wait_ioctl =
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0xA11, METHOD_BUFFERED,
             FILE_READ_DATA | FILE_WRITE_DATA);
inline constexpr unsigned long release_ioctl =
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0xA12, METHOD_BUFFERED,
             FILE_READ_DATA | FILE_WRITE_DATA);
inline constexpr unsigned long stats_ioctl =
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0xA13, METHOD_BUFFERED,
             FILE_READ_DATA | FILE_WRITE_DATA);

} // namespace kmdf_verifier_stress
