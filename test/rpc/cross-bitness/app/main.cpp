#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <limits>
#include <string>
#include <thread>
#include <vector>

#include <ntl/rpc/client>

#include "rpc_cross_bitness.hpp"

namespace {

rpc_cross_bitness_payload make_boundary_payload() {
  rpc_cross_bitness_payload value{};
  value.unsigned8 = std::numeric_limits<std::uint8_t>::max();
  value.signed8 = std::numeric_limits<std::int8_t>::min();
  value.unsigned16 = std::numeric_limits<std::uint16_t>::max();
  value.signed16 = std::numeric_limits<std::int16_t>::min();
  value.unsigned32 = std::numeric_limits<std::uint32_t>::max();
  value.signed32 = std::numeric_limits<std::int32_t>::min();
  value.unsigned64 = std::numeric_limits<std::uint64_t>::max();
  value.signed64 = std::numeric_limits<std::int64_t>::min();
  value.float32 = 1.5f;
  value.float64 = -2.25;
  value.boolean = true;
  value.mode = rpc_wire_mode::boundary_values;
  value.text = std::string("A\0B", 3);
  value.wide_text = std::wstring(L"W\0Z", 3);
  value.bytes = {0, 1, 0x7F, 0x80, 0xFF};
  value.numbers = {0, 1, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu};
  value.words = {"", "alpha", std::string("x\0y", 3)};
  value.matrix = {{}, {0, 1, 0xFFFFu}, {7, 8, 9, 10}};
  value.nested = {{-1, "negative", std::uint64_t{0}},
                  {42, "answer", std::uint64_t{0xFEDCBA9876543210ull}},
                  {7, "missing", std::nullopt}};
  value.fixed = {0, 0x0123456789ABCDEFull, 0xFFFFFFFFFFFFFFFFull};
  value.list_values = {std::numeric_limits<std::int32_t>::min(), -1, 0, 1,
                       std::numeric_limits<std::int32_t>::max()};
  value.deque_values = {"front", std::string("middle\0value", 12), "back"};
  value.set_values = {std::numeric_limits<std::int64_t>::min(), -1, 0, 1,
                      std::numeric_limits<std::int64_t>::max()};
  value.multiset_values = {-1, -1, 0, 1, 1};
  value.unordered_set_values = {0, 1, 0xFEDCBA9876543210ull};
  value.ordered_values = {{-1, "minus"}, {0, "zero"}, {1, "one"}};
  value.multimap_values = {{0, "zero-a"}, {0, "zero-b"}, {1, "one"}};
  value.unordered_values = {{0, L"zero"}, {1, L"one"}, {0xFFFFFFFFu, L"max"}};
  value.pair_value = {std::numeric_limits<std::int64_t>::min(),
                      std::numeric_limits<std::uint64_t>::max()};
  value.tuple_value = {std::uint8_t{0xFFu}, std::uint16_t{0xFFFFu},
                       std::uint32_t{0xFFFFFFFFu},
                       std::uint64_t{0xFFFFFFFFFFFFFFFFull}};
  value.optional_value = std::string("present\0value", 13);
  value.empty_optional = std::nullopt;
  value.variant_number = std::numeric_limits<std::int64_t>::min();
  value.variant_text = std::string("variant\0text", 12);
  value.portable_size = 0xFEDCBA9876543210ull;
  value.portable_difference = std::numeric_limits<std::int64_t>::min();
  return value;
}

bool expect_rejected_raw_request(std::uint64_t function,
                                 const void *input,
                                 DWORD input_size) {
  HANDLE device = CreateFileW(
      L"\\\\?\\Global\\GLOBALROOT\\Device\\crtsys_rpc_cross_bitness",
      GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
  if (device == INVALID_HANDLE_VALUE)
    return false;

  DWORD returned = 0;
  const BOOL accepted = DeviceIoControl(
      device, ntl::rpc::control_code(static_cast<unsigned long>(function)),
      const_cast<void *>(input),
      input_size, nullptr, 0, &returned, nullptr);
  CloseHandle(device);
  return accepted == FALSE;
}

bool expect_restricted_open_denied() {
  HANDLE process_token = nullptr;
  HANDLE restricted_token = nullptr;
  HANDLE impersonation_token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY,
                        &process_token))
    return false;

  BYTE world_buffer[SECURITY_MAX_SID_SIZE]{};
  DWORD world_size = sizeof(world_buffer);
  SID_AND_ATTRIBUTES restricted[1]{};
  bool result = false;

  if (CreateWellKnownSid(WinWorldSid, nullptr, world_buffer, &world_size)) {
    restricted[0].Sid = world_buffer;
    if (CreateRestrictedToken(process_token, DISABLE_MAX_PRIVILEGE, 0,
                              nullptr, 0, nullptr, 1, restricted,
                              &restricted_token) &&
        DuplicateTokenEx(restricted_token, TOKEN_IMPERSONATE | TOKEN_QUERY,
                         nullptr, SecurityImpersonation, TokenImpersonation,
                         &impersonation_token) &&
        SetThreadToken(nullptr, impersonation_token)) {
      HANDLE device = CreateFileW(
          L"\\\\?\\Global\\GLOBALROOT\\Device\\crtsys_rpc_cross_bitness",
          GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
      const DWORD error = GetLastError();
      if (device != INVALID_HANDLE_VALUE)
        CloseHandle(device);
      result = device == INVALID_HANDLE_VALUE && error == ERROR_ACCESS_DENIED;
      RevertToSelf();
    }
  }

  if (impersonation_token)
    CloseHandle(impersonation_token);
  if (restricted_token)
    CloseHandle(restricted_token);
  CloseHandle(process_token);
  return result;
}

bool expect_contract_mismatch(
    const ntl::rpc::client &client,
    const ntl::rpc::contract_requirements &requirements,
    ntl::rpc::contract_mismatch_reason expected_reason) {
  try {
    (void)client.require_contract(requirements);
  } catch (const ntl::rpc::contract_mismatch &error) {
    return error.reason() == expected_reason;
  } catch (...) {
  }
  return false;
}

} // namespace

int wmain() {
  try {
    ntl::rpc::client client(L"crtsys_rpc_cross_bitness");
    if (!client || !expect_restricted_open_denied()) {
      std::fwprintf(stderr, L"RPC endpoint security policy failed\n");
      return 1;
    }

    ntl::rpc::contract_requirements requirements;
    requirements
        .contract_version(crtsys_rpc_cross_bitness::contract_version)
        .transport_features(ntl::rpc::transport_features::current)
        .capabilities(crtsys_rpc_cross_bitness::capabilities::current)
        .method(crtsys_rpc_cross_bitness::architecture)
        .method(crtsys_rpc_cross_bitness::echo)
        .method(crtsys_rpc_cross_bitness::narrow_reply)
        .method(crtsys_rpc_cross_bitness::vector_size)
        .method(crtsys_rpc_cross_bitness::echo_count)
        .method(crtsys_rpc_cross_bitness::guarded_words)
        .method(crtsys_rpc_cross_bitness::guarded_count)
        .method(crtsys_rpc_cross_bitness::concurrent_increment)
        .method(crtsys_rpc_cross_bitness::concurrent_count)
        .method(crtsys_rpc_cross_bitness::slow_call)
        .method(crtsys_rpc_cross_bitness::request_stop)
        .method(crtsys_rpc_cross_bitness::slow_active)
        .method(crtsys_rpc_cross_bitness::fill_shared_ring)
        .method(crtsys_rpc_cross_bitness::consume_shared_ring);
    const auto contract = client.require_contract(requirements);
    if (contract.method_ids().size() != 14 ||
        !std::is_sorted(contract.method_ids().begin(),
                        contract.method_ids().end())) {
      std::fwprintf(stderr, L"RPC contract method table is invalid\n");
      return 1;
    }

    ntl::rpc::contract_requirements wrong_version;
    wrong_version.contract_version(
        crtsys_rpc_cross_bitness::contract_version + 1);
    ntl::rpc::contract_requirements missing_capability;
    missing_capability.capabilities(1ull << 63);
    ntl::rpc::contract_requirements missing_method;
    missing_method.method(crtsys_rpc_cross_bitness::unavailable_method);
    ntl::rpc::contract_requirements missing_transport_feature;
    missing_transport_feature.transport_features(1ull << 63);
    if (!expect_contract_mismatch(
            client, wrong_version,
            ntl::rpc::contract_mismatch_reason::contract_version) ||
        !expect_contract_mismatch(
            client, missing_capability,
            ntl::rpc::contract_mismatch_reason::capabilities) ||
        !expect_contract_mismatch(
            client, missing_method,
            ntl::rpc::contract_mismatch_reason::method) ||
        !expect_contract_mismatch(
            client, missing_transport_feature,
            ntl::rpc::contract_mismatch_reason::transport_features)) {
      std::fwprintf(stderr, L"RPC contract mismatch was not diagnosed\n");
      return 1;
    }

    const auto server =
        client.invoke(crtsys_rpc_cross_bitness::architecture);
    if (server.pointer_bits != 64 || server.size_bits != 64 ||
        server.difference_bits != 64 || server.wchar_bits != 16) {
      std::fwprintf(stderr,
                    L"architecture mismatch: client_ptr=%zu server_ptr=%u "
                    L"client_size=%zu server_size=%u\n",
                    sizeof(void *) * 8, server.pointer_bits,
                    sizeof(std::size_t) * 8, server.size_bits);
      return 1;
    }

    const auto boundary = make_boundary_payload();

    bool undersized_response_rejected = false;
    try {
      constexpr auto undersized_echo =
          ntl::rpc::method<0xA02,
                           rpc_cross_bitness_payload(
                               const rpc_cross_bitness_payload &),
                           1>{};
      (void)client.invoke(undersized_echo, boundary);
    } catch (const std::exception &) {
      undersized_response_rejected = true;
    }
    if (!undersized_response_rejected ||
        client.invoke(crtsys_rpc_cross_bitness::echo_count) != 0) {
      std::fwprintf(stderr,
                    L"undersized response executed the server callback\n");
      return 1;
    }

    const auto echoed =
        client.invoke(crtsys_rpc_cross_bitness::echo, boundary);
    if (!(echoed == boundary)) {
      std::fwprintf(stderr, L"boundary payload mismatch\n");
      return 1;
    }

    rpc_cross_bitness_payload empty{};
    const auto empty_echoed =
        client.invoke(crtsys_rpc_cross_bitness::echo, empty);
    if (!(empty_echoed == empty)) {
      std::fwprintf(stderr, L"empty payload mismatch\n");
      return 1;
    }

    auto large = boundary;
    large.bytes.resize(128 * 1024);
    large.numbers.resize(32 * 1024);
    for (std::size_t index = 0; index < large.bytes.size(); ++index)
      large.bytes[index] = static_cast<std::uint8_t>(index);
    for (std::size_t index = 0; index < large.numbers.size(); ++index)
      large.numbers[index] = static_cast<std::uint32_t>(index * 2654435761u);

    const auto large_echoed =
        client.invoke(crtsys_rpc_cross_bitness::echo, large);
    if (!(large_echoed == large)) {
      std::fwprintf(stderr, L"large payload mismatch\n");
      return 1;
    }

    bool truncated_reply_rejected = false;
    try {
      constexpr auto mismatched_narrow_reply =
          ntl::rpc::method<0xA03, std::uint64_t()>{};
      (void)client.invoke(mismatched_narrow_reply);
    } catch (const std::exception &) {
      truncated_reply_rejected = true;
    }
    if (!truncated_reply_rejected) {
      std::fwprintf(stderr, L"truncated reply was accepted\n");
      return 1;
    }

    const auto vector_size =
        client.invoke(crtsys_rpc_cross_bitness::vector_size,
                      std::vector<std::uint8_t>{1, 2, 3, 4});
    if (vector_size != 4) {
      std::fwprintf(stderr, L"valid vector request failed\n");
      return 1;
    }

    if (client.invoke(crtsys_rpc_cross_bitness::guarded_words,
                      std::vector<std::string>{"alpha", "beta"}) != 2 ||
        client.invoke(crtsys_rpc_cross_bitness::guarded_count) != 1) {
      std::fwprintf(stderr, L"valid guarded request failed\n");
      return 1;
    }

    const std::uint8_t malformed = 0xFF;
    const std::uint32_t oversized_count = 0xFFFFFFFFu;
    const std::uint32_t allocation_attack_count = 0x1000u;
    const std::array<std::uint8_t, 256> oversized_request{};
    if (!expect_rejected_raw_request(crtsys_rpc_cross_bitness::echo.id(),
                                     &malformed, sizeof(malformed)) ||
        !expect_rejected_raw_request(
            crtsys_rpc_cross_bitness::vector_size.id(), &oversized_count,
            sizeof(oversized_count)) ||
        !expect_rejected_raw_request(
            crtsys_rpc_cross_bitness::guarded_words.id(),
            &allocation_attack_count, sizeof(allocation_attack_count)) ||
        !expect_rejected_raw_request(
            crtsys_rpc_cross_bitness::guarded_words.id(),
            oversized_request.data(),
            static_cast<DWORD>(oversized_request.size())) ||
        !expect_rejected_raw_request(0xAFF, nullptr, 0)) {
      std::fwprintf(stderr, L"malformed RPC request was accepted\n");
      return 1;
    }
    if (client.invoke(crtsys_rpc_cross_bitness::guarded_count) != 1) {
      std::fwprintf(stderr, L"rejected request executed its callback\n");
      return 1;
    }

    const auto final_echo =
        client.invoke(crtsys_rpc_cross_bitness::echo, boundary);
    if (!(final_echo == boundary)) {
      std::fwprintf(stderr, L"RPC endpoint did not recover after rejection\n");
      return 1;
    }

    const auto session = client.start_session();
    if (!session.valid()) {
      std::fwprintf(stderr, L"RPC shared-memory session creation failed\n");
      return 1;
    }
    {
      auto region = client.register_shared_region(
          rpc_shared_ring::required_bytes(),
          ntl::ipc::region_access::driver_read_write);
      rpc_shared_ring ring;
      if (rpc_shared_ring::initialize(region.data(), region.size(), ring, 7) !=
          ntl::ipc::validation_status::success) {
        std::fwprintf(stderr, L"RPC shared ring initialization failed\n");
        return 1;
      }

      const auto token = region.token(0, rpc_shared_ring::required_bytes());
      const auto written = client.invoke(
          crtsys_rpc_cross_bitness::fill_shared_ring, token,
          std::uint32_t{100}, std::uint32_t{10});
      if (written != 8 || ring.readable() != 8) {
        std::fwprintf(stderr, L"RPC shared ring backpressure failed\n");
        return 1;
      }

      for (std::uint32_t index = 0; index < written; ++index) {
        rpc_shared_ring_record record{};
        if (!ring.try_read(record) || record.sequence != 100 + index ||
            record.source != 0x44525652u ||
            record.checksum != ((100 + index) ^ 0xA5A55A5Au)) {
          std::fwprintf(stderr, L"driver-to-app shared ring data failed\n");
          return 1;
        }
      }

      std::uint64_t expected_sum = 0;
      for (std::uint32_t index = 0; index < 5; ++index) {
        const auto sequence = std::uint64_t{0x100000000ull} + index;
        const rpc_shared_ring_record record{
            sequence, 0x41505020u,
            static_cast<std::uint32_t>(sequence) ^ 0x5A5AA5A5u};
        if (!ring.try_write(record)) {
          std::fwprintf(stderr, L"app-to-driver shared ring write failed\n");
          return 1;
        }
        expected_sum += sequence;
      }
      const auto consumed_sum = client.invoke(
          crtsys_rpc_cross_bitness::consume_shared_ring, token,
          std::uint32_t{5});
      if (consumed_sum != expected_sum || ring.readable() != 0) {
        std::fwprintf(stderr, L"app-to-driver shared ring data failed\n");
        return 1;
      }
      region.close();

      bool stale_token_rejected = false;
      try {
        (void)client.invoke(crtsys_rpc_cross_bitness::fill_shared_ring, token,
                            std::uint32_t{1}, std::uint32_t{1});
      } catch (const std::exception &) {
        stale_token_rejected = true;
      }
      if (!stale_token_rejected) {
        std::fwprintf(stderr, L"stale RPC shared-memory token was accepted\n");
        return 1;
      }
    }
    client.close_session();

    constexpr std::uint32_t worker_count = 8;
    constexpr std::uint32_t calls_per_worker = 128;
    std::atomic<bool> concurrent_failed{false};
    std::vector<std::thread> workers;
    workers.reserve(worker_count);
    for (std::uint32_t worker = 0; worker < worker_count; ++worker) {
      workers.emplace_back([&] {
        try {
          ntl::rpc::client worker_client(L"crtsys_rpc_cross_bitness");
          if (!worker_client) {
            concurrent_failed.store(true);
            return;
          }
          for (std::uint32_t call = 0; call < calls_per_worker; ++call)
            (void)worker_client.invoke(
                crtsys_rpc_cross_bitness::concurrent_increment,
                std::uint32_t{1});
        } catch (...) {
          concurrent_failed.store(true);
        }
      });
    }
    for (auto &worker : workers)
      worker.join();
    const auto concurrent_total =
        client.invoke(crtsys_rpc_cross_bitness::concurrent_count);
    if (concurrent_failed.load() ||
        concurrent_total != worker_count * calls_per_worker) {
      std::fwprintf(stderr, L"concurrent RPC dispatch failed: %u\n",
                    concurrent_total);
      return 1;
    }

    std::atomic<std::uint32_t> slow_result{0};
    std::atomic<bool> slow_failed{false};
    std::thread slow_worker([&] {
      try {
        ntl::rpc::client slow_client(L"crtsys_rpc_cross_bitness");
        slow_result.store(slow_client.invoke(
            crtsys_rpc_cross_bitness::slow_call, std::uint32_t{300}));
      } catch (...) {
        slow_failed.store(true);
      }
    });

    bool slow_started = false;
    for (int attempt = 0; attempt < 100 && !slow_started; ++attempt) {
      slow_started =
          client.invoke(crtsys_rpc_cross_bitness::slow_active) != 0;
      if (!slow_started)
        Sleep(5);
    }
    if (!slow_started ||
        client.invoke(crtsys_rpc_cross_bitness::request_stop) != 1) {
      slow_worker.join();
      std::fwprintf(stderr, L"RPC rundown test could not start\n");
      return 1;
    }
    slow_worker.join();
    if (slow_failed.load() || slow_result.load() != 300) {
      std::fwprintf(stderr, L"active RPC was not drained successfully\n");
      return 1;
    }

    bool stopped = false;
    for (int attempt = 0; attempt < 100 && !stopped; ++attempt) {
      try {
        (void)client.invoke(crtsys_rpc_cross_bitness::architecture);
        Sleep(5);
      } catch (...) {
        stopped = true;
      }
    }
    if (!stopped) {
      std::fwprintf(stderr, L"RPC server accepted calls after stop\n");
      return 1;
    }

    const wchar_t *const client_arch = sizeof(void *) == 4 ? L"x86" : L"x64";
    std::wprintf(L"RPC cross-bitness PASS: client=%ls server=x64 "
                 L"boundary=1 empty=1 large_bytes=%zu large_numbers=%zu "
                 L"bounded_response=1 malformed=5 security=1 contract=4 "
                 L"shared_memory=2 stale_token=1 concurrent=%u "
                 L"rundown=1\n",
                 client_arch, large.bytes.size(), large.numbers.size(),
                 concurrent_total);
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "RPC cross-bitness failure: %s\n", error.what());
    return 1;
  }
}
