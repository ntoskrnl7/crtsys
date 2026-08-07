#pragma once

#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <thread>

namespace crtsys::wfp_sample {

class controller_lifecycle {
public:
  explicit controller_lifecycle(
      const std::filesystem::path &directory)
      : directory_(std::filesystem::absolute(directory)),
        ready_(directory_ / L"controller.ready"),
        stop_(directory_ / L"stop.request"),
        stats_(directory_ / L"controller.stats") {
    std::filesystem::create_directories(directory_);
    remove(ready_);
    remove(stop_);
    remove(stats_);
  }

  controller_lifecycle(const controller_lifecycle &) = delete;
  controller_lifecycle &operator=(const controller_lifecycle &) = delete;

  ~controller_lifecycle() { remove(ready_); }

  void publish_ready(std::string_view stats) {
    write(stats_, stats);
    write(ready_, "ready\n");
  }

  void publish_stats(std::string_view stats) {
    write(stats_, stats);
  }

  bool command_exists(std::wstring_view name) const {
    std::error_code error;
    const bool exists = std::filesystem::exists(
        directory_ / std::filesystem::path(name), error);
    if (error)
      throw std::system_error(error, "query controller command");
    return exists;
  }

  void acknowledge(std::wstring_view name) const {
    write(directory_ / std::filesystem::path(name), "ack\n");
  }

  void wait_for_command(std::wstring_view name) const {
    const auto command = directory_ / std::filesystem::path(name);
    while (!exists(command))
      std::this_thread::sleep_for(std::chrono::milliseconds(25));
  }

  void wait_for_stop() const {
    while (!exists(stop_))
      std::this_thread::sleep_for(std::chrono::milliseconds(25));
  }

  const std::filesystem::path &directory() const noexcept {
    return directory_;
  }

private:
  static bool exists(const std::filesystem::path &path) {
    std::error_code error;
    const bool result = std::filesystem::exists(path, error);
    if (error)
      throw std::system_error(error, "query controller IPC file");
    return result;
  }

  static void remove(const std::filesystem::path &path) noexcept {
    std::error_code ignored;
    (void)std::filesystem::remove(path, ignored);
  }

  static void write(
      const std::filesystem::path &path,
      std::string_view value) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.exceptions(std::ios::failbit | std::ios::badbit);
    stream.write(value.data(), static_cast<std::streamsize>(value.size()));
  }

  std::filesystem::path directory_;
  std::filesystem::path ready_;
  std::filesystem::path stop_;
  std::filesystem::path stats_;
};

} // namespace crtsys::wfp_sample
