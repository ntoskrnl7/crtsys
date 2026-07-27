#pragma once

#include <cstdint>

#if !defined(CTL_CODE) || !defined(GUID_DEFINED)
#error Include <ntddk.h> in kernel mode or <windows.h> and <winioctl.h> in user mode before this header.
#endif

namespace kmdf_reference {

inline constexpr GUID device_interface_guid = {
    0x1ddeb1fc,
    0x893d,
    0x409b,
    {0xbb, 0x48, 0x3f, 0x65, 0x14, 0xc2, 0xc7, 0xf8}};

inline constexpr wchar_t hardware_id[] = L"Root\\CrtSysKmdfReference";
inline constexpr std::uint16_t abi_version = 1;
inline constexpr std::uint32_t maximum_delay_ms = 30'000;

struct abi_header {
  std::uint16_t size;
  std::uint16_t version;
  std::uint32_t reserved;
};

struct query_request {
  abi_header header;
};

struct operation_request {
  abi_header header;
  std::uint32_t value;
  std::uint32_t delay_ms;
};

enum status_flag : std::uint32_t {
  hardware_prepared = 0x1,
  device_in_d0 = 0x2,
  file_cleanup_seen = 0x4,
};

struct status_reply {
  abi_header header;
  std::uint32_t value;
  std::uint32_t session_id;
  std::uint32_t sequence;
  std::uint32_t open_handles;
  std::uint32_t prepare_count;
  std::uint32_t d0_entry_count;
  std::uint32_t completed_requests;
  std::uint32_t canceled_requests;
  std::uint32_t server_irql;
  std::uint32_t flags;
  char message[64];
};

static_assert(sizeof(abi_header) == 8);
static_assert(sizeof(query_request) == 8);
static_assert(sizeof(operation_request) == 16);
static_assert(sizeof(status_reply) == 112);

constexpr abi_header make_header(std::uint16_t size) noexcept {
  return {size, abi_version, 0};
}

constexpr bool valid_header(const abi_header &header,
                            std::uint16_t size) noexcept {
  return header.size == size && header.version == abi_version &&
         header.reserved == 0;
}

struct query_ioctl_contract {
  using input_type = query_request;
  using output_type = status_reply;

  static constexpr unsigned long code =
      CTL_CODE(FILE_DEVICE_UNKNOWN, 0x9c0, METHOD_BUFFERED,
               FILE_READ_DATA | FILE_WRITE_DATA);
};

struct operation_ioctl_contract {
  using input_type = operation_request;
  using output_type = status_reply;

  static constexpr unsigned long code =
      CTL_CODE(FILE_DEVICE_UNKNOWN, 0x9c1, METHOD_BUFFERED,
               FILE_READ_DATA | FILE_WRITE_DATA);
};

inline constexpr unsigned long query_ioctl = query_ioctl_contract::code;
inline constexpr unsigned long operation_ioctl =
    operation_ioctl_contract::code;

} // namespace kmdf_reference
