#include <ntl/flt/all>

#include "../simrep_shared/simrep_runtime.hpp"

#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace {

using namespace crtsys_flt_simrep_runtime_test;

std::atomic<NTSTATUS> last_reparse_status{STATUS_NOT_SUPPORTED};
std::atomic<NTSTATUS> last_destination_status{STATUS_NOT_SUPPORTED};
std::atomic<NTSTATUS> last_reissue_status{STATUS_NOT_SUPPORTED};
std::atomic<NTSTATUS> last_tunnel_status{STATUS_NOT_SUPPORTED};
std::atomic<std::uint32_t> create_candidates{0};
std::atomic<std::uint32_t> reparses{0};
std::atomic<std::uint32_t> network_queries{0};
std::atomic<std::uint32_t> network_disallowed{0};
std::atomic<std::uint32_t> destination_queries{0};
std::atomic<std::uint32_t> renames_reissued{0};
std::atomic<std::uint32_t> links_reissued{0};
std::atomic<std::uint32_t> tunnel_attempts{0};
std::atomic<std::uint32_t> tunnel_successes{0};
std::atomic<std::uint32_t> tunnel_names_found{0};
std::atomic<std::uint32_t> tunnel_names_verified{0};
std::atomic<std::uint32_t> tunnel_states_created{0};
std::atomic<std::uint32_t> tunnel_states_destroyed{0};
std::atomic<long> visible_passthrough_armed{0};

wchar_t fold_case(wchar_t value) noexcept {
  return RtlUpcaseUnicodeChar(value);
}

bool equal_name(std::wstring_view left, std::wstring_view right) noexcept {
  if (left.size() != right.size())
    return false;
  for (std::size_t index = 0; index != left.size(); ++index) {
    if (fold_case(left[index]) != fold_case(right[index]))
      return false;
  }
  return true;
}

bool path_prefix(std::wstring_view path, std::wstring_view prefix,
                 std::wstring_view &remainder) noexcept {
  remainder = {};
  if (path.size() < prefix.size() ||
      !equal_name(path.substr(0, prefix.size()), prefix))
    return false;
  if (path.size() == prefix.size())
    return true;
  if (path[prefix.size()] != L'\\')
    return false;
  remainder = path.substr(prefix.size());
  return true;
}

bool map_visible_name(ntl::flt::name_information &name,
                      bool include_volume,
                      std::wstring &replacement) {
  if (name.try_parse().is_err() || name.name().size() < name.volume().size())
    return false;

  const std::wstring_view relative = name.name().substr(name.volume().size());
  std::wstring_view remainder;
  if (!path_prefix(relative, visible_mapping, remainder))
    return false;

  replacement.clear();
  if (include_volume)
    replacement.assign(name.volume());
  replacement.append(backing_mapping);
  replacement.append(remainder);
  return true;
}

bool is_tunnel_probe(ntl::flt::name_information &name) {
  if (name.try_parse().is_err() || name.name().size() < name.volume().size())
    return false;
  const std::wstring_view relative = name.name().substr(name.volume().size());
  std::wstring_view remainder;
  if (!path_prefix(relative, backing_mapping, remainder) ||
      remainder.size() < 2 || remainder.front() != L'\\' ||
      remainder.substr(1).find(L'\\') != std::wstring_view::npos) {
    return false;
  }
  const std::wstring_view component = remainder.substr(1);
  return equal_name(component, tunnel_original_name) ||
         component.find(L'~') != std::wstring_view::npos;
}

struct tunnel_state {
  explicit tunnel_state(ntl::flt::name_information &&name) noexcept
      : preoperation_name(std::move(name)) {
    tunnel_states_created.fetch_add(1, std::memory_order_release);
  }

  ~tunnel_state() noexcept {
    tunnel_states_destroyed.fetch_add(1, std::memory_order_release);
  }

  ntl::flt::name_information preoperation_name;
};

ntl::flt::pre_result
pre_create(ntl::flt::create_callback_data data, ntl::flt::related_objects,
           ntl::flt::completion_slot<tunnel_state> &completion) noexcept {
  if (!data.target_file() || !data.is_irp_operation() ||
      (data.operation_flags() & SL_OPEN_PAGING_FILE) != 0 ||
      (data.target_file().native_object()->Flags & FO_VOLUME_OPEN) != 0 ||
      (data.parameters().create_options() & FILE_OPEN_BY_FILE_ID) != 0) {
    return ntl::flt::pre_result::success_no_callback;
  }

  auto name =
      data.try_query_name(FLT_FILE_NAME_OPENED | FLT_FILE_NAME_QUERY_DEFAULT |
                          FLT_FILE_NAME_DO_NOT_CACHE);
  if (!name)
    return ntl::flt::pre_result::success_no_callback;

  std::wstring replacement;
  try {
    if (map_visible_name(*name, true, replacement)) {
      create_candidates.fetch_add(1, std::memory_order_release);
      if (replacement.size() ==
              name->volume().size() +
                  std::wstring_view(backing_mapping).size() &&
          visible_passthrough_armed.exchange(0,
                                             std::memory_order_acq_rel) != 0) {
        return ntl::flt::pre_result::success_no_callback;
      }

      const ntl::status status = ntl::flt::try_complete_reparse(
          ntl::flt::as_pre(data), replacement,
          ntl::flt::reparse_name_kind::absolute);
      last_reparse_status.store(status, std::memory_order_release);
      if (status.is_ok()) {
        reparses.fetch_add(1, std::memory_order_release);
        return ntl::flt::pre_result::complete;
      }
      data.complete(status);
      return ntl::flt::pre_result::complete;
    }

    auto normalized = data.try_query_name(
        FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_DEFAULT |
        FLT_FILE_NAME_DO_NOT_CACHE);
    if (!normalized || !is_tunnel_probe(*normalized))
      return ntl::flt::pre_result::success_no_callback;
    const ntl::status stored = completion.try_emplace(std::move(*normalized));
    return stored.is_ok() ? ntl::flt::pre_result::success_with_callback
                          : ntl::flt::pre_result::success_no_callback;
  } catch (...) {
    data.complete(STATUS_INSUFFICIENT_RESOURCES);
    return ntl::flt::pre_result::complete;
  }
}

void post_create(ntl::flt::create_callback_data data,
                 ntl::flt::related_objects,
                 ntl::flt::completion_ref<tunnel_state> state) noexcept {
  if (!state || data.io_status().is_err())
    return;

  tunnel_attempts.fetch_add(1, std::memory_order_release);
  auto tunneled = ntl::flt::try_get_tunneled_name(
      ntl::flt::as_post(data), state->preoperation_name);
  last_tunnel_status.store(
      tunneled ? STATUS_SUCCESS
               : static_cast<NTSTATUS>(tunneled.status()),
      std::memory_order_release);
  if (!tunneled)
    return;
  tunnel_successes.fetch_add(1, std::memory_order_release);
  if (*tunneled) {
    tunnel_names_found.fetch_add(1, std::memory_order_release);
    if (tunneled->try_parse().is_ok() &&
        tunneled->final_component() == tunnel_original_name) {
      tunnel_names_verified.fetch_add(1, std::memory_order_release);
    }
  }
}

ntl::flt::pre_result
pre_network_query_open(ntl::flt::network_query_open_callback_data data,
                       ntl::flt::related_objects, void *&) noexcept {
  network_queries.fetch_add(1, std::memory_order_release);
  const auto parameters = data.parameters();
  if (!data.is_fast_io_operation() || parameters.paging_file() ||
      parameters.open_by_file_id() || !data.target_file() ||
      (data.target_file().native_object()->Flags & FO_VOLUME_OPEN) != 0) {
    return ntl::flt::pre_result::success_no_callback;
  }

  auto name =
      data.try_query_name(FLT_FILE_NAME_OPENED | FLT_FILE_NAME_QUERY_DEFAULT |
                          FLT_FILE_NAME_DO_NOT_CACHE);
  if (!name)
    return ntl::flt::pre_result::success_no_callback;

  try {
    std::wstring replacement;
    if (!map_visible_name(*name, false, replacement))
      return ntl::flt::pre_result::success_no_callback;
    network_disallowed.fetch_add(1, std::memory_order_release);
    return ntl::flt::pre_result::disallow_fast_io;
  } catch (...) {
    data.complete(STATUS_INSUFFICIENT_RESOURCES);
    return ntl::flt::pre_result::complete;
  }
}

ntl::flt::pre_result
pre_set_information(ntl::flt::set_information_callback_data data,
                    ntl::flt::related_objects, void *&) noexcept {
  const auto destination = data.parameters().destination();
  if (!destination)
    return ntl::flt::pre_result::success_no_callback;

  destination_queries.fetch_add(1, std::memory_order_release);
  auto name = ntl::flt::try_query_destination_name(
      ntl::flt::as_pre(data),
      FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_DEFAULT |
          FLT_FILE_NAME_DO_NOT_CACHE);
  last_destination_status.store(
      name ? STATUS_SUCCESS : static_cast<NTSTATUS>(name.status()),
      std::memory_order_release);
  if (!name)
    return ntl::flt::pre_result::success_no_callback;

  try {
    std::wstring replacement;
    if (!map_visible_name(*name, true, replacement))
      return ntl::flt::pre_result::success_no_callback;

    const ntl::status status = ntl::flt::try_reissue_destination(
        ntl::flt::as_pre(data), replacement);
    last_reissue_status.store(status, std::memory_order_release);
    if (status.is_ok()) {
      if (destination.kind() ==
          ntl::flt::destination_information_kind::rename) {
        renames_reissued.fetch_add(1, std::memory_order_release);
      } else {
        links_reissued.fetch_add(1, std::memory_order_release);
      }
    }
    return ntl::flt::pre_result::complete;
  } catch (...) {
    data.complete(STATUS_INSUFFICIENT_RESOURCES);
    return ntl::flt::pre_result::complete;
  }
}

} // namespace

ntl::status ntl::flt::main(ntl::flt::driver &driver, std::wstring_view) {
  using namespace crtsys_flt_simrep_runtime_test;

  ntl::flt::communication_server messages;
  messages.contract(1)
      .on(query_observations, []() noexcept {
        observations result;
        result.last_reparse_status =
            last_reparse_status.load(std::memory_order_acquire);
        result.last_destination_status =
            last_destination_status.load(std::memory_order_acquire);
        result.last_reissue_status =
            last_reissue_status.load(std::memory_order_acquire);
        result.last_tunnel_status =
            last_tunnel_status.load(std::memory_order_acquire);
        result.create_candidates =
            create_candidates.load(std::memory_order_acquire);
        result.reparses = reparses.load(std::memory_order_acquire);
        result.network_queries =
            network_queries.load(std::memory_order_acquire);
        result.network_disallowed =
            network_disallowed.load(std::memory_order_acquire);
        result.destination_queries =
            destination_queries.load(std::memory_order_acquire);
        result.renames_reissued =
            renames_reissued.load(std::memory_order_acquire);
        result.links_reissued =
            links_reissued.load(std::memory_order_acquire);
        result.tunnel_attempts =
            tunnel_attempts.load(std::memory_order_acquire);
        result.tunnel_successes =
            tunnel_successes.load(std::memory_order_acquire);
        result.tunnel_names_found =
            tunnel_names_found.load(std::memory_order_acquire);
        result.tunnel_names_verified =
            tunnel_names_verified.load(std::memory_order_acquire);
        result.tunnel_states_created =
            tunnel_states_created.load(std::memory_order_acquire);
        result.tunnel_states_destroyed =
            tunnel_states_destroyed.load(std::memory_order_acquire);
        return result;
      })
      .on(arm_visible_passthrough, []() noexcept {
        visible_passthrough_armed.store(1, std::memory_order_release);
        return std::uint32_t{1};
      });
  const ntl::status port =
      driver.add_communication_port(port_name, std::move(messages));
  if (port.is_err())
    return port;

  ntl::flt::registration callbacks;
  callbacks
      .on_with_completion<tunnel_state>(ntl::flt::operation::create,
                                        &pre_create, &post_create)
      .on(ntl::flt::operation::network_query_open,
          &pre_network_query_open)
      .on(ntl::flt::operation::set_information, &pre_set_information)
      .on_instance_setup([](ntl::flt::related_objects,
                            FLT_INSTANCE_SETUP_FLAGS, DEVICE_TYPE,
                            FLT_FILESYSTEM_TYPE filesystem) noexcept {
        return filesystem == FLT_FSTYPE_NTFS || filesystem == FLT_FSTYPE_REFS
                   ? ntl::status{STATUS_SUCCESS}
                   : ntl::status{STATUS_FLT_DO_NOT_ATTACH};
      })
      .on_unload([](ntl::flt::unload_flags) noexcept {
        return ntl::status{STATUS_SUCCESS};
      });
  return driver.start(std::move(callbacks));
}
