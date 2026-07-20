#include <ntddk.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

#include <ntl/driver>
#include <ntl/rpc/server>
#include <ntl/status>

#include "rpc_security.hpp"

namespace {

constexpr ACCESS_MASK rpc_method_access = 0x0001;

struct authenticated_user_ace {
  UCHAR type = 0; // ACCESS_ALLOWED_ACE_TYPE
  UCHAR flags = 0;
  USHORT size = 0;
  ACCESS_MASK mask = rpc_method_access;
  UCHAR sid_revision = 1;
  UCHAR sub_authority_count = 1;
  UCHAR identifier_authority[6] = {0, 0, 0, 0, 0, 5}; // SECURITY_NT_AUTHORITY
  ULONG sub_authority = 11; // SECURITY_AUTHENTICATED_USER_RID
};

struct authenticated_users_acl {
  ACL acl{};
  authenticated_user_ace ace{};
};

static_assert(offsetof(authenticated_users_acl, ace) == sizeof(ACL),
              "The ACL header and first ACE must be contiguous");
static_assert(sizeof(authenticated_user_ace) == 20,
              "The access-allowed ACE must match the Windows ACL layout");

struct method_security_descriptors {
  NTSTATUS initialize() noexcept {
    NTSTATUS status = RtlCreateSecurityDescriptor(
        allow_all, SECURITY_DESCRIPTOR_REVISION);
    if (!NT_SUCCESS(status))
      return status;
    status = RtlSetDaclSecurityDescriptor(allow_all, TRUE, nullptr, FALSE);
    if (!NT_SUCCESS(status))
      return status;

    deny_acl.AclRevision = ACL_REVISION;
    deny_acl.AclSize = sizeof(deny_acl);
    status = RtlCreateSecurityDescriptor(
        deny_all, SECURITY_DESCRIPTOR_REVISION);
    if (!NT_SUCCESS(status))
      return status;
    status = RtlSetDaclSecurityDescriptor(deny_all, TRUE, &deny_acl, FALSE);
    if (!NT_SUCCESS(status))
      return status;

    authenticated_acl.acl.AclRevision = ACL_REVISION;
    authenticated_acl.acl.AclSize =
        static_cast<USHORT>(sizeof(authenticated_acl));
    authenticated_acl.acl.AceCount = 1;
    authenticated_acl.ace.size =
        static_cast<USHORT>(sizeof(authenticated_acl.ace));

    status = RtlCreateSecurityDescriptor(
        authenticated_user, SECURITY_DESCRIPTOR_REVISION);
    if (!NT_SUCCESS(status))
      return status;
    return RtlSetDaclSecurityDescriptor(authenticated_user, TRUE,
                                        &authenticated_acl.acl, FALSE);
  }

  GENERIC_MAPPING mapping{rpc_method_access, rpc_method_access,
                          rpc_method_access, rpc_method_access};
  alignas(PVOID) UCHAR allow_all[64]{};
  alignas(PVOID) UCHAR deny_all[64]{};
  alignas(PVOID) UCHAR authenticated_user[64]{};
  ACL deny_acl{};
  authenticated_users_acl authenticated_acl{};
};

} // namespace

ntl::status ntl::main(ntl::driver &driver, const std::wstring &) {
  auto descriptors = std::make_shared<method_security_descriptors>();
  const NTSTATUS descriptor_status = descriptors->initialize();
  if (!NT_SUCCESS(descriptor_status))
    return ntl::status{descriptor_status};

  auto denied_callbacks = std::make_shared<std::atomic<std::uint32_t>>(0);
  ntl::rpc::server_options options(L"crtsys_rpc_security");
  options.asynchronous().max_pending_calls(32);
  auto server = ntl::rpc::make_server(driver, options);
  server
      ->on(crtsys_rpc_security::caller_info,
           [](const ntl::rpc::call_context &context) {
             return rpc_security_caller_info{
                 static_cast<std::uint64_t>(reinterpret_cast<ULONG_PTR>(
                     context.requestor_process_id())),
                 context.is_user_mode() ? 1u : 0u};
           })
      .on(crtsys_rpc_security::unrestricted_echo,
          [](std::uint32_t value) { return value; })
      .on_authorized(
          crtsys_rpc_security::authenticated_user_only,
          [descriptors](const ntl::rpc::call_context &context) {
            return context.check_access(descriptors->authenticated_user,
                                        rpc_method_access,
                                        descriptors->mapping);
          },
          [](std::uint32_t value) { return value; })
      .on_authorized(
          crtsys_rpc_security::descriptor_allowed,
          [descriptors](const ntl::rpc::call_context &context) {
            return context.check_access(descriptors->allow_all,
                                        rpc_method_access,
                                        descriptors->mapping);
          },
          [](std::uint32_t value) { return value; })
      .on_authorized(
          crtsys_rpc_security::descriptor_denied,
          [descriptors](const ntl::rpc::call_context &context) {
            return context.check_access(descriptors->deny_all,
                                        rpc_method_access,
                                        descriptors->mapping);
          },
          [denied_callbacks](std::uint32_t value) {
            denied_callbacks->fetch_add(1, std::memory_order_relaxed);
            return value;
          })
      .on(crtsys_rpc_security::denied_callback_count,
          [denied_callbacks] {
            return denied_callbacks->load(std::memory_order_relaxed);
          })
      .start();

  driver.on_unload(
      [server, descriptors, denied_callbacks]() mutable {
        server.reset();
        descriptors.reset();
        denied_callbacks.reset();
      });
  return ntl::status::ok();
}
