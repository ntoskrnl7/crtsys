#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace crtsys::examples::wfp::content_filter::control {

struct options {
  std::uint16_t port = 0;
  std::filesystem::path ready_file;
  std::filesystem::path stop_file;
  std::filesystem::path stats_file;
  std::size_t expected_requests = 0;
  std::uint32_t duration_ms = 30'000;
  std::wstring behavior = L"normal";
};

inline unsigned long parse_unsigned(std::wstring_view value) {
  if (value.empty())
    throw std::invalid_argument("missing numeric controller argument");
  std::size_t consumed = 0;
  const unsigned long parsed = std::stoul(std::wstring(value), &consumed, 10);
  if (consumed != value.size())
    throw std::invalid_argument("invalid numeric controller argument");
  return parsed;
}

inline options parse_options(int argc, wchar_t **argv,
                             bool require_request_count) {
  options result;
  for (int index = 1; index < argc; ++index) {
    const std::wstring_view name(argv[index]);
    if (index + 1 >= argc)
      throw std::invalid_argument("controller option is missing its value");
    const std::wstring_view value(argv[++index]);
    if (name == L"--port") {
      const auto parsed = parse_unsigned(value);
      if (parsed == 0 || parsed > 65535)
        throw std::invalid_argument("--port must be between 1 and 65535");
      result.port = static_cast<std::uint16_t>(parsed);
    } else if (name == L"--ready-file") {
      result.ready_file = value;
    } else if (name == L"--stop-file") {
      result.stop_file = value;
    } else if (name == L"--stats-file") {
      result.stats_file = value;
    } else if (name == L"--expected-requests") {
      result.expected_requests =
          static_cast<std::size_t>(parse_unsigned(value));
    } else if (name == L"--duration-ms") {
      result.duration_ms = static_cast<std::uint32_t>(
          parse_unsigned(value));
      if (result.duration_ms < 100 || result.duration_ms > 300'000)
        throw std::invalid_argument(
            "--duration-ms must be between 100 and 300000");
    } else if (name == L"--behavior") {
      result.behavior = value;
    } else {
      throw std::invalid_argument("unknown content-filter controller option");
    }
  }
  if (result.port == 0 || result.ready_file.empty() ||
      result.stop_file.empty() || result.stats_file.empty())
    throw std::invalid_argument(
        "required: --port, --ready-file, --stop-file, --stats-file");
  if (require_request_count && result.behavior == L"normal" &&
      result.expected_requests == 0)
    throw std::invalid_argument(
        "normal user policy service requires --expected-requests");
  return result;
}

inline void write_file(const std::filesystem::path &path,
                       std::string_view value) {
  if (!path.parent_path().empty())
    std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output)
    throw std::runtime_error("cannot create content-filter control file");
  output.write(value.data(), static_cast<std::streamsize>(value.size()));
  output.close();
  if (!output)
    throw std::runtime_error("cannot flush content-filter control file");
}

inline void signal_ready(const options &value) {
  write_file(value.ready_file, "ready\n");
}

inline void wait_for_stop(const options &value) {
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(value.duration_ms);
  while (!std::filesystem::exists(value.stop_file)) {
    if (std::chrono::steady_clock::now() >= deadline)
      throw std::runtime_error(
          "content-filter controller timed out waiting for --stop-file");
    ::Sleep(20);
  }
}

} // namespace crtsys::examples::wfp::content_filter::control
