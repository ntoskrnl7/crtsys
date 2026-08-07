#include "controller.hpp"

#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>

int wmain(int argc, wchar_t **argv) {
  try {
    if (argc != 3)
      throw std::invalid_argument(
          "usage: crtsys_wfp_kernel_http3_inspection_controller.exe "
          "<controlled-application.exe> <ipc-directory>");
    return crtsys::wfp_kernel_http3::run_controller(
        std::filesystem::absolute(argv[1]),
        std::filesystem::absolute(argv[2]));
  } catch (const std::exception &error) {
    std::cerr << "Kernel HTTP/3 inspection controller failed: "
              << error.what() << '\n';
    return 1;
  }
}
