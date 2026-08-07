#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>

#include <iostream>
#include <stdexcept>
#include <string_view>

#include "live_host.hpp"
#include "windows_support.hpp"

int wmain(int argc, wchar_t **argv) {
  try {
    if (argc != 3 && argc != 4)
      throw std::invalid_argument(
          "usage: live-acceptance --inspect-host <dns-host> "
          "[--allow-unavailable-revocation]");
    if (std::wstring_view(argv[1]) != L"--inspect-host")
      throw std::invalid_argument("missing --inspect-host");
    const bool allow_unavailable_revocation =
        argc == 4 && std::wstring_view(argv[3]) ==
                         L"--allow-unavailable-revocation";
    if (argc == 4 && !allow_unavailable_revocation)
      throw std::invalid_argument("unknown live HTTPS option");
    crtsys::wfp_sample::winsock_session winsock;
    return crtsys::wfp_sample::tls_inspection::run_live_host_sample(
        argv[2], allow_unavailable_revocation);
  } catch (const std::exception &error) {
    std::cerr << "Live TLS inspection acceptance failed: " << error.what()
              << '\n';
    return 1;
  }
}
