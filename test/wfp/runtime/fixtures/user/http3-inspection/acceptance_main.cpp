#include <filesystem>
#include <iostream>
#include <stdexcept>

int run_controlled_msquic_acceptance(
    const std::filesystem::path &service_executable,
    const std::filesystem::path &ipc_root);

int wmain(int argc, wchar_t **argv) {
  try {
    if (argc != 3)
      throw std::invalid_argument(
          "usage: crtsys_wfp_http3_inspection_acceptance.exe "
          "<service.exe> <ipc-directory>");
    return run_controlled_msquic_acceptance(
        std::filesystem::absolute(argv[1]),
        std::filesystem::absolute(argv[2]));
  } catch (const std::exception &error) {
    std::cerr << "user HTTP/3 acceptance failed: " << error.what() << '\n';
    return 1;
  }
}
