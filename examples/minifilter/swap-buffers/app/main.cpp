#include "swap_buffers_sample.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

namespace {

bool write_and_read(const std::filesystem::path &path,
                    std::string_view expected) {
  {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream)
      return false;
    stream.write(expected.data(),
                 static_cast<std::streamsize>(expected.size()));
  }

  std::ifstream stream(path, std::ios::binary);
  const std::string actual{std::istreambuf_iterator<char>{stream},
                           std::istreambuf_iterator<char>{}};
  return stream && actual == expected;
}

} // namespace

int wmain() {
  namespace fs = std::filesystem;
  using namespace crtsys_minifilter_swap_buffers_sample;
  constexpr std::string_view plaintext =
      "crtsys NTL swapped-buffer round trip\n";

  const fs::path root = fs::temp_directory_path();
  const fs::path passthrough = root / passthrough_file_name;
  const fs::path protected_path = root / protected_file_name;

  std::error_code error;
  fs::remove(passthrough, error);
  error.clear();
  fs::remove(protected_path, error);

  if (!write_and_read(passthrough, plaintext)) {
    std::cerr << "passthrough round trip failed\n";
    fs::remove(passthrough, error);
    fs::remove(protected_path, error);
    return 1;
  }
  if (!write_and_read(protected_path, plaintext)) {
    std::cerr << "protected round trip failed\n";
    fs::remove(passthrough, error);
    fs::remove(protected_path, error);
    return 1;
  }

  fs::remove(passthrough, error);
  error.clear();
  fs::remove(protected_path, error);
  if (error) {
    std::cerr << "cleanup failed: " << error.message() << '\n';
    return 1;
  }

  std::cout << "NTL swap-buffers sample completed: .tmp passthrough, "
               ".ntlxor XOR round trip\n";
  return 0;
}
