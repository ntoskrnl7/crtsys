#include <ntl/flt/all>

#include "../delete_shared/delete_runtime.hpp"

#include <atomic>
#include <cstdint>
#include <string_view>
#include <utility>

namespace {

using namespace crtsys_flt_delete_runtime_test;

std::atomic<NTSTATUS> last_cleanup_status{STATUS_NOT_SUPPORTED};
std::atomic<std::uint32_t> create_delete_on_close{0};
std::atomic<std::uint32_t> legacy_requests{0};
std::atomic<std::uint32_t> extended_requests{0};
std::atomic<std::uint32_t> delete_requests{0};
std::atomic<std::uint32_t> clear_requests{0};
std::atomic<std::uint32_t> on_close_requests{0};
std::atomic<std::uint32_t> posix_requests{0};
std::atomic<std::uint32_t> force_image_section_requests{0};
std::atomic<std::uint32_t> ignore_readonly_requests{0};
std::atomic<std::uint32_t> set_information_successes{0};
std::atomic<std::uint32_t> set_information_failures{0};
std::atomic<std::uint32_t> disposition_races{0};
std::atomic<std::uint32_t> race_gate_arrivals{0};
std::atomic<std::uint32_t> cleanup_checks{0};
std::atomic<std::uint32_t> cleanup_present{0};
std::atomic<std::uint32_t> file_deletions{0};
std::atomic<std::uint32_t> stream_deletions{0};
std::atomic<std::uint32_t> completion_states_created{0};
std::atomic<std::uint32_t> completion_states_destroyed{0};
std::atomic<std::uint32_t> stream_contexts_created{0};
std::atomic<std::uint32_t> stream_contexts_destroyed{0};
std::atomic<long> race_gate_armed{0};
KEVENT race_gate_event;

constexpr std::wstring_view test_path =
    LR"(\crtsys-flt-delete-runtime)";

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

bool path_prefix(std::wstring_view path, std::wstring_view prefix) noexcept {
  if (path.size() < prefix.size() ||
      !equal_name(path.substr(0, prefix.size()), prefix)) {
    return false;
  }
  return path.size() == prefix.size() || path[prefix.size()] == L'\\';
}

bool is_test_name(ntl::flt::name_information &name) noexcept {
  if (name.try_parse().is_err() || name.name().size() < name.volume().size())
    return false;
  return path_prefix(name.name().substr(name.volume().size()), test_path);
}

bool is_race_name(const ntl::flt::name_information &name) noexcept {
  return equal_name(name.final_component(), race_name);
}

bool is_named_stream(const ntl::flt::name_information &name) noexcept {
  const std::wstring_view stream = name.stream();
  return !stream.empty() && !equal_name(stream, L":$DATA") &&
         !equal_name(stream, L"::$DATA");
}

struct stream_state {
  stream_state() noexcept {
    stream_contexts_created.fetch_add(1, std::memory_order_release);
  }

  ~stream_state() noexcept {
    stream_contexts_destroyed.fetch_add(1, std::memory_order_release);
  }

  bool should_check() const noexcept {
    return uncertain.load(std::memory_order_acquire) ||
           set_disposition.load(std::memory_order_acquire) ||
           delete_on_close.load(std::memory_order_acquire);
  }

  std::atomic<long> in_flight{0};
  std::atomic<bool> uncertain{false};
  std::atomic<bool> set_disposition{false};
  std::atomic<bool> delete_on_close{false};
  std::atomic<bool> notified{false};
};

inline constexpr ntl::flt::stream_context<stream_state>
    delete_stream_context{};

using stream_state_ref =
    ntl::flt::context_ref<stream_state, ntl::flt::context_scope::stream>;

struct create_completion {
  create_completion() noexcept {
    completion_states_created.fetch_add(1, std::memory_order_release);
  }
  ~create_completion() noexcept {
    completion_states_destroyed.fetch_add(1, std::memory_order_release);
  }
};

struct disposition_completion {
  disposition_completion(stream_state_ref &&state,
                         ntl::flt::disposition_state_kind state_kind,
                         bool delete_requested) noexcept
      : state(std::move(state)), state_kind(state_kind),
        delete_requested(delete_requested) {
    completion_states_created.fetch_add(1, std::memory_order_release);
  }

  ~disposition_completion() noexcept {
    if (state)
      state->in_flight.fetch_sub(1, std::memory_order_acq_rel);
    completion_states_destroyed.fetch_add(1, std::memory_order_release);
  }

  stream_state_ref state;
  ntl::flt::disposition_state_kind state_kind;
  bool delete_requested;
};

struct cleanup_completion {
  cleanup_completion(stream_state_ref &&state, bool named_stream) noexcept
      : state(std::move(state)), named_stream(named_stream) {
    completion_states_created.fetch_add(1, std::memory_order_release);
  }

  ~cleanup_completion() noexcept {
    completion_states_destroyed.fetch_add(1, std::memory_order_release);
  }

  stream_state_ref state;
  bool named_stream;
};

void reset_counters() noexcept {
  last_cleanup_status.store(STATUS_NOT_SUPPORTED, std::memory_order_release);
  create_delete_on_close.store(0, std::memory_order_release);
  legacy_requests.store(0, std::memory_order_release);
  extended_requests.store(0, std::memory_order_release);
  delete_requests.store(0, std::memory_order_release);
  clear_requests.store(0, std::memory_order_release);
  on_close_requests.store(0, std::memory_order_release);
  posix_requests.store(0, std::memory_order_release);
  force_image_section_requests.store(0, std::memory_order_release);
  ignore_readonly_requests.store(0, std::memory_order_release);
  set_information_successes.store(0, std::memory_order_release);
  set_information_failures.store(0, std::memory_order_release);
  disposition_races.store(0, std::memory_order_release);
  race_gate_arrivals.store(0, std::memory_order_release);
  cleanup_checks.store(0, std::memory_order_release);
  cleanup_present.store(0, std::memory_order_release);
  file_deletions.store(0, std::memory_order_release);
  stream_deletions.store(0, std::memory_order_release);
  completion_states_created.store(0, std::memory_order_release);
  completion_states_destroyed.store(0, std::memory_order_release);
  stream_contexts_created.store(0, std::memory_order_release);
  stream_contexts_destroyed.store(0, std::memory_order_release);
  race_gate_armed.store(0, std::memory_order_release);
  KeSetEvent(&race_gate_event, IO_NO_INCREMENT, FALSE);
}

ntl::flt::pre_result
pre_create(ntl::flt::create_callback_data data, ntl::flt::related_objects,
           ntl::flt::completion_slot<create_completion> &completion) noexcept {
  if (!data.parameters().delete_on_close())
    return ntl::flt::pre_result::success_no_callback;

  auto name = data.try_query_name(FLT_FILE_NAME_NORMALIZED |
                                  FLT_FILE_NAME_QUERY_DEFAULT |
                                  FLT_FILE_NAME_DO_NOT_CACHE);
  if (!name || !is_test_name(*name))
    return ntl::flt::pre_result::success_no_callback;

  create_delete_on_close.fetch_add(1, std::memory_order_release);
  if (completion.try_emplace().is_err())
    return ntl::flt::pre_result::success_no_callback;
  return ntl::flt::pre_result::synchronize;
}

void post_create(ntl::flt::create_callback_data data,
                 ntl::flt::related_objects objects,
                 ntl::flt::completion_ref<create_completion>) noexcept {
  if (data.io_status().is_err() || data.io_status() == STATUS_REPARSE)
    return;
  auto state = objects.try_get_or_create(delete_stream_context);
  if (state)
    state->get()->delete_on_close.store(true, std::memory_order_release);
}

void wait_for_race_peer() noexcept {
  const std::uint32_t arrival =
      race_gate_arrivals.fetch_add(1, std::memory_order_acq_rel) + 1;
  if (arrival >= 2) {
    race_gate_armed.store(0, std::memory_order_release);
    KeSetEvent(&race_gate_event, IO_NO_INCREMENT, FALSE);
    return;
  }

  LARGE_INTEGER timeout{};
  timeout.QuadPart = -5LL * 10LL * 1000LL * 1000LL;
  (void)KeWaitForSingleObject(&race_gate_event, Executive, KernelMode, FALSE,
                              &timeout);
}

ntl::flt::pre_result pre_set_information(
    ntl::flt::set_information_callback_data data,
    ntl::flt::related_objects objects,
    ntl::flt::completion_slot<disposition_completion>
        &completion) noexcept {
  const auto disposition = data.parameters().disposition();
  if (!disposition)
    return ntl::flt::pre_result::success_no_callback;

  auto name = data.try_query_name(FLT_FILE_NAME_NORMALIZED |
                                  FLT_FILE_NAME_QUERY_DEFAULT |
                                  FLT_FILE_NAME_DO_NOT_CACHE);
  if (!name || !is_test_name(*name))
    return ntl::flt::pre_result::success_no_callback;

  if (disposition.extended())
    extended_requests.fetch_add(1, std::memory_order_release);
  else
    legacy_requests.fetch_add(1, std::memory_order_release);
  if (disposition.delete_requested())
    delete_requests.fetch_add(1, std::memory_order_release);
  else
    clear_requests.fetch_add(1, std::memory_order_release);
  if (disposition.on_close())
    on_close_requests.fetch_add(1, std::memory_order_release);
  if (disposition.posix_semantics())
    posix_requests.fetch_add(1, std::memory_order_release);
  if (disposition.force_image_section_check())
    force_image_section_requests.fetch_add(1, std::memory_order_release);
  if (disposition.ignore_readonly_attribute())
    ignore_readonly_requests.fetch_add(1, std::memory_order_release);

  auto state = objects.try_get_or_create(delete_stream_context);
  if (!state)
    return ntl::flt::pre_result::success_no_callback;
  stream_state *const tracked = state->get();
  const long previous =
      tracked->in_flight.fetch_add(1, std::memory_order_acq_rel);
  if (previous != 0 &&
      !tracked->uncertain.exchange(true, std::memory_order_acq_rel)) {
    disposition_races.fetch_add(1, std::memory_order_release);
  }

  const ntl::status stored = completion.try_emplace(
      std::move(*state), disposition.state_kind(),
      disposition.delete_requested());
  if (stored.is_err()) {
    tracked->in_flight.fetch_sub(1, std::memory_order_acq_rel);
    return ntl::flt::pre_result::success_no_callback;
  }

  if (is_race_name(*name) &&
      race_gate_armed.load(std::memory_order_acquire) != 0) {
    wait_for_race_peer();
  }
  return ntl::flt::pre_result::synchronize;
}

void post_set_information(
    ntl::flt::set_information_callback_data data,
    ntl::flt::related_objects,
    ntl::flt::completion_ref<disposition_completion> completion) noexcept {
  if (!completion || !completion->state)
    return;
  if (data.io_status().is_err()) {
    set_information_failures.fetch_add(1, std::memory_order_release);
    return;
  }

  set_information_successes.fetch_add(1, std::memory_order_release);
  if (completion->state_kind ==
      ntl::flt::disposition_state_kind::delete_on_close) {
    completion->state->delete_on_close.store(completion->delete_requested,
                                             std::memory_order_release);
  } else {
    completion->state->set_disposition.store(completion->delete_requested,
                                             std::memory_order_release);
  }
}

ntl::flt::pre_result pre_cleanup(
    ntl::flt::cleanup_callback_data data, ntl::flt::related_objects objects,
    ntl::flt::completion_slot<cleanup_completion> &completion) noexcept {
  auto state = objects.try_get(delete_stream_context);
  if (!state || !state->get()->should_check() ||
      state->get()->notified.load(std::memory_order_acquire)) {
    return ntl::flt::pre_result::success_no_callback;
  }

  auto name =
      data.try_query_name(FLT_FILE_NAME_OPENED | FLT_FILE_NAME_QUERY_DEFAULT |
                          FLT_FILE_NAME_DO_NOT_CACHE);
  if (!name || name->try_parse().is_err())
    return ntl::flt::pre_result::success_no_callback;

  const bool named_stream = is_named_stream(*name);
  if (completion.try_emplace(std::move(*state), named_stream).is_err())
    return ntl::flt::pre_result::success_no_callback;
  return ntl::flt::pre_result::synchronize;
}

void post_cleanup(
    ntl::flt::cleanup_callback_data data, ntl::flt::related_objects,
    ntl::flt::completion_ref<cleanup_completion> completion) noexcept {
  if (!completion || !completion->state || data.io_status().is_err())
    return;

  cleanup_checks.fetch_add(1, std::memory_order_release);
  auto deletion =
      ntl::flt::try_query_cleanup_deletion(ntl::flt::as_post(data));
  if (!deletion) {
    last_cleanup_status.store(static_cast<NTSTATUS>(deletion.status()),
                              std::memory_order_release);
    return;
  }

  if (*deletion == ntl::flt::cleanup_deletion_state::present) {
    last_cleanup_status.store(STATUS_SUCCESS, std::memory_order_release);
    cleanup_present.fetch_add(1, std::memory_order_release);
    return;
  }

  last_cleanup_status.store(STATUS_FILE_DELETED, std::memory_order_release);
  if (completion->state->notified.exchange(true,
                                           std::memory_order_acq_rel)) {
    return;
  }
  if (completion->named_stream)
    stream_deletions.fetch_add(1, std::memory_order_release);
  else
    file_deletions.fetch_add(1, std::memory_order_release);
}

observations query_counters() noexcept {
  observations result;
  result.last_cleanup_status =
      last_cleanup_status.load(std::memory_order_acquire);
  result.create_delete_on_close =
      create_delete_on_close.load(std::memory_order_acquire);
  result.legacy_requests = legacy_requests.load(std::memory_order_acquire);
  result.extended_requests =
      extended_requests.load(std::memory_order_acquire);
  result.delete_requests = delete_requests.load(std::memory_order_acquire);
  result.clear_requests = clear_requests.load(std::memory_order_acquire);
  result.on_close_requests =
      on_close_requests.load(std::memory_order_acquire);
  result.posix_requests = posix_requests.load(std::memory_order_acquire);
  result.force_image_section_requests =
      force_image_section_requests.load(std::memory_order_acquire);
  result.ignore_readonly_requests =
      ignore_readonly_requests.load(std::memory_order_acquire);
  result.set_information_successes =
      set_information_successes.load(std::memory_order_acquire);
  result.set_information_failures =
      set_information_failures.load(std::memory_order_acquire);
  result.disposition_races =
      disposition_races.load(std::memory_order_acquire);
  result.race_gate_arrivals =
      race_gate_arrivals.load(std::memory_order_acquire);
  result.cleanup_checks = cleanup_checks.load(std::memory_order_acquire);
  result.cleanup_present = cleanup_present.load(std::memory_order_acquire);
  result.file_deletions = file_deletions.load(std::memory_order_acquire);
  result.stream_deletions = stream_deletions.load(std::memory_order_acquire);
  result.completion_states_created =
      completion_states_created.load(std::memory_order_acquire);
  result.completion_states_destroyed =
      completion_states_destroyed.load(std::memory_order_acquire);
  result.stream_contexts_created =
      stream_contexts_created.load(std::memory_order_acquire);
  result.stream_contexts_destroyed =
      stream_contexts_destroyed.load(std::memory_order_acquire);
  return result;
}

} // namespace

ntl::status ntl::flt::main(ntl::flt::driver &driver, std::wstring_view) {
  using namespace crtsys_flt_delete_runtime_test;

  KeInitializeEvent(&race_gate_event, NotificationEvent, TRUE);
  reset_counters();

  ntl::flt::communication_server messages;
  messages.contract(1)
      .on(query_observations, []() noexcept { return query_counters(); })
      .on(reset_observations, []() noexcept {
        reset_counters();
        return std::uint32_t{1};
      })
      .on(arm_race_gate, []() noexcept {
        race_gate_arrivals.store(0, std::memory_order_release);
        KeClearEvent(&race_gate_event);
        race_gate_armed.store(1, std::memory_order_release);
        return std::uint32_t{1};
      });
  const ntl::status port =
      driver.add_communication_port(port_name, std::move(messages));
  if (port.is_err())
    return port;

  ntl::flt::registration callbacks;
  callbacks
      .on_with_completion<create_completion>(ntl::flt::operation::create,
                                             &pre_create, &post_create)
      .on_with_completion<disposition_completion>(
          ntl::flt::operation::set_information, &pre_set_information,
          &post_set_information)
      .on_with_completion<cleanup_completion>(ntl::flt::operation::cleanup,
                                              &pre_cleanup, &post_cleanup)
      .on_instance_setup([](ntl::flt::related_objects,
                            FLT_INSTANCE_SETUP_FLAGS, DEVICE_TYPE,
                            FLT_FILESYSTEM_TYPE filesystem) noexcept {
        return filesystem == FLT_FSTYPE_NTFS
                   ? ntl::status{STATUS_SUCCESS}
                   : ntl::status{STATUS_FLT_DO_NOT_ATTACH};
      })
      .on_unload([](ntl::flt::unload_flags) noexcept {
        KeSetEvent(&race_gate_event, IO_NO_INCREMENT, FALSE);
        return ntl::status{STATUS_SUCCESS};
      })
      .context(delete_stream_context);
  return driver.start(std::move(callbacks));
}
