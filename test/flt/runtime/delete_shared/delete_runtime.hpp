#pragma once

#include <ntl/rpc/common>

#include <cstdint>

namespace crtsys_flt_delete_runtime_test {

inline constexpr wchar_t filter_name[] = L"CrtSysFltDeleteRuntimeTest";
inline constexpr wchar_t instance_name[] =
    L"CrtSys FLT delete runtime test instance";
inline constexpr wchar_t altitude[] = L"370030.232";
inline constexpr wchar_t port_name[] = L"\\CrtSysFltDeleteRuntimePort";

inline constexpr wchar_t test_directory_name[] =
    L"crtsys-flt-delete-runtime";
inline constexpr wchar_t legacy_clear_name[] = L"legacy-clear.tmp";
inline constexpr wchar_t on_close_clear_name[] = L"on-close-clear.tmp";
inline constexpr wchar_t readonly_name[] = L"readonly-delete.tmp";
inline constexpr wchar_t delete_on_close_name[] = L"delete-on-close.tmp";
inline constexpr wchar_t stream_base_name[] = L"stream-base.tmp";
inline constexpr wchar_t stream_name[] = L"payload";
inline constexpr wchar_t base_delete_name[] = L"base-delete.tmp";
inline constexpr wchar_t pending_name[] = L"pending-delete.tmp";
inline constexpr wchar_t race_name[] = L"racing-delete.tmp";

struct observations {
  std::int32_t last_cleanup_status = 0;
  std::uint32_t create_delete_on_close = 0;
  std::uint32_t legacy_requests = 0;
  std::uint32_t extended_requests = 0;
  std::uint32_t delete_requests = 0;
  std::uint32_t clear_requests = 0;
  std::uint32_t on_close_requests = 0;
  std::uint32_t posix_requests = 0;
  std::uint32_t force_image_section_requests = 0;
  std::uint32_t ignore_readonly_requests = 0;
  std::uint32_t set_information_successes = 0;
  std::uint32_t set_information_failures = 0;
  std::uint32_t disposition_races = 0;
  std::uint32_t race_gate_arrivals = 0;
  std::uint32_t cleanup_checks = 0;
  std::uint32_t cleanup_present = 0;
  std::uint32_t file_deletions = 0;
  std::uint32_t stream_deletions = 0;
  std::uint32_t completion_states_created = 0;
  std::uint32_t completion_states_destroyed = 0;
  std::uint32_t stream_contexts_created = 0;
  std::uint32_t stream_contexts_destroyed = 0;

  friend zpp::serializer::access;
  template <class Archive, class Self>
  static void serialize(Archive &archive, Self &self) {
    archive(
        self.last_cleanup_status, self.create_delete_on_close,
        self.legacy_requests, self.extended_requests, self.delete_requests,
        self.clear_requests, self.on_close_requests, self.posix_requests,
        self.force_image_section_requests, self.ignore_readonly_requests,
        self.set_information_successes, self.set_information_failures,
        self.disposition_races, self.race_gate_arrivals, self.cleanup_checks,
        self.cleanup_present, self.file_deletions, self.stream_deletions,
        self.completion_states_created, self.completion_states_destroyed,
        self.stream_contexts_created, self.stream_contexts_destroyed);
  }
};

inline constexpr ntl::rpc::method<0xD20, observations()>
    query_observations{};
inline constexpr ntl::rpc::method<0xD21, std::uint32_t()>
    reset_observations{};
inline constexpr ntl::rpc::method<0xD22, std::uint32_t()>
    arm_race_gate{};

} // namespace crtsys_flt_delete_runtime_test
