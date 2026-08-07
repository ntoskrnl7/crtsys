#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>

#include <ntl/wfp/all>

#include "async_inspection_contract.hpp"
#include "controller_lifecycle.hpp"

namespace {

class service_handle {
public:
  explicit service_handle(SC_HANDLE value = nullptr) noexcept
      : value_(value) {}
  service_handle(const service_handle &) = delete;
  service_handle &operator=(const service_handle &) = delete;
  ~service_handle() {
    if (value_)
      CloseServiceHandle(value_);
  }
  SC_HANDLE get() const noexcept { return value_; }
private:
  SC_HANDLE value_ = nullptr;
};

void wait_for_service_state(SC_HANDLE service, DWORD expected) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(30);
  for (;;) {
    SERVICE_STATUS_PROCESS status{};
    DWORD bytes = 0;
    if (!QueryServiceStatusEx(
            service, SC_STATUS_PROCESS_INFO,
            reinterpret_cast<BYTE *>(&status), sizeof(status), &bytes))
      throw std::system_error(
          GetLastError(), std::system_category(), "QueryServiceStatusEx");
    if (status.dwCurrentState == expected)
      return;
    if (std::chrono::steady_clock::now() >= deadline)
      throw std::runtime_error("driver service state transition timed out");
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
  }
}

void stop_driver_service(std::wstring_view name) {
  service_handle manager(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
  if (!manager.get())
    throw std::system_error(
        GetLastError(), std::system_category(), "OpenSCManagerW");
  const std::wstring service_name(name);
  service_handle service(OpenServiceW(
      manager.get(), service_name.c_str(), SERVICE_STOP | SERVICE_QUERY_STATUS));
  if (!service.get())
    throw std::system_error(
        GetLastError(), std::system_category(), "OpenServiceW(stop)");
  SERVICE_STATUS status{};
  if (!ControlService(service.get(), SERVICE_CONTROL_STOP, &status)) {
    const DWORD error = GetLastError();
    if (error != ERROR_SERVICE_NOT_ACTIVE)
      throw std::system_error(
          error, std::system_category(), "ControlService(STOP)");
  }
  wait_for_service_state(service.get(), SERVICE_STOPPED);
}

void start_driver_service(std::wstring_view name) {
  service_handle manager(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
  if (!manager.get())
    throw std::system_error(
        GetLastError(), std::system_category(), "OpenSCManagerW");
  const std::wstring service_name(name);
  service_handle service(OpenServiceW(
      manager.get(), service_name.c_str(), SERVICE_START | SERVICE_QUERY_STATUS));
  if (!service.get())
    throw std::system_error(
        GetLastError(), std::system_category(), "OpenServiceW(start)");
  if (!StartServiceW(service.get(), 0, nullptr)) {
    const DWORD error = GetLastError();
    if (error != ERROR_SERVICE_ALREADY_RUNNING)
      throw std::system_error(
          error, std::system_category(), "StartServiceW");
  }
  wait_for_service_state(service.get(), SERVICE_RUNNING);
}

std::uint16_t parse_port(const wchar_t *value) {
  wchar_t *end = nullptr;
  const unsigned long parsed = std::wcstoul(value, &end, 10);
  if (!value[0] || !end || *end || parsed == 0 || parsed > 65535)
    throw std::invalid_argument("port must be in 1..65535");
  return static_cast<std::uint16_t>(parsed);
}

template <class Layer>
void install_filter(ntl::wfp::policy_transaction &transaction,
                    const ntl::wfp::sublayer_ref &sublayer,
                    const ntl::wfp::terminating_callout_ref<Layer> &callout,
                    ntl::wfp::filter_key<Layer> key, const wchar_t *name,
                    std::uint16_t port, std::uint64_t context) {
  ntl::wfp::filter_builder<Layer> filter(
      key, name, ntl::wfp::callout_unavailable::block);
  filter.protocol_equal(IPPROTO_TCP).remote_port_equal(port).context(context);
  transaction.add_filter(sublayer, callout, filter);
}

void install_policy(ntl::wfp::policy_session &session,
                    std::uint16_t permit_v4,
                    std::uint16_t block_v4,
                    std::uint16_t permit_v6,
                    std::uint16_t block_v6) {
  session.install([&](ntl::wfp::policy_transaction &transaction) {
    const auto provider = transaction.add_provider(
        {wfp_async_inspection::provider_key,
         L"crtsys NTL WFP async-inspection provider",
         L"Dynamic ALE authorization inspection provider"});
    const auto sublayer = transaction.add_sublayer(
        provider, {wfp_async_inspection::sublayer_key,
                   L"crtsys NTL WFP async-inspection sublayer",
                   L"Contains permit and block authorization rules", 0x7300});
    const auto callout_v4 =
        transaction.add_callout<wfp_async_inspection::layer_v4>(
            provider, {wfp_async_inspection::callout_key_v4,
                       L"Pend selected IPv4 TCP authorizations",
                       L"Worker-thread ALE decision"});
    const auto callout_v6 =
        transaction.add_callout<wfp_async_inspection::layer_v6>(
            provider, {wfp_async_inspection::callout_key_v6,
                       L"Pend selected IPv6 TCP authorizations",
                       L"Worker-thread ALE decision"});
    install_filter(transaction, sublayer, callout_v4,
                   wfp_async_inspection::permit_filter_key_v4,
                   L"Asynchronously permit IPv4", permit_v4,
                   wfp_async_inspection::permit_context);
    install_filter(transaction, sublayer, callout_v4,
                   wfp_async_inspection::block_filter_key_v4,
                   L"Asynchronously block IPv4", block_v4,
                   wfp_async_inspection::block_context);
    install_filter(transaction, sublayer, callout_v6,
                   wfp_async_inspection::permit_filter_key_v6,
                   L"Asynchronously permit IPv6", permit_v6,
                   wfp_async_inspection::permit_context);
    install_filter(transaction, sublayer, callout_v6,
                   wfp_async_inspection::block_filter_key_v6,
                   L"Asynchronously block IPv6", block_v6,
                   wfp_async_inspection::block_context);
  });
}

std::string stats(
    std::string_view state, std::uint16_t permit_v4,
    std::uint16_t block_v4, std::uint16_t permit_v6,
    std::uint16_t block_v6) {
  std::ostringstream value;
  value << "state=" << state << "\n"
        << "permit_v4=" << permit_v4 << "\n"
        << "block_v4=" << block_v4 << "\n"
        << "permit_v6=" << permit_v6 << "\n"
        << "block_v6=" << block_v6 << "\n";
  return value.str();
}

} // namespace

int wmain(int argc, wchar_t **argv) {
  try {
    if (argc != 7 && argc != 8)
      throw std::invalid_argument(
          "usage: crtsys_wfp_async_inspection_controller.exe "
          "<--serve|--unload-race> <permit-v4> <block-v4> "
          "<permit-v6> <block-v6> [driver-service] <ipc-directory>");
    const std::wstring_view mode(argv[1]);
    if (mode != L"--serve" && mode != L"--unload-race")
      throw std::invalid_argument("unknown async-inspection controller mode");
    if ((mode == L"--serve" && argc != 7) ||
        (mode == L"--unload-race" && argc != 8))
      throw std::invalid_argument(
          "driver-service is required only by --unload-race");
    const auto permit_v4 = parse_port(argv[2]);
    const auto block_v4 = parse_port(argv[3]);
    const auto permit_v6 = parse_port(argv[4]);
    const auto block_v6 = parse_port(argv[5]);
    const std::wstring_view driver_service =
        mode == L"--unload-race" ? argv[6] : L"";
    crtsys::wfp_sample::controller_lifecycle lifecycle(
        argv[mode == L"--unload-race" ? 7 : 6]);
    {
      auto policy = ntl::wfp::policy_session::ephemeral(
          L"crtsys async-inspection controller");
      install_policy(policy, permit_v4, block_v4, permit_v6, block_v6);
      lifecycle.publish_ready(
          stats(mode == L"--unload-race" ? "race-ready" : "ready",
                permit_v4, block_v4, permit_v6, block_v6));
      if (mode == L"--unload-race") {
        lifecycle.wait_for_command(L"stop-driver");
        stop_driver_service(driver_service);
        lifecycle.acknowledge(L"driver.stopped");
        lifecycle.wait_for_command(L"start-driver");
        start_driver_service(driver_service);
        lifecycle.acknowledge(L"driver.started");
        lifecycle.wait_for_command(L"release-policy");
      } else {
        lifecycle.wait_for_stop();
      }
    }
    if (mode == L"--unload-race") {
      lifecycle.acknowledge(L"policy.released");
      lifecycle.wait_for_stop();
    }
    lifecycle.publish_stats(
        stats("stopped", permit_v4, block_v4, permit_v6, block_v6));
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "async-inspection controller failed: "
              << error.what() << '\n';
    return 1;
  }
}
