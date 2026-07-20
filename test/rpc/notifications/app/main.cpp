#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#define WIN32_NO_STATUS
#include <windows.h>
#undef WIN32_NO_STATUS
#include <winternl.h>
#include <ntstatus.h>

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
                          ntl::rpc::transport_features::asynchronous_calls |
                          ntl::rpc::transport_features::kernel_notifications |
                          ntl::rpc::transport_features::reliable_notifications |
                          ntl::rpc::transport_features::
                              notification_persistence_hooks)
      .capabilities(crtsys_rpc_notifications::capabilities::current)
      .method(crtsys_rpc_notifications::publish)
      .method(crtsys_rpc_notifications::publish_at_dispatch)
      .method(crtsys_rpc_notifications::query_stats)
      .method(crtsys_rpc_notifications::publish_reliable)
      .method(crtsys_rpc_notifications::hold_session);
  return value;
}

ntl::rpc::client open_client() {
  ntl::rpc::client client(endpoint_name);
  if (!client)
    throw std::runtime_error("could not open notification RPC endpoint");
  try {
    (void)client.require_contract(requirements());
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string("contract query: ") + error.what());
  }
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
  if (status != STATUS_INVALID_DEVICE_STATE)
    throw std::runtime_error("notification accepted DISPATCH_LEVEL payload");
  if (receive.ready())
    throw std::runtime_error("rejected high-IRQL publish consumed a receive");
  if (!client.invoke(crtsys_rpc_notifications::publish, 200u, 2u))
    throw std::runtime_error("PASSIVE_LEVEL retry did not publish");
  validate(receive.get(), 200u, 2u);
}

template <typename Callback>
void run_test(const char *name, Callback &&callback) {
  std::printf("[RPC notifications] RUN  %s\n", name);
  try {
    callback();
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string(name) + ": " + error.what());
  }
  std::printf("[RPC notifications] PASS %s\n", name);
}

void test_reliable_session_delivery() {
  ntl::rpc::session_token token{};
  std::uint64_t session_id = 0;
  std::uint64_t first_delivery_sequence = 0;
  {
    auto client = open_client();
    const auto session = client.start_session();
    token = session.token;
    session_id = session.id;
    client.subscribe(crtsys_rpc_notifications::progress);

    if (client.invoke(crtsys_rpc_notifications::publish_reliable, 500u, 3u) !=
        0)
      throw std::runtime_error("reliable notification was not queued");
    const auto delivery =
        client.receive_reliable(crtsys_rpc_notifications::progress);
    if (delivery.sequence() == 0)
      throw std::runtime_error("reliable notification had no ACK sequence");
    first_delivery_sequence = delivery.sequence();
    validate(delivery.payload(), 500u, 3u);
    // Do not ACK. Closing this handle must retain and replay the delivery.
  }

  auto client = open_client();
  const auto resumed = client.resume_session(token);
  if (resumed.id != session_id)
    throw std::runtime_error("in-memory session identity changed on resume");
  const auto replay =
      client.receive_reliable(crtsys_rpc_notifications::progress);
  if (replay.sequence() != first_delivery_sequence)
    throw std::runtime_error("unacknowledged notification was not replayed");
  validate(replay.payload(), 500u, 3u);
  client.acknowledge(crtsys_rpc_notifications::progress, replay);

  auto empty =
      client.receive_reliable_async(crtsys_rpc_notifications::progress);
  if (empty.wait_for(2ms) != ntl::rpc::async_wait_status::timeout) {
    try {
      const auto duplicate = empty.get();
      throw std::runtime_error(
          "ACKed notification was replayed with sequence " +
          std::to_string(duplicate.sequence()));
    } catch (const std::system_error &error) {
      throw std::runtime_error(
          "empty reliable receive completed with error " +
          std::to_string(error.code().value()) + ": " + error.what());
    }
  }
  if (!empty.cancel())
    throw std::runtime_error("reliable receive cancellation was not issued");
  try {
    (void)empty.get();
    throw std::runtime_error("cancelled reliable receive returned a payload");
  } catch (const std::system_error &error) {
    if (error.code().value() != ERROR_OPERATION_ABORTED)
      throw;
  }

  // The endpoint limit is four queued records per session. A fifth publish
  // must apply bounded backpressure until the client ACKs one delivery.
  for (std::uint64_t index = 0; index != 4; ++index) {
    if (client.invoke(crtsys_rpc_notifications::publish_reliable,
                      600u + index, 2u) != 0)
      throw std::runtime_error("reliable FIFO publish failed");
  }
  if (client.invoke(crtsys_rpc_notifications::publish_reliable, 700u, 1u) !=
      STATUS_DEVICE_BUSY)
    throw std::runtime_error("reliable queue backpressure was not enforced");

  for (std::uint64_t index = 0; index != 4; ++index) {
    const auto delivery =
        client.receive_reliable(crtsys_rpc_notifications::progress);
    validate(delivery.payload(), 600u + index, 2u);
    client.acknowledge(crtsys_rpc_notifications::progress, delivery);
  }
  if (client.invoke(crtsys_rpc_notifications::publish_reliable, 701u, 1u) !=
      0)
    throw std::runtime_error("ACK did not release reliable queue capacity");
  const auto released =
      client.receive_reliable(crtsys_rpc_notifications::progress);
  validate(released.payload(), 701u, 1u);
  client.acknowledge(crtsys_rpc_notifications::progress, released);

  auto cancelled =
      client.receive_reliable_async(crtsys_rpc_notifications::progress);
  client.unsubscribe(crtsys_rpc_notifications::progress);
  try {
    (void)cancelled.get();
    throw std::runtime_error("unsubscribe did not cancel a pending receive");
  } catch (const std::system_error &error) {
    if (error.code().value() != ERROR_OPERATION_ABORTED)
      throw;
  }
  if (client.invoke(crtsys_rpc_notifications::publish_reliable, 702u, 1u) !=
      STATUS_NOT_FOUND)
    throw std::runtime_error("unsubscribed session accepted a notification");

  client.close_session();
  auto closed = open_client();
  bool resume_rejected = false;
  try {
    (void)closed.resume_session(token);
  } catch (const std::system_error &) {
    resume_rejected = true;
  }
  if (!resume_rejected)
    throw std::runtime_error("explicitly closed session was resumed");
}

void test_persisted_session_restore() {
  ntl::rpc::session_token token{};
  {
    auto client = open_client();
    token = client.start_session().token;
    client.subscribe(crtsys_rpc_notifications::progress);
    if (client.invoke(crtsys_rpc_notifications::publish_reliable, 800u, 5u) !=
        0)
      throw std::runtime_error("persisted notification was not queued");
  }

  // The server's in-memory retention is 500 ms. Resume after it expires to
  // force notification_store::restore rather than the in-memory fast path.
  ::Sleep(650);
  auto restored = open_client();
  (void)restored.resume_session(token);
  const auto delivery =
      restored.receive_reliable(crtsys_rpc_notifications::progress);
  validate(delivery.payload(), 800u, 5u);
  restored.acknowledge(crtsys_rpc_notifications::progress, delivery);
  restored.close_session();
}

void test_close_waits_for_session_call() {
  auto client = open_client();
  const auto session = client.start_session();
  auto pending = client.invoke_async(crtsys_rpc_notifications::hold_session,
                                     std::uint32_t{100});
  ::Sleep(10);
  client.close_session();
  if (!pending.ready())
    throw std::runtime_error(
        "session close returned while its RPC callback was still active");
  if (pending.get() != session.id)
    throw std::runtime_error("active RPC lost its client session during close");
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
      STATUS_INVALID_DEVICE_STATE)
    throw std::runtime_error("restarted endpoint lost its IRQL contract");
}

} // namespace

int wmain(int argc, wchar_t **argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::setvbuf(stderr, nullptr, _IONBF, 0);

  try {
    run_test("unbuffered delivery", test_unbuffered_delivery);
    run_test("typed receive", test_typed_receive);
    run_test("synchronous receive", test_synchronous_receive);
    run_test("timeout and cancel", test_timeout_and_cancel);
    run_test("scope cleanup", test_scope_cleanup);
    run_test("FIFO and pending limit", test_fifo_and_pending_limit);
    run_test("IRQL contract", test_irql_contract);
    run_test("reliable session delivery", test_reliable_session_delivery);
    run_test("persisted session restore", test_persisted_session_restore);
    run_test("session close rundown", test_close_waits_for_session_call);

    rpc_notification_stats stats{};
    {
      auto client = open_client();
      stats = client.invoke(crtsys_rpc_notifications::query_stats);
      if (stats.delivered == 0 || stats.dropped == 0 ||
          stats.sessions_opened == 0 || stats.sessions_resumed == 0 ||
          stats.sessions_disconnected == 0 || stats.sessions_closed == 0 ||
          stats.persisted < 7 || stats.acknowledged < 7)
        throw std::runtime_error("notification counters were not exercised");
    }

    if (argc > 1) {
      run_test("pending receive service restart",
               [service_name = argv[1]] { test_pending_stop(service_name); });
    }

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
