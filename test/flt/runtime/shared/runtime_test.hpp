#pragma once

#include <ntl/flt/rpc>
#include <ntl/ipc/common>
#include <ntl/ipc/shared_ring>
#include <ntl/rpc/common>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <thread>

namespace crtsys_flt_runtime_test {
inline constexpr wchar_t filter_name[] = L"CrtSysFltRuntimeTest";
inline constexpr wchar_t default_instance_name[] =
    L"CrtSys FLT runtime test instance";
inline constexpr wchar_t secondary_instance_name[] =
    L"CrtSys FLT runtime test secondary instance";
inline constexpr wchar_t secondary_altitude[] = L"370030.228";
inline constexpr wchar_t drop_port_name[] = L"\\CrtSysFltDropPolicyPort";
inline constexpr wchar_t byte_quota_port_name[] =
    L"\\CrtSysFltByteQuotaPort";
inline constexpr wchar_t reject_port_name[] = L"\\CrtSysFltRejectPort";
inline constexpr wchar_t connection_limit_port_name[] =
    L"\\CrtSysFltConnectionLimitPort";
inline constexpr wchar_t session_limit_port_name[] =
    L"\\CrtSysFltSessionLimitPort";
inline constexpr wchar_t security_allow_port_name[] =
    L"\\CrtSysFltSecurityAllowPort";
inline constexpr wchar_t security_deny_port_name[] =
    L"\\CrtSysFltSecurityDenyPort";
inline constexpr wchar_t security_administrators_port_name[] =
    L"\\CrtSysFltSecurityAdministratorsPort";
inline constexpr ntl::rpc::session_token restored_session_token{
    0x435254535953464cull, 0x54524553544f5245ull};
inline constexpr std::wstring_view file_name = L"crtsys_flt_runtime_test.tmp";
inline constexpr std::wstring_view renamed_file_name =
    L"crtsys_flt_runtime_test_renamed.tmp";
struct ring_record {
  std::uint32_t sequence = 0;
  std::uint32_t value = 0;
};

using test_ring = ntl::ipc::shared_ring<ring_record, 4>;
inline constexpr std::size_t ring_stride =
    (test_ring::required_bytes() + test_ring::required_alignment() - 1) /
    test_ring::required_alignment() * test_ring::required_alignment();
inline constexpr std::size_t shared_region_bytes = ring_stride * 2;

inline constexpr ntl::rpc::method<0xA51, std::uint32_t(ntl::ipc::buffer_token,
                                                       ntl::ipc::buffer_token)>
    shared_ring_method{};

struct progress_event {
  std::uint32_t value = 0;

  friend zpp::serializer::access;
  template <typename Archive, typename Self>
  static void serialize(Archive &archive, Self &self) {
    archive(self.value);
  }
};

inline constexpr ntl::rpc::method<0xA53, std::uint64_t(std::uint32_t, bool)>
    publish_progress_method{};

using background_progress_notification_type =
    decltype(ntl::rpc::notification<0xB02, progress_event>{}
                 .with_priority<ntl::rpc::delivery_priority::background>());
inline constexpr background_progress_notification_type
    background_progress_notification{};

using critical_progress_notification_type =
    decltype(ntl::rpc::notification<0xB03, progress_event>{}
                 .with_priority<ntl::rpc::delivery_priority::critical>());
inline constexpr critical_progress_notification_type
    critical_progress_notification{};

inline constexpr ntl::rpc::method<0xA55, std::uint64_t(std::uint64_t,
                                                       std::uint32_t, bool)>
    publish_priority_method{};

inline constexpr ntl::rpc::method<0xA56, std::uint32_t(std::uint32_t)>
    client_transform_method{};
inline constexpr ntl::rpc::method<0xA57, std::uint32_t(std::uint32_t)>
    request_client_method{};
inline constexpr ntl::rpc::method<0xA58, std::uint64_t(std::uint32_t)>
    publish_targeted_method{};
inline constexpr ntl::rpc::method<0xA59, std::uint64_t()>
    connection_state_method{};
inline constexpr ntl::rpc::method<0xA5A, std::uint64_t()>
    storage_stats_method{};
inline constexpr ntl::rpc::method<0xA5B,
                                  std::uint64_t(std::uint64_t, std::uint32_t)>
    drop_publish_method{};
inline constexpr ntl::rpc::method<0xA5C, std::uint64_t()>
    connection_lifecycle_stats_method{};
} // namespace crtsys_flt_runtime_test

NTL_FLT_DECLARE_NOTIFICATION_ID(crtsys_flt_runtime_test, 0xB00,
                                crtsys_flt_runtime_test::progress_event,
                                progress)
NTL_FLT_DECLARE_STREAM_ID(crtsys_flt_runtime_test, 0xB01, numbers,
                          std::uint32_t, std::uint32_t)

NTL_FLT_RPC_BEGIN_CONTRACT(crtsys_flt_runtime_test,
                           L"\\CrtSysFltRuntimePort", 1, 0x3ull)

NTL_FLT_REGISTER_NOTIFICATION(crtsys_flt_runtime_test, progress);

NTL_FLT_REGISTER_NOTIFICATION(crtsys_flt_runtime_test, background_progress);

NTL_FLT_REGISTER_NOTIFICATION(crtsys_flt_runtime_test, critical_progress);

NTL_FLT_REGISTER_STREAM(crtsys_flt_runtime_test, numbers, value, stream, {
  if (value == 98) {
    auto failed = stream.try_fail(STATUS_UNSUCCESSFUL);
    if (!failed)
      throw ntl::exception(failed.status(),
                           "Stream failure was not queued");
    return;
  }
  auto queued = stream.try_write(value + 100);
  if (!queued)
    throw ntl::exception(queued.status(), "Stream reply was not queued");
  if (value == 99) {
    auto completed = stream.try_complete();
    if (!completed)
      throw ntl::exception(completed.status(),
                           "Stream completion was not queued");
  }
});

NTL_FLT_ADD_CALLBACK_ID(crtsys_flt_runtime_test, 0xA50,
                        std::uint32_t(std::uint32_t), ping,
                        [](std::uint32_t value) noexcept { return value + 1; })

NTL_FLT_ADD_AUTHORIZED_METHOD_ID(
    crtsys_flt_runtime_test, 0xA54, std::uint32_t(std::uint32_t), denied,
    [](const ntl::flt::communication_context &) noexcept {
      return ntl::status{STATUS_ACCESS_DENIED};
    },
    [](std::uint32_t value) noexcept { return value; })

NTL_FLT_ADD_CALLBACK_ID(
    crtsys_flt_runtime_test, 0xA52, std::uint32_t(std::uint32_t),
    cancellable_count,
    [](const ntl::flt::communication_context &context,
       std::uint32_t iterations) -> std::uint32_t {
      for (std::uint32_t index = 0; index != iterations; ++index) {
        if (context.cancellation_requested())
          throw ntl::exception(STATUS_CANCELLED,
                               "Minifilter async request was cancelled");
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
      }
      return iterations;
    })

NTL_FLT_RPC_END(crtsys_flt_runtime_test)
