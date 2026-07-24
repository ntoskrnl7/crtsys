#pragma once

#include <ntl/ipc/common>
#include <ntl/rpc/common>

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace crtsys_flt_io_buffer_runtime_test {

inline constexpr wchar_t filter_name[] = L"CrtSysFltIoBufferRuntimeTest";
inline constexpr wchar_t instance_name[] =
    L"CrtSys FLT I/O buffer runtime test instance";
inline constexpr wchar_t altitude[] = L"370030.229";
inline constexpr wchar_t port_name[] = L"\\CrtSysFltIoBufferRuntimeTestPort";
inline constexpr std::wstring_view target_file_name =
    L"crtsys_flt_io_buffer_runtime_test.bin";
inline constexpr std::uint8_t transform_key = 0xA7;
inline constexpr std::size_t payload_bytes = 4096;
inline constexpr std::uint32_t transform_timeout_milliseconds = 2000;

enum class transform_direction : std::uint32_t {
  write_input = 1,
  read_output = 2,
};

enum class transform_test_behavior : std::uint32_t {
  normal = 0,
  wait_for_disconnect = 1,
  wait_for_teardown = 2,
};

struct runtime_state {
  std::uint32_t active_pre_requests = 0;
  std::uint32_t active_post_requests = 0;
  std::uint32_t pending_pre_resumes = 0;
  std::uint32_t pending_post_replies = 0;
  std::uint32_t pending_pre_cancels = 0;
  std::uint32_t pending_post_cancels = 0;
  std::uint32_t waiting_pre_requests = 0;
  std::uint32_t waiting_post_requests = 0;
  std::uint64_t waiting_pre_address = 0;
  std::uint64_t waiting_post_address = 0;

  template <typename Archive, typename Self>
  static void serialize(Archive &archive, Self &self) {
    archive(self.active_pre_requests, self.active_post_requests,
            self.pending_pre_resumes, self.pending_post_replies,
            self.pending_pre_cancels, self.pending_post_cancels,
            self.waiting_pre_requests, self.waiting_post_requests,
            self.waiting_pre_address, self.waiting_post_address);
  }
};

inline constexpr ntl::rpc::method<
    0xB01u,
    std::uint64_t(ntl::ipc::mapped_buffer_descriptor, transform_direction)>
    transform_mapped_buffer_method{};

inline constexpr ntl::rpc::method<
    0xB02u, std::uint32_t(transform_direction, transform_test_behavior)>
    configure_transform_test_method{};

inline constexpr ntl::rpc::method<0xB03u, runtime_state()>
    query_runtime_state_method{};

} // namespace crtsys_flt_io_buffer_runtime_test
