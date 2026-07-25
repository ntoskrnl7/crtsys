#pragma once

#include <ntl/flt/rpc>

#include <cstdint>

namespace crtsys_flt_metadata_runtime {

inline constexpr wchar_t metadata_relative_path[] =
    L"\\System Volume Information\\CrtSysMetadataRuntime.md";

struct observations {
  std::int32_t last_open_status = 0;
  std::int32_t last_transition_status = 0;
  std::uint32_t instances_opened = 0;
  std::uint32_t instance_teardowns = 0;
  std::uint32_t instances_closed = 0;
  std::uint32_t releases = 0;
  std::uint32_t reopen_attempts = 0;
  std::uint32_t reopens = 0;
  std::uint32_t reopen_noops = 0;
  std::uint32_t implicit_lock_pre = 0;
  std::uint32_t implicit_lock_succeeded = 0;
  std::uint32_t explicit_lock_pre = 0;
  std::uint32_t explicit_lock_succeeded = 0;
  std::uint32_t explicit_lock_failed = 0;
  std::uint32_t unlock_succeeded = 0;
  std::uint32_t dismount_succeeded = 0;
  std::uint32_t dismount_failed = 0;
  std::uint32_t snapshot_pre = 0;
  std::uint32_t snapshot_post = 0;
  std::uint32_t snapshot_update_rejected = 0;
  std::uint32_t query_remove = 0;
  std::uint32_t cancel_remove = 0;
  std::uint32_t surprise_remove = 0;

  friend zpp::serializer::access;
  template <typename Archive, typename Self>
  static void serialize(Archive &archive, Self &self) {
    archive(self.last_open_status, self.last_transition_status,
            self.instances_opened, self.instance_teardowns,
            self.instances_closed, self.releases, self.reopen_attempts,
            self.reopens, self.reopen_noops, self.implicit_lock_pre,
            self.implicit_lock_succeeded, self.explicit_lock_pre,
            self.explicit_lock_succeeded, self.explicit_lock_failed,
            self.unlock_succeeded, self.dismount_succeeded,
            self.dismount_failed, self.snapshot_pre, self.snapshot_post,
            self.snapshot_update_rejected, self.query_remove,
            self.cancel_remove, self.surprise_remove);
  }
};

#if !defined(NTL_USER_MODE)
observations capture_observations() noexcept;
#endif

} // namespace crtsys_flt_metadata_runtime

NTL_FLT_RPC_BEGIN_CONTRACT(crtsys_flt_metadata_runtime,
                           L"\\CrtSysFltMetadataRuntimePort", 1, 0)

NTL_FLT_ADD_METHOD_ID(
    crtsys_flt_metadata_runtime, 0xB40,
    crtsys_flt_metadata_runtime::observations(), get_observations,
    []() noexcept {
      return crtsys_flt_metadata_runtime::capture_observations();
    })

NTL_FLT_RPC_END(crtsys_flt_metadata_runtime)
