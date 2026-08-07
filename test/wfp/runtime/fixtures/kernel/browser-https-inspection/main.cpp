#include "managed_acceptance.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>

int wmain(int argc, wchar_t **argv) {
  try {
    if (argc > 2)
      throw std::invalid_argument(
          "usage: crtsys_wfp_kernel_browser_https_inspection_acceptance.exe "
          "[log-directory]");
    const std::filesystem::path logs =
        argc == 2 ? argv[1] : L"kernel-browser-https-acceptance";
    return crtsys::wfp_kernel_browser_https::run_managed_acceptance(logs);
  } catch (const std::exception &error) {
    std::cerr << "Kernel browser HTTPS acceptance failed: " << error.what()
              << '\n';
    return 1;
  }
}
