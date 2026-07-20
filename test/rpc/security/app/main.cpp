#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <system_error>

#include <ntl/handle>
#include <ntl/rpc/client>

#include "rpc_security.hpp"

namespace {

class thread_impersonation {
public:
  thread_impersonation() = default;
  thread_impersonation(const thread_impersonation &) = delete;
  thread_impersonation &operator=(const thread_impersonation &) = delete;

  ~thread_impersonation() {
    if (active_)
      RevertToSelf();
  }

  void restricted() {
    HANDLE process_token_raw = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY,
                          &process_token_raw))
      throw std::system_error(static_cast<int>(GetLastError()),
                              std::system_category(),
                              "OpenProcessToken failed");
    ntl::unique_handle process_token(process_token_raw);

    BYTE world_buffer[SECURITY_MAX_SID_SIZE]{};
    DWORD world_size = sizeof(world_buffer);
    if (!CreateWellKnownSid(WinWorldSid, nullptr, world_buffer, &world_size))
      throw std::system_error(static_cast<int>(GetLastError()),
                              std::system_category(),
                              "CreateWellKnownSid failed");

    SID_AND_ATTRIBUTES restricting_sid{};
    restricting_sid.Sid = world_buffer;
    HANDLE restricted_raw = nullptr;
    if (!CreateRestrictedToken(process_token.get(), DISABLE_MAX_PRIVILEGE, 0,
                               nullptr, 0, nullptr, 1, &restricting_sid,
                               &restricted_raw))
      throw std::system_error(static_cast<int>(GetLastError()),
                              std::system_category(),
                              "CreateRestrictedToken failed");
    ntl::unique_handle restricted_token(restricted_raw);

    HANDLE impersonation_raw = nullptr;
    if (!DuplicateTokenEx(restricted_token.get(),
                          TOKEN_IMPERSONATE | TOKEN_QUERY, nullptr,
                          SecurityImpersonation, TokenImpersonation,
                          &impersonation_raw))
      throw std::system_error(static_cast<int>(GetLastError()),
                              std::system_category(),
                              "DuplicateTokenEx failed");
    impersonation_token_.reset(impersonation_raw);

    if (!SetThreadToken(nullptr, impersonation_token_.get()))
      throw std::system_error(static_cast<int>(GetLastError()),
                              std::system_category(),
                              "SetThreadToken failed");
    active_ = true;
  }

private:
  ntl::unique_handle impersonation_token_;
  bool active_ = false;
};

template <typename Callback> void expect_access_denied(Callback &&callback) {
  try {
    callback();
  } catch (const std::system_error &error) {
    if (static_cast<DWORD>(error.code().value()) == ERROR_ACCESS_DENIED)
      return;
    throw;
  }
  throw std::runtime_error("RPC method authorization unexpectedly succeeded");
}

} // namespace

int wmain() {
  try {
    ntl::rpc::client client(L"crtsys_rpc_security");
    if (!client)
      throw std::runtime_error("could not open RPC security endpoint");

    const auto normal = client.invoke(crtsys_rpc_security::caller_info);
    if (!normal.user_mode ||
        normal.process_id != static_cast<std::uint64_t>(GetCurrentProcessId()))
      throw std::runtime_error("normal caller identity was not preserved");

    if (client.invoke(crtsys_rpc_security::authenticated_user_only, 41u) !=
        41u)
      throw std::runtime_error("authenticated caller policy failed");
    if (client.invoke(crtsys_rpc_security::descriptor_allowed, 42u) != 42u)
      throw std::runtime_error("allow security descriptor policy failed");
    expect_access_denied([&] {
      (void)client.invoke(crtsys_rpc_security::descriptor_denied, 43u);
    });
    if (client.invoke(crtsys_rpc_security::denied_callback_count) != 0)
      throw std::runtime_error("denied RPC callback was invoked");

    {
      thread_impersonation impersonation;
      impersonation.restricted();
      const auto restricted = client.invoke(crtsys_rpc_security::caller_info);
      if (!restricted.user_mode ||
          restricted.process_id !=
              static_cast<std::uint64_t>(GetCurrentProcessId()))
        throw std::runtime_error("restricted caller identity was not preserved");
      if (client.invoke(crtsys_rpc_security::unrestricted_echo, 44u) != 44u)
        throw std::runtime_error("unrestricted RPC rejected restricted caller");
      expect_access_denied([&] {
        (void)client.invoke(crtsys_rpc_security::authenticated_user_only, 45u);
      });
    }

    std::printf("RPC security PASS: pid=%llu mode=user restricted_deny=1 "
                "method_allow=1 method_deny=2 callback_suppressed=1\n",
                static_cast<unsigned long long>(normal.process_id));
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "RPC security failure: %s\n", error.what());
    return 1;
  }
}
