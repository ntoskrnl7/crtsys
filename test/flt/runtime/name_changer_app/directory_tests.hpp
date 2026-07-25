#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace crtsys_flt_name_changer_runtime_test {

inline constexpr std::array<std::string_view, 10>
    directory_information_class_names{{
        "FileDirectoryInformation",
        "FileFullDirectoryInformation",
        "FileBothDirectoryInformation",
        "FileNamesInformation",
        "FileIdBothDirectoryInformation",
        "FileIdFullDirectoryInformation",
        "FileIdExtdDirectoryInformation",
        "FileIdExtdBothDirectoryInformation",
        "FileId64ExtdDirectoryInformation",
        "FileId64ExtdBothDirectoryInformation",
    }};

struct directory_record_metadata {
  std::int64_t creation_time = 0;
  std::int64_t last_access_time = 0;
  std::int64_t last_write_time = 0;
  std::int64_t change_time = 0;
  std::int64_t end_of_file = 0;
  std::int64_t allocation_size = 0;
  std::array<std::uint8_t, 16> file_id{};
  std::uint8_t file_id_size = 0;
  std::uint32_t attributes = 0;
  std::uint32_t ea_size = 0;
  bool has_common_metadata = false;
  bool has_ea_size = false;
  bool has_file_id = false;
};

struct directory_enumeration_baseline {
  std::array<directory_record_metadata, 10> real_mapping_metadata{};
  std::array<bool, 10> supported{};
};

bool prepare_directory_enumeration_tests(
    const std::filesystem::path &visible_parent,
    const std::filesystem::path &store_parent,
    directory_enumeration_baseline &baseline, std::string &failure);

bool run_directory_enumeration_tests(
    const std::filesystem::path &visible_parent,
    const std::filesystem::path &visible_mapping,
    const std::filesystem::path &store_parent,
    const directory_enumeration_baseline &baseline, std::string &failure);

} // namespace crtsys_flt_name_changer_runtime_test
