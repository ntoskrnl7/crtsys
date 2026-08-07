#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <thread>

#include "runtime_controller.hpp"

namespace crtsys::examples::wfp::tls_runtime {

struct lifecycle_options {
  std::filesystem::path ready_file;
  std::filesystem::path remove_policy_file;
  std::filesystem::path policy_removed_file;
  std::filesystem::path stop_file;
  std::filesystem::path stats_file;
  std::filesystem::path ca_file;
  std::filesystem::path identity_thumbprint_file;
  std::uint32_t duration_ms = 60'000;
};

inline lifecycle_options parse_lifecycle(
    crtsys::examples::wfp::runtime::arguments &arguments) {
  lifecycle_options result;
  result.ready_file = arguments.required(L"--ready-file");
  result.remove_policy_file =
      arguments.required(L"--remove-policy-file");
  result.policy_removed_file =
      arguments.required(L"--policy-removed-file");
  result.stop_file = arguments.required(L"--stop-file");
  result.stats_file = arguments.required(L"--stats-file");
  result.ca_file = arguments.required(L"--ca-file");
  result.identity_thumbprint_file =
      arguments.required(L"--identity-thumbprint-file");
  result.duration_ms = arguments.optional_u32(
      L"--duration-ms", result.duration_ms, 100, 300'000);
  return result;
}

template <class Poll>
inline void wait_for_file(const std::filesystem::path &path,
                          std::uint32_t duration_ms, Poll &&poll) {
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(duration_ms);
  while (!std::filesystem::exists(path)) {
    if (std::chrono::steady_clock::now() >= deadline)
      throw std::runtime_error("TLS runtime IPC wait timed out");
    poll();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
}

inline void signal_ready(const lifecycle_options &options) {
  crtsys::examples::wfp::runtime::write_file(options.ready_file,
                                              "ready\n");
}

inline void signal_policy_removed(const lifecycle_options &options) {
  crtsys::examples::wfp::runtime::write_file(options.policy_removed_file,
                                              "removed\n");
}

} // namespace crtsys::examples::wfp::tls_runtime
