#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string_view>

#include <ntl/wfp/all>

#include "ale_connect_block_contract.hpp"
#include "controller_lifecycle.hpp"

namespace {

using layer = ntl::wfp::layers::ale_auth_connect_v4;

std::uint16_t parse_port(const wchar_t *value) {
  wchar_t *end = nullptr;
  const unsigned long parsed = std::wcstoul(value, &end, 10);
  if (!value[0] || !end || *end || parsed == 0 || parsed > 65535)
    throw std::invalid_argument("port must be in 1..65535");
  return static_cast<std::uint16_t>(parsed);
}
ntl::wfp::policy_manifest policy_manifest() {
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
  const auto provider =
      transaction.add_provider({wfp_ale_connect_block::provider_key,
                                L"crtsys NTL WFP ALE connect-block provider",
                                L"Typed ALE connect-block controller",
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
      .remote_address_equal(
          ntl::wfp::ipv4_address::from_octets(127, 0, 0, 1))
      .remote_port_equal(port);
  transaction.add_filter(sublayer, callout, filter);
  if (deployment == policy_deployment::persistent_fail_closed) {
    ntl::wfp::boot_time_block_filter_builder<layer> boot_filter(
        wfp_ale_connect_block::boot_filter_key,
        L"Block the selected TCP port before BFE starts");
    boot_filter
        .description(L"Fail-closed handoff to the persistent runtime callout")
        .protocol_equal(IPPROTO_TCP)
        .remote_address_equal(
            ntl::wfp::ipv4_address::from_octets(127, 0, 0, 1))
        .remote_port_equal(port);
    transaction.add_boot_time_block_filter(sublayer, boot_filter);
  }
}

void install_ephemeral(
    ntl::wfp::policy_session &session,
    std::uint16_t port) {
  session.install([&](ntl::wfp::policy_transaction &transaction) {
    write_policy(transaction, port, policy_deployment::ephemeral);
  });
}

std::string state_stats(std::string_view state, std::uint16_t port) {
  std::ostringstream value;
  value << "state=" << state << "\nport=" << port << "\n";
  return value.str();
}

void install_arbitration_permit(
    ntl::wfp::policy_session &session,
    const ntl::wfp::application_id &application,
    std::uint16_t port) {
  session.install([&](ntl::wfp::policy_transaction &transaction) {
    const auto provider = transaction.add_provider(
        {wfp_ale_connect_block::arbitration_permit_provider_key,
         L"NTL WFP arbitration permit provider",
         L"Synthetic independent provider"});
    const auto sublayer = transaction.add_sublayer(
        provider,
        {wfp_ale_connect_block::arbitration_permit_sublayer_key,
         L"NTL WFP arbitration high permit sublayer",
         L"Permit is evaluated before lower block", 0x7200});
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
}

void install_arbitration_block(
    ntl::wfp::policy_session &session,
    const ntl::wfp::application_id &application,
    std::uint16_t port) {
  session.install([&](ntl::wfp::policy_transaction &transaction) {
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
}

void run_persistent(
    std::wstring_view mode, std::uint16_t port,
    crtsys::wfp_sample::controller_lifecycle &lifecycle) {
  const ntl::wfp::policy_revision revision1(1, policy_manifest());
  const ntl::wfp::policy_revision revision2(2, policy_manifest());
  auto session = ntl::wfp::policy_session::persistent(
      L"crtsys NTL WFP persistent controller");
  std::string state;
  if (mode == L"--persistent-install") {
    session.uninstall(revision1);
    session.uninstall(revision2);
    session.reconcile(
        revision1, [&](ntl::wfp::policy_transaction &transaction) {
          write_policy(transaction, port,
                       policy_deployment::persistent_fail_closed);
        });
    state = "persistent-installed";
  } else if (mode == L"--persistent-check") {
    const auto health = session.health(revision1);
    const auto runtime =
        session.inspect_filter(wfp_ale_connect_block::filter_key);
    const auto boot =
        session.inspect_filter(wfp_ale_connect_block::boot_filter_key);
    if (!health.healthy() || !runtime || !boot ||
        (runtime->flags & FWPM_FILTER_FLAG_PERSISTENT) == 0 ||
        (boot->flags & FWPM_FILTER_FLAG_BOOTTIME) == 0)
      throw std::runtime_error("persistent policy health check failed");
    state = "persistent-healthy";
  } else if (mode == L"--persistent-migrate") {
    session.migrate(
        revision1, revision2,
        [&](ntl::wfp::policy_transaction &transaction) {
          write_policy(transaction, port,
                       policy_deployment::persistent_fail_closed);
        });
    if (!session.health(revision2).healthy())
      throw std::runtime_error("persistent migration failed");
    state = "persistent-migrated";
  } else if (mode == L"--persistent-rollback") {
    session.rollback(
        revision2, revision1,
        [&](ntl::wfp::policy_transaction &transaction) {
          write_policy(transaction, port,
                       policy_deployment::persistent_fail_closed);
        });
    if (!session.health(revision1).healthy())
      throw std::runtime_error("persistent rollback failed");
    state = "persistent-rolled-back";
  } else if (mode == L"--persistent-recover") {
    session.uninstall(revision1);
    const bool restored = session.recover(
        revision1, [&](ntl::wfp::policy_transaction &transaction) {
          write_policy(transaction, port,
                       policy_deployment::persistent_fail_closed);
        });
    if (!restored || !session.health(revision1).healthy())
      throw std::runtime_error("persistent recovery failed");
    state = "persistent-recovered";
  } else if (mode == L"--persistent-uninstall") {
    session.uninstall(revision2);
    session.uninstall(revision1);
    state = "persistent-uninstalled";
  } else {
    throw std::invalid_argument("unknown persistent controller mode");
  }
  lifecycle.publish_ready(state_stats(state, port));
  lifecycle.wait_for_stop();
  lifecycle.publish_stats(state_stats(state, port));
}

} // namespace

int wmain(int argc, wchar_t **argv) {
  try {
    if (argc < 4)
      throw std::invalid_argument(
          "usage: crtsys_wfp_ale_connect_block_controller.exe "
          "<mode> <port> [application.exe] <ipc-directory>");
    const std::wstring_view mode(argv[1]);
    const auto port = parse_port(argv[2]);
    if (mode == L"--arbitration") {
      if (argc != 5)
        throw std::invalid_argument(
            "--arbitration requires application path and IPC directory");
      crtsys::wfp_sample::controller_lifecycle lifecycle(argv[4]);
      const auto application =
          ntl::wfp::application_id::from_path(
              std::filesystem::canonical(argv[3]).wstring());
      auto permit = ntl::wfp::policy_session::ephemeral(
          L"crtsys arbitration permit controller");
      install_arbitration_permit(permit, application, port);
      lifecycle.publish_ready(state_stats("permit-ready", port));
      lifecycle.wait_for_command(L"enable-block");
      auto block = std::make_unique<ntl::wfp::policy_session>(
          ntl::wfp::policy_session::ephemeral(
              L"crtsys arbitration block controller"));
      install_arbitration_block(*block, application, port);
      lifecycle.acknowledge(L"block.ready");
      lifecycle.wait_for_command(L"disable-block");
      block.reset();
      lifecycle.acknowledge(L"recovered.ready");
      lifecycle.wait_for_stop();
      lifecycle.publish_stats(state_stats("stopped", port));
      return 0;
    }
    if (argc != 4)
      throw std::invalid_argument("controller mode requires port and IPC directory");
    crtsys::wfp_sample::controller_lifecycle lifecycle(argv[3]);
    if (mode == L"--serve") {
      {
        auto policy = ntl::wfp::policy_session::ephemeral(
            L"crtsys ALE connect-block controller");
        install_ephemeral(policy, port);
        lifecycle.publish_ready(state_stats("ready", port));
        lifecycle.wait_for_stop();
      }
      lifecycle.publish_stats(state_stats("stopped", port));
      return 0;
    }
    run_persistent(mode, port, lifecycle);
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "ALE connect-block controller failed: "
              << error.what() << '\n';
    return 1;
  }
}
