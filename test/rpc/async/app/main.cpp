#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <numeric>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <ntl/rpc/client>

#include "rpc_async.hpp"

namespace {

using namespace std::chrono_literals;
constexpr wchar_t endpoint_name[] = L"crtsys_rpc_async";

class service_handle {
public:
  explicit service_handle(SC_HANDLE value = nullptr) noexcept : value_(value) {}
  service_handle(const service_handle &) = delete;
  service_handle &operator=(const service_handle &) = delete;
  ~service_handle() {
    if (value_)
      ::CloseServiceHandle(value_);
  }

  SC_HANDLE get() const noexcept { return value_; }
  explicit operator bool() const noexcept { return value_ != nullptr; }

private:
  SC_HANDLE value_;
};

ntl::rpc::contract_requirements requirements() {
  ntl::rpc::contract_requirements value;
  value.contract_version(crtsys_rpc_async::contract_version)
      .transport_features(ntl::rpc::transport_features::current |
                          ntl::rpc::transport_features::asynchronous_calls)
      .capabilities(crtsys_rpc_async::capabilities::current)
      .method(crtsys_rpc_async::delayed_value)
      .method(crtsys_rpc_async::delayed_void)
      .method(crtsys_rpc_async::checksum)
      .method(crtsys_rpc_async::query_stats);
  return value;
}

ntl::rpc::client open_client() {
  ntl::rpc::client client(endpoint_name);
  if (!client)
    throw std::runtime_error("could not open asynchronous RPC endpoint");
  (void)client.require_contract(requirements());
  return client;
}

bool wait_for_service_state(SC_HANDLE service, DWORD expected_state,
                            DWORD timeout_ms) {
  const ULONGLONG deadline = ::GetTickCount64() + timeout_ms;
  for (;;) {
    SERVICE_STATUS_PROCESS status{};
    DWORD bytes = 0;
    if (!::QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO,
                                reinterpret_cast<BYTE *>(&status),
                                sizeof(status), &bytes))
      return false;
    if (status.dwCurrentState == expected_state)
      return true;
    if (::GetTickCount64() >= deadline) {
      ::SetLastError(ERROR_TIMEOUT);
      return false;
    }
    ::Sleep(50);
  }
}

bool wait_for_endpoint(DWORD timeout_ms) {
  const ULONGLONG deadline = ::GetTickCount64() + timeout_ms;
  while (::GetTickCount64() < deadline) {
    try {
      auto client = open_client();
      (void)client.invoke(crtsys_rpc_async::query_stats);
      return true;
    } catch (...) {
    }
    ::Sleep(50);
  }
  return false;
}

void test_client_lifetime() {
  auto call = [] {
    auto client = open_client();
    return client.invoke_async(crtsys_rpc_async::delayed_value, 40u, 17u);
  }();
  if (call.get() != 17u)
    throw std::runtime_error("asynchronous call did not outlive its client");
}

void test_timeout() {
  auto client = open_client();
  auto call =
      client.invoke_async(crtsys_rpc_async::delayed_value, 200u, 23u);
  if (call.wait_for(5ms) != ntl::rpc::async_wait_status::timeout)
    throw std::runtime_error("delayed RPC did not report timeout");
  if (call.wait_for(5s) != ntl::rpc::async_wait_status::completed ||
      call.get() != 23u)
    throw std::runtime_error("timed RPC did not complete with its result");
}

void test_cancel() {
  auto client = open_client();
  auto call =
      client.invoke_async(crtsys_rpc_async::delayed_value, 500u, 29u);
  if (call.wait_for(10ms) != ntl::rpc::async_wait_status::timeout)
    throw std::runtime_error("cancellation call completed unexpectedly");
  if (!call.cancel())
    throw std::runtime_error("CancelIoEx did not find the pending RPC");

  try {
    (void)call.get();
  } catch (const std::system_error &error) {
    if (error.code().value() == ERROR_OPERATION_ABORTED)
      return;
    throw;
  }
  throw std::runtime_error("cancelled RPC returned a value");
}

void test_void_and_scope_cleanup() {
  auto client = open_client();
  auto completed =
      client.invoke_async(crtsys_rpc_async::delayed_void, 20u);
  completed.get();

  {
    auto abandoned =
        client.invoke_async(crtsys_rpc_async::delayed_void, 100u);
    if (abandoned.wait_for(1ms) != ntl::rpc::async_wait_status::timeout)
      throw std::runtime_error("scope cleanup RPC completed unexpectedly");
  }
}

void test_concurrent_calls() {
  auto client = open_client();
  std::vector<ntl::rpc::async_call<std::uint64_t>> calls;
  std::vector<std::uint64_t> expected;
  calls.reserve(32);
  expected.reserve(32);

  for (std::uint32_t index = 0; index != 32; ++index) {
    std::vector<std::uint32_t> values{index, index + 1, index + 2, 0x1000u};
    expected.push_back(
        std::accumulate(values.begin(), values.end(), std::uint64_t{0}));
    calls.emplace_back(
        client.invoke_async(crtsys_rpc_async::checksum, values));
  }

  for (std::size_t index = 0; index != calls.size(); ++index) {
    if (calls[index].get() != expected[index])
      throw std::runtime_error("concurrent RPC result mismatch");
  }
}

void test_pending_limit() {
  auto client = open_client();
  std::vector<ntl::rpc::async_call<void>> calls;
  calls.reserve(32);
  for (std::uint32_t index = 0; index != 32; ++index)
    calls.emplace_back(
        client.invoke_async(crtsys_rpc_async::delayed_void, 2000u));

  bool rejected = false;
  try {
    auto excess =
        client.invoke_async(crtsys_rpc_async::delayed_void, 2000u);
    excess.cancel();
  } catch (const std::system_error &) {
    rejected = true;
  }
  if (!rejected)
    throw std::runtime_error("asynchronous RPC pending limit was not enforced");

  for (auto &call : calls)
    (void)call.cancel();
  for (auto &call : calls) {
    try {
      call.get();
    } catch (const std::system_error &error) {
      if (error.code().value() == ERROR_OPERATION_ABORTED)
        continue;
      throw;
    }
    throw std::runtime_error("pending-limit cleanup returned success");
  }
}

void test_in_flight_stop(const wchar_t *service_name) {
  service_handle manager(
      ::OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
  service_handle service(
      manager ? ::OpenServiceW(manager.get(), service_name,
                               SERVICE_STOP | SERVICE_START |
                                   SERVICE_QUERY_STATUS)
              : nullptr);
  if (!manager || !service)
    throw std::system_error(static_cast<int>(::GetLastError()),
                            std::system_category(),
                            "could not open asynchronous RPC service");

  SERVICE_STATUS status{};
  {
    auto client = open_client();
    auto call =
        client.invoke_async(crtsys_rpc_async::delayed_value, 1000u, 73u);
    if (call.wait_for(5ms) != ntl::rpc::async_wait_status::timeout)
      throw std::runtime_error("in-flight stop RPC completed unexpectedly");

    if (!::ControlService(service.get(), SERVICE_CONTROL_STOP, &status))
      throw std::system_error(static_cast<int>(::GetLastError()),
                              std::system_category(),
                              "asynchronous RPC service stop failed");

    // The server rundown waits for this pending IRP. Consume the result and
    // close the device handle before waiting for SERVICE_STOPPED.
    if (call.get() != 73u)
      throw std::runtime_error("in-flight stop RPC result mismatch");
  }

  if (!wait_for_service_state(service.get(), SERVICE_STOPPED, 15000))
    throw std::system_error(static_cast<int>(::GetLastError()),
                            std::system_category(),
                            "asynchronous RPC service did not stop");

  if (!::StartServiceW(service.get(), 0, nullptr) &&
      ::GetLastError() != ERROR_SERVICE_ALREADY_RUNNING)
    throw std::system_error(static_cast<int>(::GetLastError()),
                            std::system_category(),
                            "asynchronous RPC service restart failed");
  if (!wait_for_service_state(service.get(), SERVICE_RUNNING, 15000) ||
      !wait_for_endpoint(5000))
    throw std::runtime_error(
        "asynchronous RPC endpoint did not recover after restart");
}

} // namespace

int wmain(int argc, wchar_t **argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::setvbuf(stderr, nullptr, _IONBF, 0);

  try {
    test_client_lifetime();
    test_timeout();
    test_cancel();
    test_void_and_scope_cleanup();
    test_concurrent_calls();
    test_pending_limit();

    if (argc > 1)
      test_in_flight_stop(argv[1]);

    auto client = open_client();
    const auto stats = client.invoke(crtsys_rpc_async::query_stats);
    if (stats.started != stats.completed)
      throw std::runtime_error("asynchronous RPC callbacks did not drain");

    std::printf("RPC async PASS: callbacks=%lu timeout=1 cancel=1 "
                "concurrent=32 pending_limit=32 service_restart=%u\n",
                static_cast<unsigned long>(stats.completed), argc > 1 ? 1u : 0u);
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "RPC async failure: %s\n", error.what());
    return 1;
  }
}
