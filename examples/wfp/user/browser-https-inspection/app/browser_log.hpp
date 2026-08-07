#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

#include <ntl/net/websocket/framing>

#include "http1_support.hpp"

namespace crtsys::wfp_sample::browser_https {

std::string narrow_dns_name(std::wstring_view value);

class browser_html_logger {
public:
  explicit browser_html_logger(std::filesystem::path directory);

  browser_html_logger(const browser_html_logger &) = delete;
  browser_html_logger &
  operator=(const browser_html_logger &) = delete;

  std::optional<std::filesystem::path> record_response(
      std::wstring_view server_name,
      const parsed_http_response &response);

  void record_request(
      std::wstring_view server_name,
      std::string_view method,
      std::string_view path) noexcept;

  void record_error(std::string_view message) noexcept;
  void record_protocol(
      std::wstring_view server_name,
      std::string_view protocol) noexcept;
  void record_lifecycle(std::string_view message) noexcept;
  void record_websocket(
      std::wstring_view server_name,
      std::string_view direction,
      ntl::net::websocket::opcode operation,
      std::size_t payload_size,
      bool message_complete,
      bool message_compressed,
      std::size_t inspected_message_size) noexcept;

  std::size_t html_files() const noexcept;
  const std::filesystem::path &directory() const noexcept;

private:
  static std::string safe_name(std::string_view value);
  void append_event_locked(std::string_view message);

  std::filesystem::path directory_;
  std::filesystem::path event_path_;
  std::wstring session_prefix_;
  mutable std::mutex lock_;
  std::uint64_t next_file_ = 0;
  std::size_t html_files_ = 0;
};

} // namespace crtsys::wfp_sample::browser_https
