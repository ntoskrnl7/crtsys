#include "capture_log.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>

namespace contract = wfp_kernel_browser_https_inspection;
using crtsys::wfp_kernel_browser_https::capture_log;

namespace {

void put(std::span<std::byte> destination, std::string_view value) {
  std::copy_n(reinterpret_cast<const std::byte *>(value.data()), value.size(),
              destination.begin());
}

std::string read_all(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

} // namespace

int main() {
  const auto root = std::filesystem::temp_directory_path() /
                    ("crtsys-kernel-browser-capture-privacy-" +
                     std::to_string(GetCurrentProcessId()));
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
  try {
    contract::inspection_record record{};
    record.sequence = 1;
    record.session_id = 7;
    record.protocol = contract::inspected_protocol::http3;
    record.action = contract::inspection_action::permitted;
    constexpr std::string_view server_name = "privacy.invalid";
    record.server_name_size = static_cast<std::uint32_t>(server_name.size());
    std::copy(server_name.begin(), server_name.end(), record.server_name.begin());
    constexpr std::string_view private_request =
        "Authorization: Bearer authorization-secret\r\n"
        "Cookie: session=cookie-secret\r\n"
        "GET /?token=query-secret HTTP/1.1\r\n\r\nrequest-body-secret";
    record.request_size =
        static_cast<std::uint32_t>(private_request.size());
    put(record.request, private_request);
    constexpr std::string_view private_binary_response =
        "<html>binary-response-secret";
    record.response_size =
        static_cast<std::uint32_t>(private_binary_response.size());
    put(record.response, private_binary_response);

    capture_log log(root);
    log.write(record);

    record.sequence = 2;
    record.request_size = 0;
    record.flags = contract::html_content;
    constexpr std::string_view public_html = "<html>public fixture</html>";
    record.response_size = static_cast<std::uint32_t>(public_html.size());
    put(record.response, public_html);
    log.write(record);

    constexpr std::array forbidden{
        "authorization-secret", "cookie-secret", "query-secret",
        "request-body-secret",  "binary-response-secret",
        "Authorization:",       "Cookie:"};
    bool metadata_seen = false;
    bool html_seen = false;
    for (const auto &entry :
         std::filesystem::recursive_directory_iterator(root)) {
      if (!entry.is_regular_file())
        continue;
      if (entry.path().filename() == "request.bin" ||
          entry.path().filename() == "response.bin")
        return 1;
      if (entry.path().filename() == "response.html")
        html_seen = true;
      const std::string contents = read_all(entry.path());
      for (const std::string_view marker : forbidden) {
        if (contents.find(marker) != std::string::npos)
          return 2;
      }
      if (entry.path().filename() == "metadata.txt") {
        metadata_seen =
            contents.find("request-metadata-bytes=") != std::string::npos;
      }
    }
    if (!metadata_seen || !html_seen)
      return 3;
  } catch (...) {
    std::filesystem::remove_all(root, ignored);
    return 4;
  }
  std::filesystem::remove_all(root, ignored);
  std::cout << "Kernel browser capture log privacy contract PASS: "
               "request-file=absent non-html-response-file=absent "
               "request-secrets=absent html=content-type-gated "
               "metadata=bounded\n";
  return 0;
}
