#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

#include <ntl/wfp/all>

#include "ale_connect_block_contract.hpp"

namespace {

class winsock_session {
public:
  winsock_session() {
    WSADATA data{};
    const int result = WSAStartup(MAKEWORD(2, 2), &data);
    if (result != 0)
      throw std::system_error(result, std::system_category(), "WSAStartup");
  }
  ~winsock_session() { WSACleanup(); }
};

class socket_owner {
public:
  explicit socket_owner(SOCKET value = INVALID_SOCKET) noexcept
      : value_(value) {}
  socket_owner(const socket_owner &) = delete;
  socket_owner &operator=(const socket_owner &) = delete;
  socket_owner(socket_owner &&other) noexcept
      : value_(std::exchange(other.value_, INVALID_SOCKET)) {}
  ~socket_owner() {
    if (value_ != INVALID_SOCKET)
      closesocket(value_);
  }
  SOCKET get() const noexcept { return value_; }

private:
  SOCKET value_ = INVALID_SOCKET;
};

socket_owner make_listener(std::uint16_t port) {
  socket_owner listener(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
  if (listener.get() == INVALID_SOCKET)
    throw std::system_error(WSAGetLastError(), std::system_category(),
                            "socket(listener)");

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(port);
  if (bind(listener.get(), reinterpret_cast<const sockaddr *>(&address),
           sizeof(address)) == SOCKET_ERROR)
    throw std::system_error(WSAGetLastError(), std::system_category(), "bind");
  if (listen(listener.get(), 4) == SOCKET_ERROR)
    throw std::system_error(WSAGetLastError(), std::system_category(),
                            "listen");
  return listener;
}

int connect_once(std::uint16_t port) {
  socket_owner client(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
  if (client.get() == INVALID_SOCKET)
    throw std::system_error(WSAGetLastError(), std::system_category(),
                            "socket(client)");

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(port);
  if (connect(client.get(), reinterpret_cast<const sockaddr *>(&address),
              sizeof(address)) == 0)
    return ERROR_SUCCESS;
  return WSAGetLastError();
}

ntl::wfp::policy_manifest policy_manifest() {
  using layer = ntl::wfp::layers::ale_auth_connect_v4;
  ntl::wfp::policy_manifest manifest;
  manifest.include(wfp_ale_connect_block::provider_key)
      .include(wfp_ale_connect_block::sublayer_key)
      .include(wfp_ale_connect_block::callout_key)
      .include(wfp_ale_connect_block::filter_key)
      .include(wfp_ale_connect_block::boot_filter_key);
  return manifest;
}

enum class policy_deployment {
  ephemeral,
  persistent_fail_closed,
};

void write_policy(ntl::wfp::policy_transaction &transaction,
                  std::uint16_t port,
                  policy_deployment deployment) {
  using layer = ntl::wfp::layers::ale_auth_connect_v4;
  const auto provider =
      transaction.add_provider({wfp_ale_connect_block::provider_key,
                                L"crtsys NTL WFP ALE connect-block provider",
                                L"Provider for the ALE connect-block sample",
                                wfp_ale_connect_block::service_name});
  const auto sublayer = transaction.add_sublayer(
      provider, {wfp_ale_connect_block::sublayer_key,
                 L"crtsys NTL WFP ALE connect-block sublayer",
                 L"Typed ALE connect-block sublayer", 0x7100});
  const auto callout = transaction.add_callout<layer>(
      provider, {wfp_ale_connect_block::callout_key,
                 L"crtsys NTL WFP ALE connect-block callout",
                 L"Typed ALE_AUTH_CONNECT_V4 terminating callout"});

  ntl::wfp::filter_builder<layer> filter(
      wfp_ale_connect_block::filter_key,
      L"Block the selected loopback TCP port",
      ntl::wfp::callout_unavailable::block);
  filter.description(L"Observable ALE_AUTH_CONNECT_V4 enforcement")
      .protocol_equal(IPPROTO_TCP)
      .remote_address_equal(ntl::wfp::ipv4_address::from_octets(127, 0, 0, 1))
      .remote_port_equal(port);
  transaction.add_filter(sublayer, callout, filter);

  if (deployment != policy_deployment::persistent_fail_closed)
    return;

  ntl::wfp::boot_time_block_filter_builder<layer>
      boot_filter(
          wfp_ale_connect_block::boot_filter_key,
          L"Block the selected TCP port before BFE starts");
  boot_filter
      .description(
          L"Fail-closed handoff to the persistent runtime callout")
      .protocol_equal(IPPROTO_TCP)
      .remote_address_equal(
          ntl::wfp::ipv4_address::from_octets(
              127, 0, 0, 1))
      .remote_port_equal(port);
  transaction.add_boot_time_block_filter(
      sublayer, boot_filter);
}

void install_policy(ntl::wfp::policy_session &session, std::uint16_t port) {
  session.install([&](ntl::wfp::policy_transaction &transaction) {
    write_policy(
        transaction, port, policy_deployment::ephemeral);
  });
}

int persistent_lifecycle_self_test(std::uint16_t port) {
  const ntl::wfp::policy_revision revision1(
      1, policy_manifest());
  const ntl::wfp::policy_revision revision2(
      2, policy_manifest());

  {
    auto cleanup = ntl::wfp::policy_session::persistent(
        L"crtsys NTL WFP persistent pre-cleanup");
    cleanup.uninstall(revision1);
    cleanup.reconcile(
        revision1,
        [&](ntl::wfp::policy_transaction &transaction) {
          write_policy(
              transaction, port,
              policy_deployment::persistent_fail_closed);
        });
  }

  {
    auto observer = ntl::wfp::policy_session::persistent(
        L"crtsys NTL WFP persistent observer");
    const auto health = observer.health(revision1);
    const auto filter =
        observer.inspect_filter(wfp_ale_connect_block::filter_key);
    const auto boot_filter =
        observer.inspect_filter(
            wfp_ale_connect_block::boot_filter_key);
    if (!health.healthy() || !filter ||
        !boot_filter ||
        (filter->flags & FWPM_FILTER_FLAG_PERSISTENT) == 0 ||
        (boot_filter->flags & FWPM_FILTER_FLAG_BOOTTIME) == 0)
      throw std::runtime_error(
          "persistent WFP graph did not survive controller close");
  }

  {
    auto upgrade = ntl::wfp::policy_session::persistent(
        L"crtsys NTL WFP persistent migration");
    upgrade.migrate(
        revision1, revision2,
        [&](ntl::wfp::policy_transaction &transaction) {
          write_policy(
              transaction, port,
              policy_deployment::persistent_fail_closed);
        });
    if (!upgrade.health(revision2).healthy())
      throw std::runtime_error(
          "persistent WFP migration did not activate revision 2");
    upgrade.rollback(
        revision2, revision1,
        [&](ntl::wfp::policy_transaction &transaction) {
          write_policy(
              transaction, port,
              policy_deployment::persistent_fail_closed);
        });
    if (!upgrade.health(revision1).healthy())
      throw std::runtime_error(
          "persistent WFP rollback did not restore revision 1");
  }

  if (connect_once(port) != WSAEACCES)
    throw std::runtime_error(
        "persistent WFP filter did not enforce after controller close");

  {
    auto cleanup = ntl::wfp::policy_session::persistent(
        L"crtsys NTL WFP persistent recovery");
    cleanup.uninstall(revision1);
    const bool restored = cleanup.recover(
        revision1,
        [&](ntl::wfp::policy_transaction &transaction) {
          write_policy(
              transaction, port,
              policy_deployment::persistent_fail_closed);
        });
    if (!restored || !cleanup.health(revision1).healthy())
      throw std::runtime_error(
          "persistent WFP recovery did not restore the graph");
    cleanup.uninstall(revision1);
  }

  {
    auto observer = ntl::wfp::policy_session::persistent(
        L"crtsys NTL WFP uninstall observer");
    if (observer.health(revision1).missing.size() != 5)
      throw std::runtime_error(
          "persistent WFP graph remained after manifest uninstall");
  }

  if (connect_once(port) != ERROR_SUCCESS)
    throw std::runtime_error(
        "connectivity did not recover after persistent uninstall");

  std::wcout << L"NTL WFP persistent lifecycle ok: boot-time=block, "
                L"reconcile=atomic, migrate=2, rollback=1, recover=1, "
                L"session-close=retained, uninstall=clean, port="
             << port << L'\n';
  return 0;
}

int arbitration_self_test(std::uint16_t port) {
  using layer = ntl::wfp::layers::ale_auth_connect_v4;
  const auto application = ntl::wfp::application_id::current_process();

  auto permit = ntl::wfp::policy_session::ephemeral(
      L"crtsys NTL WFP arbitration permit provider");
  permit.install([&](ntl::wfp::policy_transaction &transaction) {
    const auto provider = transaction.add_provider(
        {wfp_ale_connect_block::arbitration_permit_provider_key,
         L"NTL WFP arbitration permit provider",
         L"Synthetic independent provider"});
    const auto sublayer = transaction.add_sublayer(
        provider,
        {wfp_ale_connect_block::arbitration_permit_sublayer_key,
         L"NTL WFP arbitration high permit sublayer",
         L"Permit is evaluated before the lower block sublayer", 0x7200});
    ntl::wfp::enforcement_filter_builder<layer> filter(
        wfp_ale_connect_block::arbitration_permit_filter_key,
        L"Permit selected loopback connection",
        ntl::wfp::enforcement_action::permit);
    filter.application_equal(application)
        .protocol_equal(IPPROTO_TCP)
        .remote_address_equal(
            ntl::wfp::ipv4_address::from_octets(127, 0, 0, 1))
        .remote_port_equal(port);
    transaction.add_enforcement_filter(sublayer, filter);
  });
  if (connect_once(port) != ERROR_SUCCESS)
    throw std::runtime_error(
        "the independent permit provider did not allow the connection");

  {
    auto block = ntl::wfp::policy_session::ephemeral(
        L"crtsys NTL WFP arbitration block provider");
    block.install([&](ntl::wfp::policy_transaction &transaction) {
      const auto provider = transaction.add_provider(
          {wfp_ale_connect_block::arbitration_block_provider_key,
           L"NTL WFP arbitration block provider",
           L"Synthetic independent provider"});
      const auto sublayer = transaction.add_sublayer(
          provider,
          {wfp_ale_connect_block::arbitration_block_sublayer_key,
           L"NTL WFP arbitration lower block sublayer",
           L"Block vetoes the earlier provider permit", 0x7100});
      ntl::wfp::enforcement_filter_builder<layer> filter(
          wfp_ale_connect_block::arbitration_block_filter_key,
          L"Block selected loopback connection",
          ntl::wfp::enforcement_action::block);
      filter.application_equal(application)
          .protocol_equal(IPPROTO_TCP)
          .remote_address_equal(
              ntl::wfp::ipv4_address::from_octets(127, 0, 0, 1))
          .remote_port_equal(port);
      transaction.add_enforcement_filter(sublayer, filter);
    });

    const auto permit_filter = permit.inspect_filter(
        wfp_ale_connect_block::arbitration_permit_filter_key);
    const auto block_filter = block.inspect_filter(
        wfp_ale_connect_block::arbitration_block_filter_key);
    if (!permit_filter || !block_filter ||
        permit_filter->action_type != FWP_ACTION_PERMIT ||
        block_filter->action_type != FWP_ACTION_BLOCK)
      throw std::runtime_error(
          "synthetic provider filters have unexpected actions");
    if (connect_once(port) != WSAEACCES)
      throw std::runtime_error(
          "the lower independent block provider did not veto permit");
  }

  if (connect_once(port) != ERROR_SUCCESS)
    throw std::runtime_error(
        "connectivity did not recover after block provider removal");
  std::wcout
      << L"NTL WFP provider-arbitration ok: high-permit=installed, "
         L"lower-block=veto, block-removal=recovered, port="
      << port << L'\n';
  return 0;
}

int listener_process(std::uint16_t port) {
  auto listener = make_listener(port);
  std::wcout << L"NTL WFP crash-recovery listener ready: port="
             << port << std::endl;
  for (;;) {
    socket_owner accepted(
        accept(listener.get(), nullptr, nullptr));
    if (accepted.get() == INVALID_SOCKET)
      throw std::system_error(
          WSAGetLastError(), std::system_category(), "accept");
  }
}

int hold_ephemeral_policy(std::uint16_t port) {
  auto policy = ntl::wfp::policy_session::ephemeral(
      L"crtsys NTL WFP crash-recovery held policy");
  install_policy(policy, port);
  std::wcout << L"NTL WFP crash-recovery policy ready: port="
             << port << std::endl;
  for (;;)
    Sleep(INFINITE);
}

int probe_connection(std::uint16_t port) {
  const int result = connect_once(port);
  std::wcout << L"NTL WFP crash-recovery probe: port=" << port
             << L", error=" << result << L'\n';
  return result == ERROR_SUCCESS ? 0 : result == WSAEACCES ? 2 : 3;
}

} // namespace

int wmain(int argc, wchar_t **argv) {
  try {
    const bool persistent_self_test =
        argc > 1 &&
        std::wstring_view(argv[1]) == L"--persistent-lifecycle-self-test";
    const bool arbitration_test =
        argc > 1 &&
        std::wstring_view(argv[1]) == L"--arbitration-self-test";
    const bool listener_mode =
        argc > 1 && std::wstring_view(argv[1]) == L"--listener";
    const bool hold_policy_mode =
        argc > 1 && std::wstring_view(argv[1]) == L"--hold-policy";
    const bool probe_mode =
        argc > 1 && std::wstring_view(argv[1]) == L"--probe";
    const int port_argument =
        persistent_self_test || arbitration_test ||
                listener_mode || hold_policy_mode || probe_mode
            ? 2
            : 1;
    const auto port =
        argc > port_argument
            ? static_cast<std::uint16_t>(std::stoul(argv[port_argument]))
            : wfp_ale_connect_block::default_port;

    std::wcout << L"[1/5] Starting a loopback TCP listener on port " << port
               << L".\n";
    winsock_session winsock;

    if (listener_mode)
      return listener_process(port);
    if (hold_policy_mode)
      return hold_ephemeral_policy(port);
    if (probe_mode)
      return probe_connection(port);

    auto listener = make_listener(port);

    if (persistent_self_test)
      return persistent_lifecycle_self_test(port);
    if (arbitration_test)
      return arbitration_self_test(port);

    int blocked_error = ERROR_SUCCESS;
    {
      std::wcout
          << L"[2/5] Installing an ephemeral WFP rule: outbound IPv4 TCP "
             L"connects to this port must call the kernel driver.\n";
      auto policy = ntl::wfp::policy_session::ephemeral(
          L"crtsys ntl::wfp ALE connect-block sample");
      install_policy(policy, port);
      std::wcout
          << L"[3/5] Connecting while the rule exists. The driver should "
             L"return block.\n";
      blocked_error = connect_once(port);
      if (blocked_error == ERROR_SUCCESS) {
        std::wcerr << L"WFP policy did not block TCP port " << port << L'\n';
        return 2;
      }
      if (blocked_error != WSAEACCES) {
        std::wcerr << L"The connection failed, but not because WFP denied "
                      L"it. Winsock error="
                   << blocked_error << L'\n';
        return 4;
      }
      std::wcout << L"      Expected result: WSAEACCES (10013).\n"
                    L"[4/5] Closing the ephemeral WFP session. Its provider, "
                    L"sublayer, callout, and filter are removed now.\n";
    }

    std::wcout << L"[5/5] Connecting again without the rule. This connection "
                  L"should succeed.\n";
    const int restored_error = connect_once(port);
    if (restored_error != ERROR_SUCCESS) {
      std::wcerr << L"TCP remained blocked after dynamic session close: "
                 << restored_error << L'\n';
      return 3;
    }

    std::wcout << L"NTL WFP ale-connect-block ok: blocked_error="
               << blocked_error << L", restored_connect=success, port=" << port
               << L'\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "NTL WFP ale-connect-block failed: " << error.what() << '\n';
    return 1;
  }
}
