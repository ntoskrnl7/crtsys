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
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <ntl/rpc/client>

#include "rpc_notifications.hpp"

namespace {

using namespace std::chrono_literals;
constexpr wchar_t endpoint_name[] = L"crtsys_rpc_notifications";

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
  value.contract_version(crtsys_rpc_notifications::contract_version)
      .transport_features(ntl::rpc::transport_features::current |
                          ntl::rpc::transport_features::kernel_notifications)
      .capabilities(crtsys_rpc_notifications::capabilities::current)
      .method(crtsys_rpc_notifications::publish)
      .method(crtsys_rpc_notifications::publish_at_dispatch)
      .method(crtsys_rpc_notifications::query_stats);
  return value;
}

ntl::rpc::client open_client() {
  ntl::rpc::client client(endpoint_name);
  if (!client)
    throw std::runtime_error("could not open notification RPC endpoint");
  (void)client.require_contract(requirements());
  return client;
}

void validate(const rpc_notification_message &message,
              std::uint64_t sequence, std::uint32_t value_count) {
  if (message.sequence != sequence ||
      message.text != "notification-" + std::to_string(sequence) ||
      message.values.size() != value_count)
    throw std::runtime_error("notification payload metadata mismatch");
  for (std::uint32_t index = 0; index != value_count; ++index) {
    if (message.values[index] !=
        static_cast<std::uint32_t>(sequence) + index)
      throw std::runtime_error("notification payload value mismatch");
  }
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
      (void)client.invoke(crtsys_rpc_notifications::query_stats);
      return true;
    } catch (...) {
    }
    ::Sleep(50);
  }
  return false;
}

void test_unbuffered_delivery() {
  auto client = open_client();
  if (client.invoke(crtsys_rpc_notifications::publish, 1u, 1u))
    throw std::runtime_error("notification was buffered without a receiver");
}

void test_typed_receive() {
  auto client = open_client();
  auto receive =
      client.receive_async(crtsys_rpc_notifications::progress);
  if (!client.invoke(crtsys_rpc_notifications::publish, 42u, 32u))
    throw std::runtime_error("notification publisher found no receiver");
  validate(receive.get(), 42u, 32u);
}

void test_synchronous_receive() {
  auto client = open_client();
  std::exception_ptr publisher_error;
  std::thread publisher([&publisher_error] {
    try {
      ::Sleep(20);
      auto producer = open_client();
      if (!producer.invoke(crtsys_rpc_notifications::publish, 43u, 4u))
        throw std::runtime_error("synchronous receive had no queued IRP");
    } catch (...) {
      publisher_error = std::current_exception();
    }
  });
  try {
    const auto message = client.receive(crtsys_rpc_notifications::progress);
    publisher.join();
    if (publisher_error)
      std::rethrow_exception(publisher_error);
    validate(message, 43u, 4u);
  } catch (...) {
    publisher.join();
    throw;
  }
}

void test_timeout_and_cancel() {
  auto client = open_client();
  auto receive =
      client.receive_async(crtsys_rpc_notifications::progress);
  if (receive.wait_for(2ms) != ntl::rpc::async_wait_status::timeout)
    throw std::runtime_error("notification receive did not time out");
  if (!receive.cancel())
    throw std::runtime_error("CancelIoEx did not find notification receive");
  try {
    (void)receive.get();
  } catch (const std::system_error &error) {
    if (error.code().value() == ERROR_OPERATION_ABORTED)
      return;
    throw;
  }
  throw std::runtime_error("cancelled notification returned a payload");
}

void test_scope_cleanup() {
  auto client = open_client();
  {
    auto abandoned =
        client.receive_async(crtsys_rpc_notifications::progress);
    if (abandoned.wait_for(1ms) != ntl::rpc::async_wait_status::timeout)
      throw std::runtime_error("abandoned notification completed early");
  }
}

void test_fifo_and_pending_limit() {
  auto client = open_client();
  std::vector<ntl::rpc::notification_wait<rpc_notification_message>> waits;
  waits.reserve(16);
  for (std::uint32_t index = 0; index != 16; ++index)
    waits.emplace_back(
        client.receive_async(crtsys_rpc_notifications::progress));

  bool rejected = false;
  try {
    auto excess =
        client.receive_async(crtsys_rpc_notifications::progress);
    (void)excess;
  } catch (const std::system_error &) {
    rejected = true;
  }
  if (!rejected)
    throw std::runtime_error("notification pending limit was not enforced");

  for (std::uint32_t index = 0; index != 16; ++index) {
    if (!client.invoke(crtsys_rpc_notifications::publish,
                       100u + index, index + 1))
      throw std::runtime_error("queued notification receive was lost");
  }
  for (std::uint32_t index = 0; index != 16; ++index)
    validate(waits[index].get(), 100u + index, index + 1);
}

void test_irql_contract() {
  auto client = open_client();
  auto receive =
      client.receive_async(crtsys_rpc_notifications::progress);
  const auto status = client.invoke(
      crtsys_rpc_notifications::publish_at_dispatch, 200u);
  if (status != crtsys_rpc_notifications::invalid_device_state)
    throw std::runtime_error("notification accepted DISPATCH_LEVEL payload");
  if (receive.ready())
    throw std::runtime_error("rejected high-IRQL publish consumed a receive");
  if (!client.invoke(crtsys_rpc_notifications::publish, 200u, 2u))
    throw std::runtime_error("PASSIVE_LEVEL retry did not publish");
  validate(receive.get(), 200u, 2u);
}

void test_pending_stop(const wchar_t *service_name) {
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
                            "could not open notification RPC service");

  SERVICE_STATUS status{};
  {
    auto client = open_client();
    auto receive =
        client.receive_async(crtsys_rpc_notifications::progress);
    if (receive.wait_for(2ms) != ntl::rpc::async_wait_status::timeout)
      throw std::runtime_error("stop test receive completed unexpectedly");
    if (!::ControlService(service.get(), SERVICE_CONTROL_STOP, &status))
      throw std::system_error(static_cast<int>(::GetLastError()),
                              std::system_category(),
                              "notification RPC service stop failed");

    // An open device handle pins a legacy WDM driver while the service stop is
    // pending. Cancel and reap the receive before closing the client so the
    // I/O manager can enter DriverUnload and the server can finish rundown.
    (void)receive.cancel();
    bool receive_failed = false;
    try {
      (void)receive.get();
    } catch (const std::system_error &) {
      // Cleanup must fail the pending receive instead of leaking it.
      receive_failed = true;
    }
    if (!receive_failed)
      throw std::runtime_error("server stop completed a receive successfully");
  }

  if (!wait_for_service_state(service.get(), SERVICE_STOPPED, 15000))
    throw std::system_error(static_cast<int>(::GetLastError()),
                            std::system_category(),
                            "notification RPC service did not stop");
  if (!::StartServiceW(service.get(), 0, nullptr) &&
      ::GetLastError() != ERROR_SERVICE_ALREADY_RUNNING)
    throw std::system_error(static_cast<int>(::GetLastError()),
                            std::system_category(),
                            "notification RPC service restart failed");
  if (!wait_for_service_state(service.get(), SERVICE_RUNNING, 15000) ||
      !wait_for_endpoint(5000))
    throw std::runtime_error(
        "notification RPC endpoint did not recover after restart");

  auto restarted = open_client();
  auto receive =
      restarted.receive_async(crtsys_rpc_notifications::progress);
  if (!restarted.invoke(crtsys_rpc_notifications::publish, 300u, 3u))
    throw std::runtime_error("restarted endpoint did not publish");
  validate(receive.get(), 300u, 3u);
  if (restarted.invoke(crtsys_rpc_notifications::publish_at_dispatch, 301u) !=
      crtsys_rpc_notifications::invalid_device_state)
    throw std::runtime_error("restarted endpoint lost its IRQL contract");
}

} // namespace

int wmain(int argc, wchar_t **argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::setvbuf(stderr, nullptr, _IONBF, 0);

  try {
    test_unbuffered_delivery();
    test_typed_receive();
    test_synchronous_receive();
    test_timeout_and_cancel();
    test_scope_cleanup();
    test_fifo_and_pending_limit();
    test_irql_contract();

    if (argc > 1)
      test_pending_stop(argv[1]);

    auto client = open_client();
    const auto stats =
        client.invoke(crtsys_rpc_notifications::query_stats);
    if (stats.delivered == 0 || stats.dropped == 0)
      throw std::runtime_error("notification counters were not exercised");

    std::printf("RPC notifications PASS: delivered=%lu dropped=%lu "
                "cross_bitness_safe=1 service_restart=%u\n",
                static_cast<unsigned long>(stats.delivered),
                static_cast<unsigned long>(stats.dropped), argc > 1 ? 1u : 0u);
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "RPC notification failure: %s\n", error.what());
    return 1;
  }
}
