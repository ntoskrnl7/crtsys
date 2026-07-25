#pragma once

#include <ntl/rpc/common>

#include <array>
#include <cstddef>
#include <cstdint>

namespace crtsys_flt_scanner_runtime_test {

inline constexpr wchar_t filter_name[] = L"CrtSysFltScannerRuntimeTest";
inline constexpr wchar_t instance_name[] =
    L"CrtSys FLT scanner runtime test instance";
inline constexpr wchar_t altitude[] = L"370030.233";
inline constexpr wchar_t port_name[] = L"\\CrtSysFltScannerRuntimePort";

inline constexpr wchar_t test_directory_name[] =
    L"crtsys-flt-scanner-runtime";
inline constexpr wchar_t clean_open_name[] = L"clean-open.scan";
inline constexpr wchar_t infected_open_name[] = L"infected-open.scan";
inline constexpr wchar_t clean_write_name[] = L"clean-write.scan";
inline constexpr wchar_t blocked_write_name[] = L"blocked-write.scan";
inline constexpr wchar_t mapped_write_name[] = L"mapped-write.scan";
inline constexpr wchar_t transaction_commit_name[] =
    L"transaction-commit.scan";
inline constexpr wchar_t transaction_rollback_name[] =
    L"transaction-rollback.scan";

inline constexpr char foul_signature[] = "CRTSYS_FOUL";
inline constexpr std::size_t scan_payload_capacity = 4096;

enum class scan_stage : std::uint32_t {
  open,
  write,
  cleanup,
};

enum class scan_verdict : std::uint32_t {
  clean,
  infected,
};

struct scan_request {
  scan_stage stage = scan_stage::open;
  std::uint32_t bytes = 0;
  std::array<std::uint8_t, scan_payload_capacity> payload{};

  friend zpp::serializer::access;
  template <class Archive, class Self>
  static void serialize(Archive &archive, Self &self) {
    archive(self.stage, self.bytes, self.payload);
  }
};

struct observations {
  std::int32_t last_scan_status = 0;
  std::uint32_t instances_registered = 0;
  std::uint32_t policy_requests = 0;
  std::uint32_t policy_failures = 0;
  std::uint32_t open_scans = 0;
  std::uint32_t open_denied = 0;
  std::uint32_t write_scans = 0;
  std::uint32_t writes_allowed = 0;
  std::uint32_t writes_denied = 0;
  std::uint32_t cleanup_scans = 0;
  std::uint32_t cleanup_infected = 0;
  std::uint32_t sections_created = 0;
  std::uint32_t sections_mapped = 0;
  std::uint32_t sections_closed = 0;
  std::uint32_t section_conflicts = 0;
  std::uint32_t section_contexts_created = 0;
  std::uint32_t section_contexts_destroyed = 0;
  std::uint32_t pended_writes = 0;
  std::uint32_t resumed_writes = 0;
  std::uint32_t cancelled_writes = 0;
  std::uint32_t deferred_writes = 0;
  std::uint32_t handle_contexts_created = 0;
  std::uint32_t handle_contexts_destroyed = 0;
  std::uint32_t transaction_contexts_created = 0;
  std::uint32_t transaction_contexts_destroyed = 0;
  std::uint32_t transaction_enlistments = 0;
  std::uint32_t transaction_commits = 0;
  std::uint32_t transaction_rollbacks = 0;

  friend zpp::serializer::access;
  template <class Archive, class Self>
  static void serialize(Archive &archive, Self &self) {
    archive(
        self.last_scan_status, self.instances_registered,
        self.policy_requests, self.policy_failures, self.open_scans,
        self.open_denied, self.write_scans, self.writes_allowed,
        self.writes_denied, self.cleanup_scans, self.cleanup_infected,
        self.sections_created, self.sections_mapped, self.sections_closed,
        self.section_conflicts, self.section_contexts_created,
        self.section_contexts_destroyed, self.pended_writes,
        self.resumed_writes, self.cancelled_writes, self.deferred_writes,
        self.handle_contexts_created, self.handle_contexts_destroyed,
        self.transaction_contexts_created,
        self.transaction_contexts_destroyed, self.transaction_enlistments,
        self.transaction_commits, self.transaction_rollbacks);
  }
};

inline constexpr ntl::rpc::method<0xD30, scan_verdict(scan_request)>
    scan_payload{};
inline constexpr ntl::rpc::method<0xD31, observations()>
    query_observations{};
inline constexpr ntl::rpc::method<0xD32, std::uint32_t()>
    reset_observations{};

} // namespace crtsys_flt_scanner_runtime_test
