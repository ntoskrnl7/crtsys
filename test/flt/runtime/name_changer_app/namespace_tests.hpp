#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace crtsys_flt_name_changer_runtime_test {

inline constexpr std::uint32_t usn_query_iterations = 512;
inline constexpr std::uint32_t find_by_sid_stress_file_count = 40000;

struct namespace_feature_support {
  bool enum_usn = false;
  bool read_usn_journal = false;
  bool lookup_stream_from_cluster = false;
  bool find_files_by_sid = false;
};

bool prepare_find_by_sid_stress(const std::filesystem::path &physical_mapping,
                                std::string &failure);

bool run_namespace_tests(const std::filesystem::path &visible_mapping,
                         const std::filesystem::path &visible_created,
                         const std::filesystem::path &visible_renamed,
                         const std::filesystem::path &visible_hard_link,
                         bool run_find_by_sid_stress,
                         namespace_feature_support &support,
                         std::string &failure);

} // namespace crtsys_flt_name_changer_runtime_test
