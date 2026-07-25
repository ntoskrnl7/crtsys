#include "transaction_runtime.hpp"

#include "../shared/runtime_test.hpp"

#include <KtmW32.h>
#include <Windows.h>

#include <ntl/flt/communication_client>

#include <chrono>
#include <string_view>
#include <thread>

namespace crtsys_flt_runtime_test {
namespace {

namespace fs = std::filesystem;

class transaction_handle {
public:
  transaction_handle() noexcept
      : value_(
            CreateTransaction(nullptr, nullptr, 0, 0, 0, 0,
                              const_cast<wchar_t *>(L"crtsys FLT runtime"))) {}

  transaction_handle(const transaction_handle &) = delete;
  transaction_handle &operator=(const transaction_handle &) = delete;

  ~transaction_handle() {
    if (valid())
      CloseHandle(value_);
  }

  bool valid() const noexcept {
    return value_ != nullptr && value_ != INVALID_HANDLE_VALUE;
  }
  HANDLE get() const noexcept { return value_; }

private:
  HANDLE value_ = INVALID_HANDLE_VALUE;
};

bool write_transacted_file(const fs::path &path, bool commit,
                           std::string &failure) {
  transaction_handle transaction;
  if (!transaction.valid()) {
    failure = "CreateTransaction failed: " + std::to_string(GetLastError());
    return false;
  }

  HANDLE file = CreateFileTransactedW(
      path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
      FILE_ATTRIBUTE_NORMAL, nullptr, transaction.get(), nullptr, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    failure = "CreateFileTransactedW failed: " + std::to_string(GetLastError());
    return false;
  }

  constexpr std::string_view payload = "crtsys transacted minifilter test\n";
  DWORD written = 0;
  const bool write_ok =
      WriteFile(file, payload.data(), static_cast<DWORD>(payload.size()),
                &written, nullptr) != FALSE &&
      written == payload.size();
  const DWORD write_error = write_ok ? ERROR_SUCCESS : GetLastError();
  CloseHandle(file);
  if (!write_ok) {
    failure = "transacted WriteFile failed: " + std::to_string(write_error);
    return false;
  }

  const BOOL finalized = commit ? CommitTransaction(transaction.get())
                                : RollbackTransaction(transaction.get());
  if (!finalized) {
    failure =
        std::string(commit ? "CommitTransaction" : "RollbackTransaction") +
        " failed: " + std::to_string(GetLastError());
    return false;
  }
  return true;
}

} // namespace

bool run_transaction_runtime_tests(ntl::flt::communication_client &client,
                                   const fs::path &root, std::string &failure) {
  const fs::path committed = root / L"crtsys_flt_transaction_commit.tmp";
  const fs::path rolled_back = root / L"crtsys_flt_transaction_rollback.tmp";
  std::error_code error;
  fs::remove(committed, error);
  error.clear();
  fs::remove(rolled_back, error);

  const auto before = client.invoke(transaction_observations_method);
  if (!write_transacted_file(committed, true, failure) ||
      !write_transacted_file(rolled_back, false, failure)) {
    fs::remove(committed, error);
    fs::remove(rolled_back, error);
    return false;
  }

  error.clear();
  if (!fs::exists(committed, error) || error) {
    failure = "the committed transacted file is absent";
    fs::remove(committed, error);
    return false;
  }
  error.clear();
  if (fs::exists(rolled_back, error) || error) {
    failure = "the rolled-back transacted file became visible";
    fs::remove(committed, error);
    fs::remove(rolled_back, error);
    return false;
  }

  transaction_observations after{};
  for (int attempt = 0; attempt != 100; ++attempt) {
    after = client.invoke(transaction_observations_method);
    if (after.commits > before.commits && after.rollbacks > before.rollbacks &&
        after.contexts_destroyed > before.contexts_destroyed) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  fs::remove(committed, error);
  fs::remove(rolled_back, error);

  if (after.contexts_created <= before.contexts_created ||
      after.enlistments <= before.enlistments) {
    failure = "the minifilter did not create and enlist transaction contexts";
    return false;
  }
  if (after.commits <= before.commits) {
    failure = "the minifilter did not observe commit-finalize";
    return false;
  }
  if (after.rollbacks <= before.rollbacks) {
    failure = "the minifilter did not observe rollback";
    return false;
  }
  if (after.contexts_destroyed <= before.contexts_destroyed) {
    failure = "transaction contexts were not cleaned up";
    return false;
  }
  return true;
}

} // namespace crtsys_flt_runtime_test
