#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include <Windows.h>
#include <KtmW32.h>
#include <fltUser.h>

#define NTL_USER_MODE
#include <ntl/flt/communication_client>

#include "../scanner_shared/scanner_runtime.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace {

namespace fs = std::filesystem;
using namespace crtsys_flt_scanner_runtime_test;

class unique_handle {
public:
  unique_handle() noexcept = default;
  explicit unique_handle(HANDLE value) noexcept : value_(value) {}

  unique_handle(const unique_handle &) = delete;
  unique_handle &operator=(const unique_handle &) = delete;

  unique_handle(unique_handle &&other) noexcept
      : value_(std::exchange(other.value_, INVALID_HANDLE_VALUE)) {}

  unique_handle &operator=(unique_handle &&other) noexcept {
    if (this != &other) {
      reset();
      value_ = std::exchange(other.value_, INVALID_HANDLE_VALUE);
    }
    return *this;
  }

  ~unique_handle() { reset(); }

  HANDLE get() const noexcept { return value_; }
  explicit operator bool() const noexcept {
    return value_ != nullptr && value_ != INVALID_HANDLE_VALUE;
  }

  void reset() noexcept {
    if (*this) {
      CloseHandle(value_);
      value_ = INVALID_HANDLE_VALUE;
    }
  }

private:
  HANDLE value_ = INVALID_HANDLE_VALUE;
};

[[noreturn]] void fail(std::string_view operation,
                       DWORD error = GetLastError()) {
  std::ostringstream message;
  message << operation << " failed: error=" << error;
  throw std::runtime_error(message.str());
}

void require(bool condition, std::string_view message) {
  if (!condition)
    throw std::runtime_error(std::string(message));
}

fs::path relative_path(std::wstring_view value) {
  while (!value.empty() && (value.front() == L'\\' || value.front() == L'/'))
    value.remove_prefix(1);
  return fs::path(value);
}

void write_seed(const fs::path &path, std::string_view contents) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output)
    fail("open seed file");
  output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
  if (!output.good())
    fail("write seed file");
}

unique_handle create_writable(const fs::path &path) {
  HANDLE value =
      CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (value == INVALID_HANDLE_VALUE)
    fail("CreateFileW(writable scan target)");
  return unique_handle(value);
}

void write_all(HANDLE handle, std::string_view contents) {
  DWORD written = 0;
  if (!WriteFile(handle, contents.data(),
                 static_cast<DWORD>(contents.size()), &written, nullptr) ||
      written != contents.size()) {
    fail("WriteFile(clean payload)");
  }
}

void verify_clean_open(const fs::path &path) {
  HANDLE value =
      CreateFileW(path.c_str(), GENERIC_READ,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (value == INVALID_HANDLE_VALUE)
    fail("CreateFileW(clean existing file)");
  CloseHandle(value);
}

DWORD infected_open_result(const fs::path &path) {
  SetLastError(ERROR_SUCCESS);
  HANDLE value =
      CreateFileW(path.c_str(), GENERIC_READ,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  const DWORD error = value == INVALID_HANDLE_VALUE ? GetLastError()
                                                    : ERROR_SUCCESS;
  if (value != INVALID_HANDLE_VALUE)
    CloseHandle(value);
  return error;
}

void verify_clean_write(const fs::path &path) {
  auto file = create_writable(path);
  write_all(file.get(), "clean write accepted by scanner\n");
  file.reset();
  require(fs::file_size(path) != 0,
          "clean write did not reach the file system");
}

void verify_infected_write_denied(const fs::path &path) {
  auto file = create_writable(path);
  const std::string payload =
      std::string("prefix-") + foul_signature + "-suffix";
  DWORD written = 0;
  SetLastError(ERROR_SUCCESS);
  const BOOL result =
      WriteFile(file.get(), payload.data(), static_cast<DWORD>(payload.size()),
                &written, nullptr);
  const DWORD error = result ? ERROR_SUCCESS : GetLastError();
  file.reset();
  require(!result && error == ERROR_ACCESS_DENIED,
          "infected write was not rejected with access denied");
  require(fs::file_size(path) == 0,
          "rejected write changed the on-disk file");
}

void verify_cleanup_rescan(const fs::path &path) {
  auto file = create_writable(path);
  LARGE_INTEGER size{};
  size.QuadPart = 4096;
  if (!SetFilePointerEx(file.get(), size, nullptr, FILE_BEGIN) ||
      !SetEndOfFile(file.get())) {
    fail("extend memory-mapped scan file");
  }

  unique_handle mapping(
      CreateFileMappingW(file.get(), nullptr, PAGE_READWRITE, 0, 0, nullptr));
  if (!mapping)
    fail("CreateFileMappingW");
  void *view =
      MapViewOfFile(mapping.get(), FILE_MAP_WRITE, 0, 0, 4096);
  if (!view)
    fail("MapViewOfFile");
  std::copy_n(foul_signature, sizeof(foul_signature) - 1,
              static_cast<char *>(view));
  if (!FlushViewOfFile(view, sizeof(foul_signature) - 1)) {
    const DWORD error = GetLastError();
    UnmapViewOfFile(view);
    fail("FlushViewOfFile", error);
  }
  UnmapViewOfFile(view);
  mapping.reset();
  if (!FlushFileBuffers(file.get()))
    fail("FlushFileBuffers(mapped scan file)");
  file.reset();
}

void write_transacted(const fs::path &path, bool commit) {
  unique_handle transaction(CreateTransaction(
      nullptr, nullptr, 0, 0, 0, 0,
      const_cast<wchar_t *>(commit ? L"scanner commit"
                                   : L"scanner rollback")));
  if (!transaction)
    fail("CreateTransaction");

  unique_handle file(CreateFileTransactedW(
      path.c_str(), GENERIC_READ | GENERIC_WRITE,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr, transaction.get(),
      nullptr, nullptr));
  if (!file)
    fail("CreateFileTransactedW");
  write_all(file.get(), commit ? "clean committed scanner transaction\n"
                              : "clean rolled-back scanner transaction\n");
  file.reset();

  const BOOL finalized = commit ? CommitTransaction(transaction.get())
                                : RollbackTransaction(transaction.get());
  if (!finalized)
    fail(commit ? "CommitTransaction" : "RollbackTransaction");
  transaction.reset();
}

scan_verdict scan_payload_in_user(const scan_request &request) noexcept {
  if (request.bytes > request.payload.size())
    return scan_verdict::infected;
  const auto begin = request.payload.begin();
  const auto end = begin + request.bytes;
  const auto signature_begin =
      reinterpret_cast<const std::uint8_t *>(foul_signature);
  const auto signature_end =
      signature_begin + sizeof(foul_signature) - 1;
  return std::search(begin, end, signature_begin, signature_end) == end
             ? scan_verdict::clean
             : scan_verdict::infected;
}

observations wait_for_observations(ntl::flt::communication_client &client) {
  observations value{};
  for (unsigned attempt = 0; attempt != 200; ++attempt) {
    value = client.invoke(query_observations);
    if (value.open_denied >= 1 && value.writes_denied >= 1 &&
        value.cleanup_infected >= 1 && value.transaction_commits >= 1 &&
        value.transaction_rollbacks >= 1 &&
        value.section_contexts_created ==
            value.section_contexts_destroyed &&
        value.handle_contexts_created ==
            value.handle_contexts_destroyed &&
        value.transaction_contexts_created ==
            value.transaction_contexts_destroyed) {
      return value;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
  }
  return value;
}

void print_observations(const observations &value) {
  std::cout
      << "last_scan_status=0x" << std::hex << std::uppercase
      << static_cast<std::uint32_t>(value.last_scan_status) << std::dec
      << std::nouppercase
      << " instances=" << value.instances_registered
      << " policy=" << value.policy_requests << '/' << value.policy_failures
      << " open=" << value.open_scans << '/' << value.open_denied
      << " write=" << value.write_scans << '/' << value.writes_allowed << '/'
      << value.writes_denied
      << " cleanup=" << value.cleanup_scans << '/'
      << value.cleanup_infected
      << " sections=" << value.sections_created << '/'
      << value.sections_mapped << '/' << value.sections_closed
      << " conflicts=" << value.section_conflicts
      << " section_contexts=" << value.section_contexts_created << '/'
      << value.section_contexts_destroyed
      << " pending=" << value.pended_writes << '/'
      << value.resumed_writes << '/' << value.cancelled_writes
      << " deferred=" << value.deferred_writes
      << " handle_contexts=" << value.handle_contexts_created << '/'
      << value.handle_contexts_destroyed
      << " transaction_contexts=" << value.transaction_contexts_created << '/'
      << value.transaction_contexts_destroyed
      << " enlistments=" << value.transaction_enlistments
      << " commits=" << value.transaction_commits
      << " rollbacks=" << value.transaction_rollbacks << '\n';
}

void require_observations(const observations &value) {
  require(value.last_scan_status == 0,
          "the last scanner policy operation did not succeed");
  require(value.instances_registered >= 1,
          "the filter did not register its NTFS instance for data scan");
  require(value.policy_requests >= 10 && value.policy_failures == 0,
          "typed scanner policy requests failed");
  require(value.open_scans >= 7 && value.open_denied >= 1,
          "open-time scanning did not reject the infected file");
  require(value.write_scans >= 4 && value.writes_allowed >= 3 &&
              value.writes_denied >= 1,
          "pended pre-write policy did not allow/deny expected writes");
  require(value.cleanup_scans >= 5 && value.cleanup_infected >= 1,
          "cleanup rescan did not detect the mapped infection");
  require(value.sections_created >= 5 &&
              value.sections_created == value.sections_mapped &&
              value.sections_created == value.sections_closed,
          "data-scan section lifecycle did not balance");
  require(value.section_contexts_created ==
              value.section_contexts_destroyed,
          "section-context ownership did not balance");
  require(value.pended_writes == value.write_scans &&
              value.pended_writes ==
                  value.resumed_writes + value.cancelled_writes &&
              value.pended_writes == value.deferred_writes,
          "cancel-safe pended-write ownership did not balance");
  require(value.handle_contexts_created >= 5 &&
              value.handle_contexts_created ==
                  value.handle_contexts_destroyed,
          "stream-handle rescan contexts did not balance");
  require(value.transaction_contexts_created >= 2 &&
              value.transaction_contexts_created ==
                  value.transaction_contexts_destroyed &&
              value.transaction_enlistments >= 2 &&
              value.transaction_commits >= 1 &&
              value.transaction_rollbacks >= 1,
          "transaction-aware scan lifecycle was not observed");
}

} // namespace

int wmain(int argc, wchar_t **argv) {
  if (argc > 2) {
    std::cerr
        << "usage: crtsys_flt_scanner_runtime_test_app [volume-root]\n";
    return 2;
  }

  try {
    fs::path root =
        argc == 2 ? fs::path(argv[1]) : fs::current_path().root_path();
    root = root.root_path();
    if (root.empty())
      throw std::runtime_error("a volume root such as C:\\ is required");

    const fs::path directory = root / relative_path(test_directory_name);
    std::error_code error;
    fs::remove_all(directory, error);
    error.clear();
    if (!fs::create_directories(directory, error) || error) {
      throw std::runtime_error("failed to create scanner directory: " +
                               error.message());
    }

    // Seed before the policy service connects. The driver deliberately
    // follows Scanner's fail-open transport policy.
    write_seed(directory / clean_open_name, "known clean seed\n");
    write_seed(directory / infected_open_name,
               std::string("known ") + foul_signature + " seed\n");
    const auto clean_seed_bytes = fs::file_size(directory / clean_open_name);
    const auto infected_seed_bytes =
        fs::file_size(directory / infected_open_name);
    require(clean_seed_bytes != 0 && infected_seed_bytes != 0,
            "scanner seed writes produced an empty file");
    std::cout << "seed_bytes=" << clean_seed_bytes << '/'
              << infected_seed_bytes << '\n';

    auto client = ntl::flt::communication_client::connect(port_name);
    client.on_request(scan_payload, &scan_payload_in_user);
    require(client.invoke(reset_observations) == 1,
            "the driver rejected scanner counter reset");

    verify_clean_open(directory / clean_open_name);
    const DWORD infected_open_error =
        infected_open_result(directory / infected_open_name);
    if (infected_open_error != ERROR_ACCESS_DENIED) {
      print_observations(client.invoke(query_observations));
      std::ostringstream message;
      message << "infected existing file open was not denied: error="
              << infected_open_error;
      throw std::runtime_error(message.str());
    }
    verify_clean_write(directory / clean_write_name);
    verify_infected_write_denied(directory / blocked_write_name);
    verify_cleanup_rescan(directory / mapped_write_name);

    const fs::path committed = directory / transaction_commit_name;
    const fs::path rolled_back = directory / transaction_rollback_name;
    write_transacted(committed, true);
    write_transacted(rolled_back, false);
    require(fs::exists(committed),
            "committed scanner transaction is not visible");
    require(!fs::exists(rolled_back),
            "rolled-back scanner transaction became visible");

    const observations observed = wait_for_observations(client);
    print_observations(observed);
    require_observations(observed);

    // The infected-open policy also applies to deletion's internal opens.
    // Disconnect first, then let the driver's documented fail-open path make
    // test cleanup possible.
    client = {};
    for (unsigned attempt = 0; attempt != 200; ++attempt) {
      error.clear();
      fs::remove_all(directory, error);
      if (!error)
        break;
      std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    if (error)
      throw std::runtime_error("failed to clean scanner directory: " +
                               error.message());

    std::cout << "NTL scanner lifecycle runtime test PASS\n";
    return 0;
  } catch (const std::exception &failure) {
    std::cerr << "NTL scanner lifecycle runtime test FAIL: "
              << failure.what() << '\n';
    return 1;
  }
}
