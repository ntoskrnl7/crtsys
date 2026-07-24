#include "basic_sample.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

int wmain() {
  namespace fs = std::filesystem;
  using namespace crtsys_minifilter_basic_sample;

  const fs::path root = fs::temp_directory_path();
  const fs::path source = root / file_name;
  const fs::path renamed = root / renamed_file_name;

  std::error_code error;
  fs::remove(source, error);
  error.clear();
  fs::remove(renamed, error);

  {
    std::ofstream stream(source, std::ios::binary | std::ios::trunc);
    if (!stream) {
      std::cerr << "failed to create " << source << '\n';
      return 1;
    }
    stream << "crtsys NTL basic minifilter sample\n";
  }

  {
    std::ifstream stream(source, std::ios::binary);
    std::string contents;
    if (!stream || !std::getline(stream, contents) ||
        contents != "crtsys NTL basic minifilter sample") {
      std::cerr << "failed to read " << source << '\n';
      fs::remove(source, error);
      return 1;
    }
  }

  fs::rename(source, renamed, error);
  if (error) {
    std::cerr << "rename failed: " << error.message() << '\n';
    fs::remove(source, error);
    return 1;
  }

  fs::remove(renamed, error);
  if (error) {
    std::cerr << "remove failed: " << error.message() << '\n';
    return 1;
  }

  std::cout << "NTL basic minifilter sample completed\n";
  return 0;
}
