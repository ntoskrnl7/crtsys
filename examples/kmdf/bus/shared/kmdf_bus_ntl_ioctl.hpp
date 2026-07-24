#pragma once

#include <cstdint>

#if !defined(CTL_CODE) || !defined(GUID_DEFINED)
#error Include <ntddk.h> in kernel mode or <windows.h> and <winioctl.h> in user mode before this header.
#endif

namespace kmdf_bus_ntl_sample {

inline constexpr GUID bus_interface_guid = {
    0xc4605c71,
    0x15c8,
    0x4724,
    {0xaa, 0xd9, 0x38, 0x16, 0xcf, 0xa2, 0xf8, 0xa1}};

inline constexpr GUID child_device_interface_guid = {
    0x3c13cd3d,
    0x2fe8,
    0x4317,
    {0xb9, 0xc9, 0x71, 0x88, 0xef, 0x3a, 0x78, 0x9e}};

inline constexpr GUID child_query_interface_guid = {
    0x469140e2,
    0x6ac2,
    0x48cb,
    {0x93, 0xa0, 0xee, 0x67, 0xb0, 0xdd, 0x8e, 0xa5}};

inline constexpr GUID internal_bus_type_guid = {
    0x1530ea73,
    0x086b,
    0x11d1,
    {0xa0, 0x9f, 0x00, 0xc0, 0x4f, 0xc3, 0x40, 0xb1}};

struct child_identity {
  std::uint32_t serial_number;
};

enum class child_action : std::uint32_t {
  plug_in = 1,
  mark_missing = 2,
  eject = 3,
};

struct child_action_request {
  child_action action;
  std::uint32_t serial_number;
};

struct child_action_reply {
  std::int32_t operation_status;
  std::uint32_t serial_number;
};

struct pdo_event_counts {
  std::uint32_t resource_requirements_queries;
  std::uint32_t resource_queries;
  std::uint32_t ejects;
  std::uint32_t lock_changes;
  std::uint32_t wake_enables;
  std::uint32_t wake_disables;
  std::uint32_t reported_missing;
};

struct query_reply {
  std::uint32_t serial_number;
  std::uint32_t request_count;
  std::uint32_t server_irql;
  pdo_event_counts pdo_events;
  char message[96];
};

#if defined(_KERNEL_MODE) || defined(_KERNEL32_)
using get_serial_number_fn = NTSTATUS(NTAPI *)(void *context,
                                               std::uint32_t *serial_number);
using next_request_count_fn = NTSTATUS(NTAPI *)(void *context,
                                                std::uint32_t *request_count);
using get_pdo_event_counts_fn = NTSTATUS(NTAPI *)(void *context,
                                                  pdo_event_counts *counts);

// Driver-defined interfaces follow the Windows INTERFACE convention: the
// standard header is the first member and function pointers follow it.
struct child_query_interface {
  INTERFACE header;
  get_serial_number_fn get_serial_number;
  next_request_count_fn next_request_count;
  get_pdo_event_counts_fn get_pdo_event_counts;
};
#endif

struct child_action_ioctl_contract {
  using input_type = child_action_request;
  using output_type = child_action_reply;

  static constexpr unsigned long device_type = FILE_DEVICE_BUS_EXTENDER;
  static constexpr unsigned long function = 0x9b4;
  static constexpr unsigned long method = METHOD_BUFFERED;
  static constexpr unsigned long access = FILE_READ_DATA | FILE_WRITE_DATA;
  static constexpr unsigned long code =
      CTL_CODE(device_type, function, method, access);
};

struct query_ioctl_contract {
  using input_type = void;
  using output_type = query_reply;

  static constexpr unsigned long device_type = FILE_DEVICE_UNKNOWN;
  static constexpr unsigned long function = 0x9b5;
  static constexpr unsigned long method = METHOD_BUFFERED;
  static constexpr unsigned long access = FILE_READ_DATA | FILE_WRITE_DATA;
  static constexpr unsigned long code =
      CTL_CODE(device_type, function, method, access);
};

inline constexpr unsigned long query_ioctl_code = query_ioctl_contract::code;
inline constexpr unsigned long child_action_ioctl_code =
    child_action_ioctl_contract::code;

} // namespace kmdf_bus_ntl_sample
