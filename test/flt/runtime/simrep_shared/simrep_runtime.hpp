#pragma once

#include <ntl/rpc/common>

#include <cstdint>

namespace crtsys_flt_simrep_runtime_test {

inline constexpr wchar_t filter_name[] = L"CrtSysFltSimRepRuntimeTest";
inline constexpr wchar_t instance_name[] =
    L"CrtSys FLT SimRep runtime test instance";
inline constexpr wchar_t altitude[] = L"370030.231";
inline constexpr wchar_t port_name[] = L"\\CrtSysFltSimRepRuntimePort";

inline constexpr wchar_t visible_mapping[] =
    LR"(\crtsys-flt-simrep-visible)";
inline constexpr wchar_t backing_mapping[] =
    LR"(\crtsys-flt-simrep-backing)";
inline constexpr wchar_t payload_name[] = L"payload.txt";
inline constexpr wchar_t created_name[] = L"created.txt";
inline constexpr wchar_t rename_source_name[] = L"rename-source.txt";
inline constexpr wchar_t renamed_name[] = L"renamed.txt";
inline constexpr wchar_t link_source_name[] = L"link-source.txt";
inline constexpr wchar_t linked_name[] = L"linked.txt";
inline constexpr wchar_t tunnel_original_name[] =
    L"Tunneled Long Name.tmp";

struct observations {
  std::int32_t last_reparse_status = 0;
  std::int32_t last_destination_status = 0;
  std::int32_t last_reissue_status = 0;
  std::int32_t last_tunnel_status = 0;
  std::uint32_t create_candidates = 0;
  std::uint32_t reparses = 0;
  std::uint32_t network_queries = 0;
  std::uint32_t network_disallowed = 0;
  std::uint32_t destination_queries = 0;
  std::uint32_t renames_reissued = 0;
  std::uint32_t links_reissued = 0;
  std::uint32_t tunnel_attempts = 0;
  std::uint32_t tunnel_successes = 0;
  std::uint32_t tunnel_names_found = 0;
  std::uint32_t tunnel_names_verified = 0;
  std::uint32_t tunnel_states_created = 0;
  std::uint32_t tunnel_states_destroyed = 0;

  friend zpp::serializer::access;
  template <class Archive, class Self>
  static void serialize(Archive &archive, Self &self) {
    archive(self.last_reparse_status, self.last_destination_status,
            self.last_reissue_status, self.last_tunnel_status,
            self.create_candidates, self.reparses, self.network_queries,
            self.network_disallowed, self.destination_queries,
            self.renames_reissued, self.links_reissued, self.tunnel_attempts,
            self.tunnel_successes, self.tunnel_names_found,
            self.tunnel_names_verified, self.tunnel_states_created,
            self.tunnel_states_destroyed);
  }
};

inline constexpr ntl::rpc::method<0xD10, observations()>
    query_observations{};
inline constexpr ntl::rpc::method<0xD11, std::uint32_t()>
    arm_visible_passthrough{};

} // namespace crtsys_flt_simrep_runtime_test
