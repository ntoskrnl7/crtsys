#pragma once

#include <ntl/rpc/common>

#include <cstdint>
#include <string_view>

namespace crtsys_flt_name_changer_runtime_test {

inline constexpr wchar_t filter_name[] = L"CrtSysFltNameChangerRuntimeTest";
inline constexpr wchar_t default_instance_name[] =
    L"CrtSys FLT NameChanger runtime test instance";
inline constexpr wchar_t default_altitude[] = L"370030.230";
inline constexpr wchar_t port_name[] = L"\\CrtSysFltNameChangerRuntimePort";
inline constexpr ntl::rpc::method<0xC71, std::uint64_t()>
    query_generated_name_count{};

struct observations {
  std::uint64_t generated_names = 0;
  std::uint64_t query_name_rewrites = 0;
  std::uint64_t rename_reissues = 0;
  std::uint64_t hard_link_reissues = 0;
  std::uint64_t notification_requests = 0;
  std::uint64_t usn_rewrites = 0;
  std::uint64_t extended_directory_queries = 0;
  std::uint64_t network_query_retries = 0;
  std::uint64_t synthetic_file_id_64_layouts = 0;
  std::uint64_t hard_link_query_rewrites = 0;
  std::uint64_t enum_usn_rewrites = 0;
  std::uint64_t read_journal_rewrites = 0;
  std::uint64_t lookup_cluster_rewrites = 0;
  std::uint64_t find_by_sid_rewrites = 0;

  friend zpp::serializer::access;
  template <class Archive, class Self>
  static void serialize(Archive &archive, Self &self) {
    archive(self.generated_names, self.query_name_rewrites,
            self.rename_reissues, self.hard_link_reissues,
            self.notification_requests, self.usn_rewrites,
            self.extended_directory_queries, self.network_query_retries,
            self.synthetic_file_id_64_layouts, self.hard_link_query_rewrites,
            self.enum_usn_rewrites, self.read_journal_rewrites,
            self.lookup_cluster_rewrites, self.find_by_sid_rewrites);
  }
};

inline constexpr ntl::rpc::method<0xC72, observations()> query_observations{};

inline constexpr wchar_t user_mapping[] =
    LR"(\crtsys-namechanger-visible\graft)";
inline constexpr wchar_t real_mapping[] = LR"(\crtsys-namechanger-store\real)";
inline constexpr wchar_t user_mapping_final_component[] = L"graft";
inline constexpr wchar_t real_mapping_final_component[] = L"real";
inline constexpr wchar_t payload_name[] = L"payload.txt";
inline constexpr wchar_t created_name[] = L"created-through-graft.txt";
inline constexpr wchar_t renamed_name[] = L"renamed-through-graft.txt";
inline constexpr wchar_t hard_link_name[] = L"hard-link-through-graft.txt";
inline constexpr wchar_t notification_created_name[] =
    L"notification-created.txt";
inline constexpr wchar_t notification_renamed_name[] =
    L"notification-renamed.txt";

} // namespace crtsys_flt_name_changer_runtime_test
