#include "data_scan_runtime.hpp"

#include "../shared/runtime_test.hpp"

#include <Windows.h>

#include <ntl/flt/communication_client>

#include <chrono>
#include <fstream>
#include <thread>

namespace crtsys_flt_runtime_test {

bool run_data_scan_runtime_tests(ntl::flt::communication_client &client,
                                 const std::filesystem::path &root,
                                 std::string &failure) {
  namespace fs = std::filesystem;
  const fs::path seed = root / L"crtsys_flt_data_scan.seed";
  const fs::path target = root / data_scan_file_name;
  std::error_code error;
  fs::remove(seed, error);
  error.clear();
  fs::remove(target, error);

  {
    std::ofstream output(seed, std::ios::binary | std::ios::trunc);
    if (!output) {
      failure = "failed to create the data-scan seed";
      return false;
    }
    output << "crtsys data-scan section payload\n";
  }
  fs::rename(seed, target, error);
  if (error) {
    failure = "failed to rename the data-scan seed: " + error.message();
    fs::remove(seed, error);
    return false;
  }

  const auto before = client.invoke(data_scan_observations_method);
  if (client.invoke(arm_data_scan_method) != 1) {
    failure = "the minifilter refused to arm the data-scan test";
    fs::remove(target, error);
    return false;
  }
  {
    std::ifstream input(target, std::ios::binary);
    char value = 0;
    if (!input || !input.get(value)) {
      failure = "failed to open the data-scan target";
      fs::remove(target, error);
      return false;
    }
  }

  data_scan_observations armed{};
  for (int attempt = 0; attempt != 100; ++attempt) {
    armed = client.invoke(data_scan_observations_method);
    if (armed.sections_created > before.sections_created &&
        armed.sections_mapped > before.sections_mapped) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  if (armed.sections_created <= before.sections_created ||
      armed.sections_mapped <= before.sections_mapped) {
    failure =
        "the minifilter did not create and map a data-scan section; "
        "candidates " +
        std::to_string(armed.create_candidates) + ", registration status " +
        std::to_string(
            static_cast<unsigned long>(armed.last_registration_status)) +
        ", create status " +
        std::to_string(static_cast<unsigned long>(armed.last_create_status)) +
        ", map status " +
        std::to_string(static_cast<unsigned long>(armed.last_map_status));
    (void)client.invoke(close_data_scan_method);
    fs::remove(target, error);
    return false;
  }

  // A cached append can coexist with a read-only data-scan section. Opening
  // with TRUNCATE_EXISTING takes the overwrite path, which requires the file
  // system to purge cached data and therefore conflicts with the held section.
  HANDLE file =
      CreateFileW(target.c_str(), GENERIC_WRITE,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  const DWORD conflict_error =
      file == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
  if (file != INVALID_HANDLE_VALUE)
    CloseHandle(file);

  data_scan_observations closed{};
  for (int attempt = 0; attempt != 100; ++attempt) {
    closed = client.invoke(data_scan_observations_method);
    if (closed.conflicts > before.conflicts &&
        closed.sections_closed > before.sections_closed &&
        closed.contexts_destroyed > before.contexts_destroyed) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  if (closed.conflicts <= before.conflicts) {
    failure = "the section-conflict callback was not delivered; overwrite "
              "status " +
              std::to_string(conflict_error);
    (void)client.invoke(close_data_scan_method);
    fs::remove(target, error);
    return false;
  }

  fs::remove(target, error);
  if (closed.sections_closed <= before.sections_closed ||
      closed.contexts_destroyed <= before.contexts_destroyed) {
    failure =
        "the conflict callback did not close its data-scan section/context";
    return false;
  }
  return true;
}

} // namespace crtsys_flt_runtime_test
