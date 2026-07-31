#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "browser_log.hpp"

#include <fstream>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace crtsys::wfp_sample::browser_https {

std::string narrow_dns_name(std::wstring_view value) {
  std::string result;
  result.reserve(value.size());
  for (const wchar_t character : value) {
    if (character <= 0 || character > 0x7f)
      throw std::runtime_error(
          "live HTTPS sample requires an ASCII DNS name");
    result.push_back(static_cast<char>(character));
  }
  return result;
}

browser_html_logger::browser_html_logger(
    std::filesystem::path directory)
    : directory_(std::move(directory)),
      event_path_(directory_ / L"events.log"),
      session_prefix_(
          std::to_wstring(::GetCurrentProcessId()) + L"-" +
          std::to_wstring(::GetTickCount64()) + L"-") {
  std::error_code error;
  std::filesystem::create_directories(directory_, error);
  if (error)
    throw std::system_error(
        error, "create browser HTTPS log directory");
  std::ofstream events(
      event_path_, std::ios::binary | std::ios::app);
  if (!events)
    throw std::runtime_error(
        "cannot create browser HTTPS event log");
  events << "session-start pid=" << ::GetCurrentProcessId()
         << '\n';
}

std::optional<std::filesystem::path>
browser_html_logger::record_response(
    std::wstring_view server_name,
    const parsed_http_response &response) {
  const std::string host = narrow_dns_name(server_name);
  std::lock_guard lock(lock_);
  append_event_locked(
      "response host=" + host +
      " status=" + std::to_string(response.status) +
      " type=" + response.content_type +
      " encoding=" +
      (response.content_encoding.empty()
           ? std::string("identity")
           : response.content_encoding) +
      " body=" + std::to_string(response.body.size()));

  if (!ascii_contains_ci(response.content_type, "text/html") ||
      !response.body_decoded)
    return std::nullopt;

  const std::string safe = safe_name(host);
  wchar_t sequence[16]{};
  if (swprintf_s(
          sequence, L"%06llu-",
          static_cast<unsigned long long>(++next_file_)) < 0)
    throw std::runtime_error(
        "cannot format browser HTTPS log sequence");
  const std::filesystem::path path =
      directory_ /
      (session_prefix_ + std::wstring(sequence) +
       std::wstring(safe.begin(), safe.end()) + L".html");
  std::ofstream output(
      path, std::ios::binary | std::ios::trunc);
  if (!output)
    throw std::runtime_error(
        "cannot create browser HTML log");
  if (!response.body.empty())
    output.write(
        reinterpret_cast<const char *>(
            response.body.data()),
        static_cast<std::streamsize>(response.body.size()));
  if (!output)
    throw std::runtime_error(
        "cannot write browser HTML log");
  ++html_files_;
  append_event_locked(
      "html host=" + host +
      " file=" + path.filename().string() +
      " bytes=" + std::to_string(response.body.size()));
  return path;
}

void browser_html_logger::record_error(
    std::string_view message) noexcept {
  try {
    std::lock_guard lock(lock_);
    append_event_locked(
        "error " + std::string(message));
  } catch (...) {
  }
}

void browser_html_logger::record_protocol(
    std::wstring_view server_name,
    std::string_view protocol) noexcept {
  try {
    const std::string host = narrow_dns_name(server_name);
    std::lock_guard lock(lock_);
    append_event_locked(
        "tls host=" + host + " protocol=" +
        (protocol.empty() ? std::string("http/1.1-fallback")
                          : std::string(protocol)));
  } catch (...) {
  }
}

void browser_html_logger::record_lifecycle(
    std::string_view message) noexcept {
  try {
    std::lock_guard lock(lock_);
    append_event_locked(
        "lifecycle " + std::string(message));
  } catch (...) {
  }
}

void browser_html_logger::record_websocket(
    std::wstring_view server_name,
    std::string_view direction,
    ntl::net::websocket::opcode operation,
    std::size_t payload_size,
    bool message_complete,
    bool message_compressed,
    std::size_t inspected_message_size) noexcept {
  try {
    const std::string host = narrow_dns_name(server_name);
    std::lock_guard lock(lock_);
    append_event_locked(
        "websocket host=" + host +
        " direction=" + std::string(direction) +
        " opcode=" +
        std::to_string(static_cast<unsigned>(operation)) +
        " payload=" + std::to_string(payload_size) +
        " message-complete=" +
        (message_complete ? "yes" : "no") +
        " compressed=" +
        (message_compressed ? "yes" : "no") +
        " inspected-message=" +
        std::to_string(inspected_message_size));
  } catch (...) {
  }
}

std::size_t browser_html_logger::html_files() const noexcept {
  std::lock_guard lock(lock_);
  return html_files_;
}

const std::filesystem::path &
browser_html_logger::directory() const noexcept {
  return directory_;
}

std::string browser_html_logger::safe_name(
    std::string_view value) {
  std::string result;
  result.reserve(value.size());
  for (const unsigned char character : value) {
    const bool safe =
        (character >= 'a' && character <= 'z') ||
        (character >= 'A' && character <= 'Z') ||
        (character >= '0' && character <= '9') ||
        character == '.' || character == '-';
    result.push_back(safe ? static_cast<char>(character) : '_');
  }
  return result.empty() ? std::string("unknown-host") : result;
}

void browser_html_logger::append_event_locked(
    std::string_view message) {
  std::ofstream events(
      event_path_, std::ios::binary | std::ios::app);
  if (!events)
    throw std::runtime_error(
        "cannot append the browser HTTPS event log");
  events << message << '\n';
  if (!events)
    throw std::runtime_error(
        "cannot write the browser HTTPS event log");
}

} // namespace crtsys::wfp_sample::browser_https
