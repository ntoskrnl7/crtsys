#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "browser_log.hpp"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace sample = crtsys::wfp_sample::browser_https;

namespace {

void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}

std::string read_all(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    throw std::runtime_error("cannot read privacy contract artifact");
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

} // namespace

int main() {
  const auto directory =
      std::filesystem::temp_directory_path() /
      (L"crtsys-browser-log-privacy-" +
       std::to_wstring(::GetCurrentProcessId()) + L"-" +
       std::to_wstring(::GetTickCount64()));
  std::filesystem::create_directories(directory);

  sample::browser_html_logger logger(directory);
  logger.record_request(
      L"privacy.example", "GET",
      "/private/account?token=super-secret&user=alice");

  crtsys::wfp_sample::parsed_http_response response;
  response.status = 200;
  response.content_type = "text/html; charset=utf-8";
  response.body_decoded = true;
  constexpr std::string_view html =
      "<html><body>explicit capture</body></html>";
  response.body.assign(
      reinterpret_cast<const std::byte *>(html.data()),
      reinterpret_cast<const std::byte *>(html.data() + html.size()));
  const auto html_path = logger.record_response(
      L"privacy.example", response);
  require(html_path.has_value(),
          "explicit decoded HTML capture was not written");

  const std::string events = read_all(directory / L"events.log");
  require(events.find("query=redacted") != std::string::npos &&
              events.find("super-secret") == std::string::npos &&
              events.find("alice") == std::string::npos &&
              events.find("/private/account") == std::string::npos &&
              events.find("authorization") == std::string::npos,
          "request path, query, or header data leaked into metadata logs");
  require(read_all(*html_path) == html,
          "explicit HTML capture content changed");

  std::cout
      << "Browser capture privacy contracts passed: "
         "path=metadata-only query=redacted headers=metadata-only "
         "request-body=excluded html=explicit\n";
  return 0;
}
