#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <ntl/rpc/client>

#include "rpc_lifecycle_stress.hpp"

namespace {

constexpr wchar_t endpoint_name[] = L"crtsys_rpc_lifecycle_stress";

class service_handle {
public:
  explicit service_handle(SC_HANDLE value = nullptr) noexcept : value_(value) {}
  service_handle(const service_handle &) = delete;
  service_handle &operator=(const service_handle &) = delete;
  ~service_handle() {
    if (value_)
      CloseServiceHandle(value_);
  }

  SC_HANDLE get() const noexcept { return value_; }
  explicit operator bool() const noexcept { return value_ != nullptr; }

private:
  SC_HANDLE value_;
};

unsigned parse_unsigned(int argc, wchar_t **argv, int index,
                        unsigned fallback, unsigned maximum,
                        const wchar_t *name) {
  if (argc <= index)
    return fallback;

  wchar_t *end = nullptr;
  const auto value = std::wcstoul(argv[index], &end, 10);
  if (!end || *end != L'\0' || value == 0 || value > maximum) {
    std::fwprintf(stderr, L"invalid %ls: %ls\n", name, argv[index]);
    std::exit(2);
  }
  return static_cast<unsigned>(value);
}

ntl::rpc::contract_requirements contract_requirements() {
  ntl::rpc::contract_requirements requirements;
  requirements
      .contract_version(crtsys_rpc_lifecycle_stress::contract_version)
      .transport_features(ntl::rpc::transport_features::current)
      .capabilities(crtsys_rpc_lifecycle_stress::capabilities::current)
      .method(crtsys_rpc_lifecycle_stress::ping)
      .method(crtsys_rpc_lifecycle_stress::checksum)
      .method(crtsys_rpc_lifecycle_stress::slow_call)
      .method(crtsys_rpc_lifecycle_stress::query_stats);
  return requirements;
}

bool validate_client(ntl::rpc::client &client) {
  if (!client)
    return false;
  (void)client.require_contract(contract_requirements());
  return true;
}

bool run_handle_churn(unsigned iterations, unsigned worker_count) {
  std::atomic<bool> start{false};
  std::atomic<bool> failed{false};
  std::mutex failure_lock;
  std::string failure;
  std::vector<std::thread> workers;
  workers.reserve(worker_count);

  for (unsigned worker = 0; worker != worker_count; ++worker) {
    workers.emplace_back([&, worker] {
      while (!start.load(std::memory_order_acquire))
        std::this_thread::yield();

      try {
        for (unsigned iteration = 0; iteration != iterations; ++iteration) {
          if (failed.load(std::memory_order_relaxed))
            return;

          ntl::rpc::client client(endpoint_name);
          if (!validate_client(client))
            throw std::runtime_error("could not open RPC endpoint");

          const std::uint64_t value =
              (static_cast<std::uint64_t>(worker) << 32) | iteration;
          if (client.invoke(crtsys_rpc_lifecycle_stress::ping, value) !=
              (value ^ crtsys_rpc_lifecycle_stress::ping_mask))
            throw std::runtime_error("ping result mismatch");

          const std::vector<std::uint32_t> values{
              worker, iteration, worker + iteration, 0x10203040u};
          const auto expected = std::accumulate(
              values.begin(), values.end(), std::uint64_t{0});
          if (client.invoke(crtsys_rpc_lifecycle_stress::checksum, values) !=
              expected)
            throw std::runtime_error("checksum result mismatch");
        }
      } catch (const std::exception &error) {
        {
          std::lock_guard<std::mutex> guard(failure_lock);
          if (failure.empty())
            failure = error.what();
        }
        failed.store(true, std::memory_order_relaxed);
      }
    });
  }

  start.store(true, std::memory_order_release);
  for (auto &worker : workers)
    worker.join();

  if (failed.load(std::memory_order_relaxed)) {
    std::fprintf(stderr, "RPC handle churn failed: %s\n", failure.c_str());
    return false;
  }

  ntl::rpc::client client(endpoint_name);
  if (!validate_client(client))
    return false;
  const auto stats = client.invoke(crtsys_rpc_lifecycle_stress::query_stats);
  const auto expected =
      static_cast<std::uint64_t>(iterations) * worker_count * 2;
  if (stats.completed_calls != expected || stats.active_calls != 0) {
    std::fprintf(stderr,
                 "RPC handle churn stats mismatch: completed=%llu/%llu "
                 "active=%u\n",
                 static_cast<unsigned long long>(stats.completed_calls),
                 static_cast<unsigned long long>(expected), stats.active_calls);
    return false;
  }

  std::printf("RPC handle churn PASS: opens=%llu calls=%llu\n",
              static_cast<unsigned long long>(iterations) * worker_count,
              static_cast<unsigned long long>(expected));
  return true;
}

bool wait_for_service_state(SC_HANDLE service, DWORD expected_state,
                            DWORD timeout_ms) {
  const ULONGLONG deadline = GetTickCount64() + timeout_ms;
  for (;;) {
    SERVICE_STATUS_PROCESS status{};
    DWORD bytes = 0;
    if (!QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO,
                              reinterpret_cast<BYTE *>(&status),
                              sizeof(status), &bytes))
      return false;
    if (status.dwCurrentState == expected_state)
      return true;
    if (GetTickCount64() >= deadline) {
      SetLastError(ERROR_TIMEOUT);
      return false;
    }
    Sleep(50);
  }
}

bool wait_for_active_call(DWORD timeout_ms) {
  const ULONGLONG deadline = GetTickCount64() + timeout_ms;
  while (GetTickCount64() < deadline) {
    try {
      ntl::rpc::client client(endpoint_name);
      if (validate_client(client) &&
          client.invoke(crtsys_rpc_lifecycle_stress::query_stats)
                  .active_calls != 0)
        return true;
    } catch (...) {
    }
    Sleep(10);
  }
  return false;
}

bool wait_for_endpoint(DWORD timeout_ms) {
  const ULONGLONG deadline = GetTickCount64() + timeout_ms;
  while (GetTickCount64() < deadline) {
    try {
      ntl::rpc::client client(endpoint_name);
      if (validate_client(client))
        return true;
    } catch (...) {
    }
    Sleep(50);
  }
  return false;
}

bool run_in_flight_stop(const wchar_t *service_name, unsigned slow_ms) {
  std::atomic<bool> slow_succeeded{false};
  std::thread worker([&] {
    try {
      ntl::rpc::client client(endpoint_name);
      if (!validate_client(client))
        return;
      slow_succeeded.store(
          client.invoke(crtsys_rpc_lifecycle_stress::slow_call, slow_ms) ==
              slow_ms,
          std::memory_order_release);
    } catch (...) {
    }
  });

  if (!wait_for_active_call(5000)) {
    worker.join();
    std::fprintf(stderr, "RPC slow callback did not become active\n");
    return false;
  }

  service_handle manager(
      OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
  service_handle service(
      manager ? OpenServiceW(manager.get(), service_name,
                             SERVICE_STOP | SERVICE_START |
                                 SERVICE_QUERY_STATUS)
              : nullptr);
  if (!manager || !service) {
    const DWORD error = GetLastError();
    worker.join();
    std::fprintf(stderr, "OpenServiceW failed: %lu\n", error);
    return false;
  }

  SERVICE_STATUS status{};
  const BOOL stop_started =
      ControlService(service.get(), SERVICE_CONTROL_STOP, &status);
  const DWORD stop_error = stop_started ? ERROR_SUCCESS : GetLastError();
  const bool stopped =
      stop_started && wait_for_service_state(service.get(), SERVICE_STOPPED,
                                             slow_ms + 15000);
  const DWORD wait_error = stopped ? ERROR_SUCCESS : GetLastError();
  worker.join();

  if (!stopped || !slow_succeeded.load(std::memory_order_acquire)) {
    std::fprintf(stderr,
                 "RPC in-flight stop failed: stop=%u stop_error=%lu "
                 "wait_error=%lu slow=%u\n",
                 stop_started ? 1u : 0u, stop_error, wait_error,
                 slow_succeeded.load(std::memory_order_relaxed) ? 1u : 0u);
    return false;
  }

  if (!StartServiceW(service.get(), 0, nullptr) &&
      GetLastError() != ERROR_SERVICE_ALREADY_RUNNING) {
    std::fprintf(stderr, "StartServiceW failed: %lu\n", GetLastError());
    return false;
  }
  if (!wait_for_service_state(service.get(), SERVICE_RUNNING, 15000) ||
      !wait_for_endpoint(5000)) {
    std::fprintf(stderr, "RPC endpoint did not recover after restart: %lu\n",
                 GetLastError());
    return false;
  }

  try {
    ntl::rpc::client client(endpoint_name);
    if (!validate_client(client) ||
        client.invoke(crtsys_rpc_lifecycle_stress::ping, 7) !=
            (7 ^ crtsys_rpc_lifecycle_stress::ping_mask))
      return false;
  } catch (...) {
    return false;
  }

  std::printf("RPC in-flight stop PASS: delay_ms=%u restart=1\n", slow_ms);
  return true;
}

} // namespace

int wmain(int argc, wchar_t **argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::setvbuf(stderr, nullptr, _IONBF, 0);

  const unsigned iterations =
      parse_unsigned(argc, argv, 1, 128, 100000, L"iterations");
  const unsigned workers =
      parse_unsigned(argc, argv, 2, 8, 64, L"workers");
  const wchar_t *const service_name = argc > 3 ? argv[3] : nullptr;
  const unsigned slow_ms =
      parse_unsigned(argc, argv, 4, 1500, 60000, L"slow milliseconds");

  try {
    if (!run_handle_churn(iterations, workers))
      return 1;
    if (service_name && !run_in_flight_stop(service_name, slow_ms))
      return 1;

    std::printf("RPC lifecycle stress PASS: iterations=%u workers=%u "
                "service_restart=%u\n",
                iterations, workers, service_name ? 1u : 0u);
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "RPC lifecycle stress failure: %s\n", error.what());
    return 1;
  }
}
