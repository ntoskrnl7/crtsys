#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

namespace crtsys::examples::wfp::runtime {

class arguments {
public:
  arguments(int argc, wchar_t **argv) {
    for (int index = 1; index < argc; index += 2) {
      if (index + 1 >= argc)
        throw std::invalid_argument("controller option is missing its value");
      std::wstring name(argv[index]);
      std::wstring value(argv[index + 1]);
      if (!values_.emplace(std::move(name), std::move(value)).second)
        throw std::invalid_argument("duplicate controller option");
    }
  }

  std::wstring required(std::wstring_view name) {
    auto value = take(name);
    if (!value || value->empty())
      throw std::invalid_argument("required controller option is missing");
    return std::move(*value);
  }

  std::uint16_t required_port(std::wstring_view name) {
    const auto parsed = parse_unsigned(required(name));
    if (parsed == 0 || parsed > 65535)
      throw std::invalid_argument("controller port must be between 1 and 65535");
    return static_cast<std::uint16_t>(parsed);
  }

  std::uint32_t optional_u32(std::wstring_view name,
                             std::uint32_t fallback,
                             std::uint32_t minimum,
                             std::uint32_t maximum) {
    auto value = take(name);
    if (!value)
      return fallback;
    const auto parsed = parse_unsigned(*value);
    if (parsed < minimum || parsed > maximum)
      throw std::invalid_argument("controller numeric option is out of range");
    return static_cast<std::uint32_t>(parsed);
  }

  void finish() const {
    if (!values_.empty())
      throw std::invalid_argument("unknown controller option");
  }

private:
  static unsigned long parse_unsigned(std::wstring_view value) {
    if (value.empty())
      throw std::invalid_argument("empty controller numeric option");
    std::size_t consumed = 0;
    const auto parsed = std::stoul(std::wstring(value), &consumed, 10);
    if (consumed != value.size())
      throw std::invalid_argument("invalid controller numeric option");
    return parsed;
  }

  std::optional<std::wstring> take(std::wstring_view name) {
    const auto found = values_.find(std::wstring(name));
    if (found == values_.end())
      return std::nullopt;
    auto value = std::move(found->second);
    values_.erase(found);
    return value;
  }

  std::map<std::wstring, std::wstring, std::less<>> values_;
};

struct lifecycle_options {
  std::filesystem::path ready_file;
  std::filesystem::path stop_file;
  std::filesystem::path stats_file;
  std::uint32_t duration_ms = 60'000;
};

inline lifecycle_options parse_lifecycle(arguments &values) {
  lifecycle_options result;
  result.ready_file = values.required(L"--ready-file");
  result.stop_file = values.required(L"--stop-file");
  result.stats_file = values.required(L"--stats-file");
  result.duration_ms = values.optional_u32(
      L"--duration-ms", result.duration_ms, 100, 300'000);
  return result;
}

inline void write_file(const std::filesystem::path &path,
                       std::string_view contents) {
  if (!path.parent_path().empty())
    std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output)
    throw std::runtime_error("cannot create controller lifecycle file");
  output.write(contents.data(),
               static_cast<std::streamsize>(contents.size()));
  output.close();
  if (!output)
    throw std::runtime_error("cannot flush controller lifecycle file");
}

inline void signal_ready(const lifecycle_options &options) {
  write_file(options.ready_file, "ready\n");
}

template <class Poll>
void wait_for_stop(const lifecycle_options &options, Poll &&poll) {
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(options.duration_ms);
  while (!std::filesystem::exists(options.stop_file)) {
    if (std::chrono::steady_clock::now() >= deadline)
      throw std::runtime_error("controller timed out waiting for --stop-file");
    poll();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
}

inline void wait_for_stop(const lifecycle_options &options) {
  wait_for_stop(options, [] {});
}

} // namespace crtsys::examples::wfp::runtime
