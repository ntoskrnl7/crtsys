#include <Windows.h>
#include <fltUser.h>

#define NTL_USER_MODE
#include <ntl/flt/communication_client>

#include "io_buffer_runtime.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <future>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace crtsys_flt_io_buffer_runtime_test;

std::atomic<std::uint32_t> mapped_write_requests{0};
std::atomic<std::uint32_t> mapped_read_requests{0};
std::atomic<std::uint64_t> last_mapped_address{0};
std::atomic<std::uint32_t> timeout_direction{0};
std::mutex timeout_lock;
std::condition_variable timeout_condition;
bool timeout_handler_entered = false;
bool timeout_handler_release = false;
bool timeout_handler_completed = false;

[[noreturn]] void fail(const char *operation, DWORD error = GetLastError()) {
  char message[192]{};
  std::snprintf(message, sizeof(message), "%s failed: error=%lu", operation,
                static_cast<unsigned long>(error));
  throw std::runtime_error(message);
}

void enable_load_driver_privilege() {
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(),
                        TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
    fail("OpenProcessToken");

  LUID privilege{};
  if (!LookupPrivilegeValueW(nullptr, L"SeLoadDriverPrivilege", &privilege)) {
    const DWORD error = GetLastError();
    CloseHandle(token);
    fail("LookupPrivilegeValueW", error);
  }

  TOKEN_PRIVILEGES privileges{};
  privileges.PrivilegeCount = 1;
  privileges.Privileges[0].Luid = privilege;
  privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
  SetLastError(ERROR_SUCCESS);
  const BOOL adjusted = AdjustTokenPrivileges(
      token, FALSE, &privileges, sizeof(privileges), nullptr, nullptr);
  const DWORD error = GetLastError();
  CloseHandle(token);
  if (!adjusted || error == ERROR_NOT_ALL_ASSIGNED)
    fail("AdjustTokenPrivileges", error);
}

void load_filter() {
  const HRESULT status = FilterLoad(filter_name);
  if (FAILED(status))
    fail("FilterLoad", HRESULT_CODE(status));
}

void unload_filter(bool required) {
  const HRESULT status = FilterUnload(filter_name);
  if (FAILED(status) && required)
    fail("FilterUnload", HRESULT_CODE(status));
}

ntl::flt::communication_client connect_transform_service() {
  auto client = ntl::flt::communication_client::connect(port_name);
  client.on_request(
      transform_mapped_buffer_method,
      [](ntl::ipc::mapped_buffer_descriptor descriptor,
         transform_direction direction) -> std::uint64_t {
        ntl::ipc::mapped_client_buffer mapped;
        const auto validation = ntl::ipc::mapped_client_buffer::open(
            descriptor, descriptor.generation, mapped);
        if (validation != ntl::ipc::validation_status::success ||
            !mapped.writable_data())
          throw std::runtime_error(
              "driver supplied an invalid mapped-buffer descriptor");

        last_mapped_address.store(descriptor.mapped_address,
                                  std::memory_order_release);
        if (timeout_direction.load(std::memory_order_acquire) ==
            static_cast<std::uint32_t>(direction)) {
          std::unique_lock<std::mutex> guard(timeout_lock);
          timeout_handler_entered = true;
          timeout_condition.notify_all();
          (void)timeout_condition.wait_for(guard, std::chrono::seconds(15), [] {
            return timeout_handler_release;
          });
          timeout_handler_completed = true;
          timeout_condition.notify_all();
          return descriptor.length;
        }

        auto *const bytes = mapped.writable_data();
        for (std::size_t index = 0; index != mapped.size(); ++index)
          bytes[index] ^= transform_key;

        if (direction == transform_direction::write_input)
          mapped_write_requests.fetch_add(1, std::memory_order_relaxed);
        else if (direction == transform_direction::read_output)
          mapped_read_requests.fetch_add(1, std::memory_order_relaxed);
        else
          throw std::runtime_error("unknown mapped-buffer direction");
        return static_cast<std::uint64_t>(mapped.size());
      });
  return client;
}

ntl::flt::communication_client reconnect_transform_service() {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  for (;;) {
    try {
      return connect_transform_service();
    } catch (const ntl::flt::communication_error &) {
      if (std::chrono::steady_clock::now() >= deadline)
        throw;
      std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
  }
}

void arm_timeout_handler(transform_direction direction) {
  std::lock_guard<std::mutex> guard(timeout_lock);
  timeout_handler_entered = false;
  timeout_handler_release = false;
  timeout_handler_completed = false;
  timeout_direction.store(static_cast<std::uint32_t>(direction),
                          std::memory_order_release);
}

void wait_for_timeout_handler() {
  std::unique_lock<std::mutex> guard(timeout_lock);
  if (!timeout_condition.wait_for(guard, std::chrono::seconds(5),
                                  [] { return timeout_handler_entered; }))
    throw std::runtime_error(
        "timed transform request did not reach the user handler");
}

void release_timeout_handler() {
  std::unique_lock<std::mutex> guard(timeout_lock);
  timeout_handler_release = true;
  timeout_condition.notify_all();
  if (!timeout_condition.wait_for(guard, std::chrono::seconds(5),
                                  [] { return timeout_handler_completed; }))
    throw std::runtime_error("timed transform handler did not finish");
  timeout_direction.store(0, std::memory_order_release);
}

bool address_is_unmapped(std::uint64_t address) noexcept {
  if (address == 0 ||
      address > static_cast<std::uint64_t>(
                    (std::numeric_limits<std::uintptr_t>::max)()))
    return false;
  MEMORY_BASIC_INFORMATION information{};
  if (VirtualQuery(
          reinterpret_cast<const void *>(static_cast<std::uintptr_t>(address)),
          &information, sizeof(information)) == 0)
    return true;
  return information.State == MEM_FREE;
}

void write_bytes(const std::filesystem::path &path,
                 const std::vector<std::uint8_t> &bytes,
                 DWORD creation_disposition) {
  HANDLE file =
      CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, creation_disposition,
                  FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE)
    fail("CreateFileW(write)");
  DWORD written = 0;
  const BOOL result = WriteFile(
      file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
  const DWORD error = GetLastError();
  if (!CloseHandle(file) && result)
    fail("CloseHandle(write)");
  if (!result || written != bytes.size())
    fail("WriteFile", result ? ERROR_WRITE_FAULT : error);
}

void write_all(const std::filesystem::path &path,
               const std::vector<std::uint8_t> &bytes) {
  write_bytes(path, bytes, CREATE_ALWAYS);
}

void write_existing(const std::filesystem::path &path,
                    const std::vector<std::uint8_t> &bytes) {
  write_bytes(path, bytes, OPEN_EXISTING);
}

std::vector<std::uint8_t> read_all(const std::filesystem::path &path,
                                   const char *stage = "unspecified") {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE)
    fail("CreateFileW(read)");
  LARGE_INTEGER size{};
  if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 ||
      static_cast<unsigned long long>(size.QuadPart) >
          static_cast<unsigned long long>(SIZE_MAX)) {
    const DWORD error = GetLastError();
    CloseHandle(file);
    fail("GetFileSizeEx", error);
  }
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size.QuadPart));
  DWORD read = 0;
  const BOOL result = ReadFile(
      file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr);
  const DWORD error = GetLastError();
  if (!CloseHandle(file) && result)
    fail("CloseHandle(read)");
  if (!result || read != bytes.size()) {
    char operation[128]{};
    std::snprintf(operation, sizeof(operation), "ReadFile(%s)", stage);
    fail(operation, result ? ERROR_READ_FAULT : error);
  }
  return bytes;
}

void verify_completed_read_length(
    const std::filesystem::path &path,
    const std::vector<std::uint8_t> &expected) {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE)
    fail("CreateFileW(short-read)");

  constexpr std::size_t extra_bytes = 137;
  constexpr std::uint8_t tail_sentinel = 0xd3;
  std::vector<std::uint8_t> buffer(expected.size() + extra_bytes,
                                   tail_sentinel);
  DWORD read = 0;
  const BOOL result =
      ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read,
               nullptr);
  const DWORD error = GetLastError();
  if (!CloseHandle(file) && result)
    fail("CloseHandle(short-read)");
  if (!result || read != expected.size())
    fail("ReadFile(short-read)", result ? ERROR_READ_FAULT : error);
  if (!std::equal(expected.begin(), expected.end(), buffer.begin()))
    throw std::runtime_error(
        "short read did not transform the completed prefix");
  if (!std::all_of(buffer.begin() + read, buffer.end(),
                   [](std::uint8_t value) {
                     return value == tail_sentinel;
                   }))
    throw std::runtime_error(
        "post-read copy-back modified bytes beyond IoStatus.Information");
}

void verify_zero_byte_eof_read(const std::filesystem::path &path) {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE)
    fail("CreateFileW(eof)");
  LARGE_INTEGER end{};
  if (!SetFilePointerEx(file, end, nullptr, FILE_END)) {
    const DWORD error = GetLastError();
    CloseHandle(file);
    fail("SetFilePointerEx(eof)", error);
  }
  std::uint8_t probe[64]{};
  DWORD read = 1;
  const BOOL result =
      ReadFile(file, probe, static_cast<DWORD>(sizeof(probe)), &read, nullptr);
  const DWORD error = GetLastError();
  if (!CloseHandle(file) && result)
    fail("CloseHandle(eof)");
  if (!result || read != 0)
    fail("ReadFile(eof)", result ? ERROR_READ_FAULT : error);
}

void verify_read_fails_without_service(const std::filesystem::path &path) {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE)
    fail("CreateFileW(disconnected read)");
  std::uint8_t probe[64]{};
  DWORD read = 0;
  const BOOL result =
      ReadFile(file, probe, static_cast<DWORD>(sizeof(probe)), &read, nullptr);
  const DWORD error = GetLastError();
  CloseHandle(file);
  if (result)
    fail("ReadFile unexpectedly succeeded without service", ERROR_SUCCESS);
  if (error == ERROR_SUCCESS)
    fail("ReadFile returned no disconnect error", error);
}

std::future<bool> start_expected_failure(const std::filesystem::path &path,
                                         const std::vector<std::uint8_t> &bytes,
                                         transform_direction direction) {
  return std::async(std::launch::async, [path, &bytes, direction] {
    try {
      if (direction == transform_direction::write_input)
        write_existing(path, bytes);
      else
        (void)read_all(path, "expected-failure");
      return true;
    } catch (...) {
      return false;
    }
  });
}

bool wait_for_expected_failure(std::future<bool> &operation,
                               std::chrono::seconds timeout) {
  if (operation.wait_for(timeout) != std::future_status::ready)
    return false;
  return !operation.get();
}

runtime_state wait_for_pending_gate(ntl::flt::communication_client &client,
                                    transform_direction direction) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  for (;;) {
    const auto state = client.invoke(query_runtime_state_method);
    const bool pre = direction == transform_direction::write_input;
    const auto active =
        pre ? state.active_pre_requests : state.active_post_requests;
    const auto waiters =
        pre ? state.waiting_pre_requests : state.waiting_post_requests;
    const auto address =
        pre ? state.waiting_pre_address : state.waiting_post_address;
    if (active != 0 && waiters != 0 && address != 0)
      return state;
    if (std::chrono::steady_clock::now() >= deadline)
      throw std::runtime_error(
          "pending transform did not reach its kernel test gate");
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}

runtime_state wait_for_idle(ntl::flt::communication_client &client) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  for (;;) {
    const auto state = client.invoke(query_runtime_state_method);
    if (state.active_pre_requests == 0 && state.active_post_requests == 0 &&
        state.waiting_pre_requests == 0 && state.waiting_post_requests == 0)
      return state;
    if (std::chrono::steady_clock::now() >= deadline)
      throw std::runtime_error(
          "normal transforms did not drain their pending registries");
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}

runtime_state wait_for_registry_drain(ntl::flt::communication_client &client,
                                      transform_direction direction,
                                      std::uint32_t prior_cancels) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  for (;;) {
    const auto state = client.invoke(query_runtime_state_method);
    const bool pre = direction == transform_direction::write_input;
    const auto active =
        pre ? state.active_pre_requests : state.active_post_requests;
    const auto waiters =
        pre ? state.waiting_pre_requests : state.waiting_post_requests;
    const auto cancels =
        pre ? state.pending_pre_cancels : state.pending_post_cancels;
    if (active == 0 && waiters == 0 && cancels > prior_cancels)
      return state;
    if (std::chrono::steady_clock::now() >= deadline)
      throw std::runtime_error(
          "cancelled transform did not drain its pending registry");
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}

void verify_timeout_cancels_pending(ntl::flt::communication_client &client,
                                    const std::filesystem::path &path,
                                    const std::vector<std::uint8_t> &bytes,
                                    transform_direction direction) {
  const auto before = client.invoke(query_runtime_state_method);
  const auto prior_cancels = direction == transform_direction::write_input
                                 ? before.pending_pre_cancels
                                 : before.pending_post_cancels;
  arm_timeout_handler(direction);
  auto operation = start_expected_failure(path, bytes, direction);
  wait_for_timeout_handler();
  const auto address = last_mapped_address.load(std::memory_order_acquire);
  const bool failed =
      wait_for_expected_failure(operation, std::chrono::seconds(8));
  const bool unmapped = address_is_unmapped(address);
  release_timeout_handler();
  if (!failed)
    throw std::runtime_error("service timeout did not fail the pended I/O");
  if (!unmapped)
    throw std::runtime_error(
        "service timeout left the mapped replacement VAD alive");
  (void)wait_for_registry_drain(client, direction, prior_cancels);
}

void verify_disconnect_cancels_pending(ntl::flt::communication_client &client,
                                       const std::filesystem::path &path,
                                       const std::vector<std::uint8_t> &bytes,
                                       transform_direction direction) {
  const auto before = client.invoke(query_runtime_state_method);
  const auto prior_cancels = direction == transform_direction::write_input
                                 ? before.pending_pre_cancels
                                 : before.pending_post_cancels;
  if (client.invoke(configure_transform_test_method, direction,
                    transform_test_behavior::wait_for_disconnect) != 1)
    throw std::runtime_error("failed to arm disconnect test gate");

  auto operation = start_expected_failure(path, bytes, direction);
  const auto waiting = wait_for_pending_gate(client, direction);
  const auto address = direction == transform_direction::write_input
                           ? waiting.waiting_pre_address
                           : waiting.waiting_post_address;
  client = {};
  if (!wait_for_expected_failure(operation, std::chrono::seconds(8)))
    throw std::runtime_error("service disconnect did not fail the pended I/O");
  if (!address_is_unmapped(address))
    throw std::runtime_error(
        "service disconnect left the mapped replacement VAD alive");

  client = reconnect_transform_service();
  (void)wait_for_registry_drain(client, direction, prior_cancels);
}

void verify_teardown_cancels_pending(ntl::flt::communication_client &client,
                                     const std::filesystem::path &path,
                                     const std::vector<std::uint8_t> &bytes,
                                     transform_direction direction) {
  if (client.invoke(configure_transform_test_method, direction,
                    transform_test_behavior::wait_for_teardown) != 1)
    throw std::runtime_error("failed to arm teardown test gate");

  auto operation = start_expected_failure(path, bytes, direction);
  const auto waiting = wait_for_pending_gate(client, direction);
  const auto address = direction == transform_direction::write_input
                           ? waiting.waiting_pre_address
                           : waiting.waiting_post_address;
  unload_filter(true);
  if (!wait_for_expected_failure(operation, std::chrono::seconds(8)))
    throw std::runtime_error("filter teardown did not fail the pended I/O");
  client = {};
  if (!address_is_unmapped(address))
    throw std::runtime_error(
        "filter teardown left the mapped replacement VAD alive");
}

void verify_ciphertext(const std::vector<std::uint8_t> &ciphertext,
                       const std::vector<std::uint8_t> &plaintext) {
  if (ciphertext.size() != plaintext.size())
    throw std::runtime_error("raw ciphertext size mismatch");
  for (std::size_t index = 0; index != ciphertext.size(); ++index) {
    if (ciphertext[index] !=
        static_cast<std::uint8_t>(plaintext[index] ^ transform_key))
      throw std::runtime_error(
          "pre-write replacement did not persist ciphertext");
  }
}

} // namespace

int wmain() {
  namespace fs = std::filesystem;
  const fs::path path = fs::temp_directory_path() / target_file_name;
  std::error_code ignored;
  fs::remove(path, ignored);

  try {
    enable_load_driver_privilege();
    unload_filter(false);
    load_filter();
    auto transform_service = connect_transform_service();

    std::vector<std::uint8_t> plaintext(payload_bytes);
    for (std::size_t index = 0; index != plaintext.size(); ++index)
      plaintext[index] = static_cast<std::uint8_t>((index * 37u + 11u) & 0xffu);
    auto cancelled_write_payload = plaintext;
    for (auto &byte : cancelled_write_payload)
      byte ^= 0x5Cu;

    write_all(path, plaintext);
    std::vector<std::uint8_t> first_read;
    try {
      first_read = read_all(path, "initial");
    } catch (...) {
      const auto state = transform_service.invoke(query_runtime_state_method);
      std::fprintf(stderr,
                   "first-read state: post-replies=%u post-cancels=%u "
                   "active-post=%u waiting-post=%u\n",
                   state.pending_post_replies, state.pending_post_cancels,
                   state.active_post_requests, state.waiting_post_requests);
      throw;
    }
    if (first_read != plaintext)
      throw std::runtime_error("post-read copy-back did not restore plaintext");
    verify_completed_read_length(path, plaintext);
    if (!address_is_unmapped(
            last_mapped_address.load(std::memory_order_relaxed)))
      throw std::runtime_error(
          "swapped-buffer user mapping survived completed I/O");
    verify_zero_byte_eof_read(path);
    const auto normal_state = wait_for_idle(transform_service);
    if (normal_state.pending_pre_resumes == 0 ||
        normal_state.pending_post_replies == 0 ||
        normal_state.active_pre_requests != 0 ||
        normal_state.active_post_requests != 0)
      throw std::runtime_error(
          "normal I/O did not drain both pending registries");

    transform_service = {};
    verify_read_fails_without_service(path);
    unload_filter(true);
    auto ciphertext = read_all(path, "raw-after-unload");
    verify_ciphertext(ciphertext, plaintext);

    load_filter();
    transform_service = connect_transform_service();
    std::vector<std::uint8_t> reloaded_read;
    try {
      reloaded_read = read_all(path, "reloaded");
    } catch (...) {
      const auto state = transform_service.invoke(query_runtime_state_method);
      std::fprintf(stderr,
                   "reloaded-read state: post-replies=%u post-cancels=%u "
                   "active-post=%u waiting-post=%u\n",
                   state.pending_post_replies, state.pending_post_cancels,
                   state.active_post_requests, state.waiting_post_requests);
      throw;
    }
    if (reloaded_read != plaintext)
      throw std::runtime_error(
          "reloaded post-read copy-back did not restore plaintext");
    verify_zero_byte_eof_read(path);

    verify_timeout_cancels_pending(transform_service, path,
                                   cancelled_write_payload,
                                   transform_direction::write_input);
    std::vector<std::uint8_t> after_write_timeout;
    try {
      after_write_timeout = read_all(path, "after-write-timeout");
    } catch (...) {
      const auto state = transform_service.invoke(query_runtime_state_method);
      std::fprintf(stderr,
                   "after-write-timeout state: pre-cancels=%u "
                   "post-replies=%u post-cancels=%u active-post=%u\n",
                   state.pending_pre_cancels, state.pending_post_replies,
                   state.pending_post_cancels, state.active_post_requests);
      throw;
    }
    if (after_write_timeout != plaintext)
      throw std::runtime_error(
          "timed-out pre-write changed the lower file contents");
    verify_timeout_cancels_pending(transform_service, path, plaintext,
                                   transform_direction::read_output);
    verify_disconnect_cancels_pending(transform_service, path,
                                      cancelled_write_payload,
                                      transform_direction::write_input);
    if (read_all(path, "after-write-disconnect") != plaintext)
      throw std::runtime_error(
          "disconnected pre-write changed the lower file contents");
    verify_disconnect_cancels_pending(transform_service, path, plaintext,
                                      transform_direction::read_output);

    verify_teardown_cancels_pending(transform_service, path,
                                    cancelled_write_payload,
                                    transform_direction::write_input);
    verify_ciphertext(read_all(path, "raw-after-write-teardown"), plaintext);

    load_filter();
    transform_service = reconnect_transform_service();
    if (read_all(path, "after-write-teardown-reload") != plaintext)
      throw std::runtime_error(
          "filter reload after pended teardown did not restore plaintext");
    verify_teardown_cancels_pending(transform_service, path, plaintext,
                                    transform_direction::read_output);
    verify_ciphertext(read_all(path, "raw-after-read-teardown"), plaintext);

    if (mapped_write_requests.load(std::memory_order_relaxed) == 0 ||
        mapped_read_requests.load(std::memory_order_relaxed) < 2)
      throw std::runtime_error(
          "minifilter did not round-trip swapped buffers through user mode");
    fs::remove(path);

    std::printf("NTL minifilter I/O buffer runtime PASS: bytes=%zu key=0x%02X "
                "mapped-writes=%u mapped-reads=%u "
                "timeout-cancels=2 disconnect-cancels=2 teardown-cancels=2\n",
                plaintext.size(), static_cast<unsigned>(transform_key),
                mapped_write_requests.load(std::memory_order_relaxed),
                mapped_read_requests.load(std::memory_order_relaxed));
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr,
                 "NTL minifilter I/O buffer runtime failure: %s "
                 "(mapped-writes=%u mapped-reads=%u)\n",
                 error.what(),
                 mapped_write_requests.load(std::memory_order_relaxed),
                 mapped_read_requests.load(std::memory_order_relaxed));
    fs::remove(path, ignored);
    unload_filter(false);
    return 1;
  }
}
