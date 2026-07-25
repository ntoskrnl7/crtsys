#include <ntl/flt/all>
#include <ntl/except>
#include <ntl/registry>

#include "../name_changer_shared/name_changer_runtime.hpp"
#include "../name_changer_shared/output_record_validation.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace crtsys_flt_name_changer_runtime_test;

std::wstring configured_user_mapping;
std::wstring configured_real_mapping;
std::atomic<std::uint64_t> generated_visible_names{0};
std::atomic<std::uint64_t> query_name_rewrites{0};
std::atomic<std::uint64_t> rename_reissues{0};
std::atomic<std::uint64_t> hard_link_reissues{0};
std::atomic<std::uint64_t> notification_requests{0};
std::atomic<std::uint64_t> usn_rewrites{0};
std::atomic<std::uint64_t> extended_directory_queries{0};
std::atomic<std::uint64_t> network_query_retries{0};
std::atomic<std::uint64_t> synthetic_file_id_64_layouts{0};
std::atomic<std::uint64_t> hard_link_query_rewrites{0};
std::atomic<std::uint64_t> enum_usn_rewrites{0};
std::atomic<std::uint64_t> read_journal_rewrites{0};
std::atomic<std::uint64_t> lookup_cluster_rewrites{0};
std::atomic<std::uint64_t> find_by_sid_rewrites{0};

enum class directory_role : std::uint8_t {
  none,
  user_parent,
  real_parent,
};

struct instance_state {
  PFLT_FILTER filter = nullptr;
  std::wstring volume_name;
  std::wstring user_full;
  std::wstring real_full;
  std::wstring user_parent;
  std::wstring real_parent;
  std::wstring user_relative;
  std::wstring real_relative;
  std::wstring user_final;
  std::wstring real_final;

  instance_state(PFLT_FILTER filter_value, std::wstring volume,
                 std::wstring user_name, std::wstring real_name,
                 std::wstring user_parent_name, std::wstring real_parent_name,
                 std::wstring user_relative_name,
                 std::wstring real_relative_name, std::wstring user_final_name,
                 std::wstring real_final_name) noexcept
      : filter(filter_value), volume_name(std::move(volume)),
        user_full(std::move(user_name)), real_full(std::move(real_name)),
        user_parent(std::move(user_parent_name)),
        real_parent(std::move(real_parent_name)),
        user_relative(std::move(user_relative_name)),
        real_relative(std::move(real_relative_name)),
        user_final(std::move(user_final_name)),
        real_final(std::move(real_final_name)) {}
};

struct enumeration_state {
  std::atomic<long> user_entry_emitted{0};
  std::atomic<long> user_pattern_matches{1};
  ERESOURCE find_by_sid_lock{};
  NTSTATUS find_by_sid_lock_status = STATUS_UNSUCCESSFUL;
  HANDLE find_by_sid_handle = nullptr;
  PFILE_OBJECT find_by_sid_file = nullptr;
  std::vector<unsigned char> find_by_sid_pending;
  std::size_t find_by_sid_offset = 0;
  bool find_by_sid_started = false;
  bool find_by_sid_exhausted = false;

  enumeration_state() noexcept {
    find_by_sid_lock_status = ExInitializeResourceLite(&find_by_sid_lock);
  }

  void close_find_by_sid_cursor() noexcept {
    if (find_by_sid_handle) {
      FltClose(find_by_sid_handle);
      find_by_sid_handle = nullptr;
    }
    if (find_by_sid_file) {
      ObDereferenceObject(find_by_sid_file);
      find_by_sid_file = nullptr;
    }
  }

  void reset_find_by_sid_search() noexcept {
    close_find_by_sid_cursor();
    find_by_sid_pending.clear();
    find_by_sid_offset = 0;
    find_by_sid_started = false;
    find_by_sid_exhausted = false;
  }

  ~enumeration_state() noexcept {
    close_find_by_sid_cursor();
    if (NT_SUCCESS(find_by_sid_lock_status))
      ExDeleteResourceLite(&find_by_sid_lock);
  }
};

struct create_completion {
  create_completion() noexcept = default;
  ~create_completion() noexcept = default;
};

struct query_information_completion {
  ntl::flt::prepared_query_information_output_buffer prepared_output;
  FILE_INFORMATION_CLASS information_class = FileDirectoryInformation;

  query_information_completion(
      ntl::flt::prepared_query_information_output_buffer &&output,
      FILE_INFORMATION_CLASS information_class_value) noexcept
      : prepared_output(std::move(output)),
        information_class(information_class_value) {}
};

enum class find_by_sid_mode : std::uint8_t {
  passthrough,
  translate_real_mapping,
  suppress_real_mapping,
  inject_real_mapping,
};

struct fsctl_completion {
  ntl::flt::prepared_fsctl_output_buffer prepared_output;
  ULONG control_code = 0;
  std::vector<unsigned char> input;
  std::wstring find_by_sid_request;
  ntl::flt::context_ref<enumeration_state,
                        ntl::flt::context_scope::stream_handle>
      find_by_sid_state;
  find_by_sid_mode find_by_sid = find_by_sid_mode::passthrough;

  explicit fsctl_completion(
      ntl::flt::prepared_fsctl_output_buffer &&output,
      ULONG control_code_value, std::vector<unsigned char> &&input_value,
      std::wstring &&request_name,
      ntl::flt::context_ref<enumeration_state,
                            ntl::flt::context_scope::stream_handle>
          &&state,
      find_by_sid_mode find_by_sid_value) noexcept
      : prepared_output(std::move(output)), control_code(control_code_value),
        input(std::move(input_value)),
        find_by_sid_request(std::move(request_name)),
        find_by_sid_state(std::move(state)),
        find_by_sid(find_by_sid_value) {}
};

inline constexpr ntl::flt::instance_context<instance_state> mapping_context{
    ntl::flt::context_pool::paged};
inline constexpr ntl::flt::stream_handle_context<enumeration_state>
    enumeration_context{};

class find_by_sid_state_lock {
public:
  explicit find_by_sid_state_lock(enumeration_state &state) noexcept
      : state_(state) {
    KeEnterCriticalRegion();
    (void)ExAcquireResourceExclusiveLite(&state_.find_by_sid_lock, TRUE);
  }

  find_by_sid_state_lock(const find_by_sid_state_lock &) = delete;
  find_by_sid_state_lock &operator=(const find_by_sid_state_lock &) = delete;

  ~find_by_sid_state_lock() noexcept {
    ExReleaseResourceLite(&state_.find_by_sid_lock);
    KeLeaveCriticalRegion();
  }

private:
  enumeration_state &state_;
};

struct directory_completion {
  ntl::flt::prepared_directory_output_buffer prepared_output;
  ntl::flt::context_ref<enumeration_state,
                        ntl::flt::context_scope::stream_handle>
      state;
  directory_role role = directory_role::none;
  bool may_emit_user_entry = false;
  bool return_single_entry = false;
  std::vector<unsigned char> injection_entry;

  directory_completion(
      ntl::flt::prepared_directory_output_buffer &&output,
      ntl::flt::context_ref<enumeration_state,
                            ntl::flt::context_scope::stream_handle>
          &&state_value,
      directory_role role_value, bool may_emit, bool return_single,
      std::vector<unsigned char> &&injection) noexcept
      : prepared_output(std::move(output)), state(std::move(state_value)),
        role(role_value), may_emit_user_entry(may_emit),
        return_single_entry(return_single),
        injection_entry(std::move(injection)) {}
};

struct directory_layout {
  std::size_t next_offset = 0;
  std::size_t name_length_offset = 0;
  std::size_t name_offset = 0;
  std::size_t attributes_offset = (std::numeric_limits<std::size_t>::max)();
  std::size_t short_name_length_offset =
      (std::numeric_limits<std::size_t>::max)();
  std::size_t short_name_offset = (std::numeric_limits<std::size_t>::max)();
};

struct stable_file_id {
  LONGLONG legacy = 0;
  FILE_ID_128 extended{};
};

constexpr std::size_t align_directory_record(std::size_t value) noexcept {
  return (value + 7u) & ~std::size_t{7u};
}

wchar_t fold_case(wchar_t value) noexcept {
  return RtlUpcaseUnicodeChar(value);
}

bool equal_name(std::wstring_view left, std::wstring_view right,
                bool ignore_case = true) noexcept {
  if (left.size() != right.size())
    return false;
  for (std::size_t index = 0; index != left.size(); ++index) {
    const wchar_t left_value =
        ignore_case ? fold_case(left[index]) : left[index];
    const wchar_t right_value =
        ignore_case ? fold_case(right[index]) : right[index];
    if (left_value != right_value)
      return false;
  }
  return true;
}

bool path_prefix(std::wstring_view path, std::wstring_view prefix,
                 bool ignore_case, std::wstring_view &remainder) noexcept {
  remainder = {};
  if (path.size() < prefix.size() ||
      !equal_name(path.substr(0, prefix.size()), prefix, ignore_case))
    return false;
  if (path.size() == prefix.size())
    return true;
  if (path[prefix.size()] != L'\\')
    return false;
  remainder = path.substr(prefix.size());
  return true;
}

bool is_path_ancestor(std::wstring_view candidate, std::wstring_view path,
                      bool ignore_case) noexcept {
  std::wstring_view remainder;
  return path_prefix(path, candidate, ignore_case, remainder) &&
         !remainder.empty();
}

std::wstring_view parent_path(std::wstring_view path) noexcept {
  const auto separator = path.find_last_of(L'\\');
  return separator == std::wstring_view::npos ? std::wstring_view{}
                                              : path.substr(0, separator);
}

std::wstring_view final_component(std::wstring_view path) noexcept {
  const auto separator = path.find_last_of(L'\\');
  return separator == std::wstring_view::npos ? path
                                              : path.substr(separator + 1);
}

bool valid_mapping(std::wstring_view value) noexcept {
  return value.size() > 2 && value.front() == L'\\' && value.back() != L'\\' &&
         parent_path(value).size() > 1 && !final_component(value).empty();
}

ntl::status initialize_configuration(std::wstring_view registry_path) noexcept {
  try {
    auto configuration = ntl::driver_config::open(std::wstring(registry_path));
    if (!configuration)
      return configuration.status();

    auto user = configuration->query_string(L"UserMapping");
    if (!user)
      return user.status();
    auto real = configuration->query_string(L"RealMapping");
    if (!real)
      return real.status();

    if (!valid_mapping(*user) || !valid_mapping(*real) ||
        equal_name(*user, *real))
      return STATUS_INVALID_PARAMETER;

    configured_user_mapping = std::move(*user);
    configured_real_mapping = std::move(*real);
    return STATUS_SUCCESS;
  } catch (...) {
    return STATUS_INSUFFICIENT_RESOURCES;
  }
}

ntl::status attach_mapping(ntl::flt::related_objects objects,
                           FLT_FILESYSTEM_TYPE filesystem_type) noexcept {
  if (filesystem_type != FLT_FSTYPE_NTFS && filesystem_type != FLT_FSTYPE_REFS)
    return STATUS_FLT_DO_NOT_ATTACH;

  auto information = objects.instance().try_information();
  if (!information)
    return information.status();

  try {
    const std::wstring user_full =
        information->volume_name + configured_user_mapping;
    const std::wstring real_full =
        information->volume_name + configured_real_mapping;
    const std::wstring user_parent_full(parent_path(user_full));
    const std::wstring real_parent_full(parent_path(real_full));
    const std::wstring user_final_name(final_component(user_full));
    const std::wstring real_final_name(final_component(real_full));

    auto state = objects.try_get_or_create(
        mapping_context, objects.filter().native_handle(),
        std::move(information->volume_name), std::wstring(user_full),
        std::wstring(real_full), std::wstring(user_parent_full),
        std::wstring(real_parent_full), std::wstring(configured_user_mapping),
        std::wstring(configured_real_mapping), std::wstring(user_final_name),
        std::wstring(real_final_name));
    return state ? ntl::status::ok() : state.status();
  } catch (...) {
    return STATUS_INSUFFICIENT_RESOURCES;
  }
}

ntl::flt::pre_result
pre_create(ntl::flt::create_callback_data data,
           ntl::flt::related_objects objects,
           ntl::flt::completion_slot<create_completion> &completion) noexcept {
  if (!data.target_file())
    return ntl::flt::pre_result::success_no_callback;
  if ((data.operation_flags() & SL_OPEN_PAGING_FILE) != 0 ||
      (data.target_file().native_object()->Flags & FO_VOLUME_OPEN) != 0 ||
      (data.parameters().create_options() & FILE_OPEN_BY_FILE_ID) != 0)
    return ntl::flt::pre_result::success_no_callback;

  auto state = objects.try_get(mapping_context);
  if (!state) {
    data.complete(state.status());
    return ntl::flt::pre_result::complete;
  }

  auto name =
      data.try_query_name(FLT_FILE_NAME_OPENED | FLT_FILE_NAME_QUERY_DEFAULT |
                          FLT_FILE_NAME_DO_NOT_CACHE);
  if (!name) {
    data.complete(name.status());
    return ntl::flt::pre_result::complete;
  }
  const ntl::status parsed = name->try_parse();
  if (parsed.is_err()) {
    data.complete(parsed);
    return ntl::flt::pre_result::complete;
  }

  const bool ignore_case = (data.operation_flags() & SL_CASE_SENSITIVE) == 0;
  std::wstring_view real_remainder;
  std::wstring_view user_remainder;
  const bool in_real = path_prefix(name->name(), (*state)->real_full,
                                   ignore_case, real_remainder);
  const bool in_user = path_prefix(name->name(), (*state)->user_full,
                                   ignore_case, user_remainder);

  if (in_real) {
    const UCHAR disposition = data.parameters().disposition();
    const NTSTATUS denied =
        real_remainder.empty() &&
                (disposition == FILE_OPEN || disposition == FILE_OVERWRITE)
            ? STATUS_OBJECT_NAME_NOT_FOUND
        : real_remainder.empty() ? STATUS_ACCESS_DENIED
                                 : STATUS_OBJECT_PATH_NOT_FOUND;
    data.complete(denied);
    return ntl::flt::pre_result::complete;
  }

  if ((data.parameters().create_options() & FILE_DELETE_ON_CLOSE) != 0 &&
      (is_path_ancestor(name->name(), (*state)->real_full, ignore_case) ||
       is_path_ancestor(name->name(), (*state)->user_full, ignore_case))) {
    data.complete(STATUS_ACCESS_DENIED);
    return ntl::flt::pre_result::complete;
  }

  if (!in_user)
    return ntl::flt::pre_result::success_no_callback;

  try {
    std::wstring redirected((*state)->real_relative);
    redirected.append(user_remainder);
    const ntl::status replaced =
        data.parameters().try_replace_target_name(redirected);
    if (replaced.is_err()) {
      data.complete(replaced);
      return ntl::flt::pre_result::complete;
    }
    data.parameters().clear_related_target();
    const ntl::status stored = completion.try_emplace();
    return stored.is_ok() ? ntl::flt::pre_result::success_with_callback
                          : ntl::flt::pre_result::success_no_callback;
  } catch (...) {
    data.complete(STATUS_INSUFFICIENT_RESOURCES);
    return ntl::flt::pre_result::complete;
  }
}

void post_create(ntl::flt::create_callback_data data,
                 ntl::flt::related_objects objects,
                 ntl::flt::completion_ref<create_completion>) noexcept {
  if (data.io_status().is_err())
    return;

  auto mapping = objects.try_get(mapping_context);
  if (!mapping)
    return;
  auto name = data.try_query_name(
      FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_DEFAULT |
      FLT_FILE_NAME_REQUEST_FROM_CURRENT_PROVIDER | FLT_FILE_NAME_DO_NOT_CACHE);
  if (!name || name->try_parse().is_err())
    return;

  std::wstring_view remainder;
  if (path_prefix(name->name(), (*mapping)->user_full, true, remainder))
    generated_visible_names.fetch_add(1, std::memory_order_relaxed);
}

ntl::flt::pre_result
pre_network_query_open(ntl::flt::network_query_open_callback_data data,
                       ntl::flt::related_objects objects, void *&) noexcept {
  const auto parameters = data.parameters();
  if (!data.is_fast_io_operation() || parameters.paging_file() ||
      parameters.open_by_file_id() || !data.target_file() ||
      (data.target_file().native_object()->Flags & FO_VOLUME_OPEN) != 0)
    return ntl::flt::pre_result::success_no_callback;

  auto mapping = objects.try_get(mapping_context);
  if (!mapping)
    return ntl::flt::pre_result::success_no_callback;
  auto name =
      data.try_query_name(FLT_FILE_NAME_OPENED | FLT_FILE_NAME_QUERY_DEFAULT |
                          FLT_FILE_NAME_DO_NOT_CACHE);
  if (!name)
    return ntl::flt::pre_result::success_no_callback;

  const bool ignore_case = !parameters.case_sensitive();
  std::wstring_view remainder;
  if (!path_prefix(name->name(), (*mapping)->user_full, ignore_case,
                   remainder) &&
      !path_prefix(name->name(), (*mapping)->real_full, ignore_case, remainder))
    return ntl::flt::pre_result::success_no_callback;

  network_query_retries.fetch_add(1, std::memory_order_relaxed);
  return ntl::flt::pre_result::disallow_fast_io;
}

bool query_returns_name(FILE_INFORMATION_CLASS information_class) noexcept {
  return information_class == FileNameInformation ||
         information_class == FileNormalizedNameInformation ||
         information_class == FileAllInformation ||
         information_class == FileAlternateNameInformation ||
         information_class == FileHardLinkInformation;
}

bool rewrite_counted_name(unsigned char *buffer, std::size_t capacity,
                          std::size_t &valid_bytes,
                          std::size_t name_length_offset,
                          std::size_t name_offset,
                          const instance_state &mapping) {
  if (!buffer || name_length_offset > valid_bytes ||
      sizeof(ULONG) > valid_bytes - name_length_offset ||
      name_offset > valid_bytes)
    return false;

  auto *const length = reinterpret_cast<ULONG *>(buffer + name_length_offset);
  if ((*length % sizeof(wchar_t)) != 0 || *length > valid_bytes - name_offset)
    return false;

  const std::wstring_view old_name{
      reinterpret_cast<const wchar_t *>(buffer + name_offset),
      *length / sizeof(wchar_t)};
  std::wstring_view remainder;
  std::wstring_view replacement;
  if (path_prefix(old_name, mapping.real_relative, true, remainder)) {
    replacement = mapping.user_relative;
  } else if (path_prefix(old_name, mapping.real_full, true, remainder)) {
    replacement = mapping.user_full;
  } else if (equal_name(old_name, mapping.real_final)) {
    replacement = mapping.user_final;
    remainder = {};
  } else {
    return false;
  }

  std::wstring visible(replacement);
  visible.append(remainder);
  const std::size_t name_bytes = visible.size() * sizeof(wchar_t);
  if (name_bytes > (std::numeric_limits<ULONG>::max)() ||
      name_offset > capacity || name_bytes > capacity - name_offset)
    return false;

  if (name_bytes != 0)
    RtlCopyMemory(buffer + name_offset, visible.data(), name_bytes);
  *length = static_cast<ULONG>(name_bytes);
  valid_bytes = name_offset + name_bytes;
  return true;
}

ntl::status query_path_file_id(const instance_state &mapping,
                               ntl::flt::instance instance,
                               std::wstring_view path, bool ignore_case,
                               stable_file_id &file_id) noexcept {
  HANDLE handle = nullptr;
  PFILE_OBJECT file = nullptr;
  try {
    constexpr auto maximum_characters =
        (std::numeric_limits<USHORT>::max)() / sizeof(wchar_t);
    if (!instance || path.size() > maximum_characters)
      return STATUS_NAME_TOO_LONG;

    UNICODE_STRING name{};
    name.Buffer = const_cast<PWCH>(path.data());
    name.Length = static_cast<USHORT>(path.size() * sizeof(wchar_t));
    name.MaximumLength = name.Length;
    OBJECT_ATTRIBUTES attributes{};
    InitializeObjectAttributes(
        &attributes, &name,
        OBJ_KERNEL_HANDLE | (ignore_case ? OBJ_CASE_INSENSITIVE : 0), nullptr,
        nullptr);

    IO_STATUS_BLOCK io_status{};
    IO_DRIVER_CREATE_CONTEXT create_context{};
    IoInitializeDriverCreateContext(&create_context);
    NTSTATUS status = FltCreateFileEx2(
        mapping.filter, instance.native_handle(), &handle, &file,
        FILE_READ_ATTRIBUTES | FILE_TRAVERSE, &attributes, &io_status, nullptr,
        FILE_ATTRIBUTE_NORMAL,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_OPEN,
        FILE_DIRECTORY_FILE, nullptr, 0, IO_IGNORE_SHARE_ACCESS_CHECK,
        &create_context);
    if (!NT_SUCCESS(status))
      return status;

    FILE_INTERNAL_INFORMATION information{};
    status = FltQueryInformationFile(
        instance.native_handle(), file, &information, sizeof(information),
        FileInternalInformation, nullptr);
    if (NT_SUCCESS(status)) {
      file_id.legacy = information.IndexNumber.QuadPart;
      RtlZeroMemory(&file_id.extended, sizeof(file_id.extended));
      RtlCopyMemory(&file_id.extended, &file_id.legacy,
                    sizeof(file_id.legacy));

      FILE_ID_INFORMATION extended{};
      const NTSTATUS extended_status = FltQueryInformationFile(
          instance.native_handle(), file, &extended, sizeof(extended),
          FileIdInformation, nullptr);
      if (NT_SUCCESS(extended_status))
        file_id.extended = extended.FileId;
    }

    FltClose(handle);
    ObDereferenceObject(file);
    return status;
  } catch (...) {
    if (handle)
      FltClose(handle);
    if (file)
      ObDereferenceObject(file);
    return STATUS_INSUFFICIENT_RESOURCES;
  }
}

ntl::status rewrite_hard_link_information(
    unsigned char *buffer, std::size_t capacity, std::size_t source_bytes,
    const instance_state &mapping, LONGLONG real_parent_id,
    LONGLONG user_parent_id, std::size_t &bytes_written,
    std::uint64_t &rewritten_entries) {
  constexpr std::size_t header_bytes =
      offsetof(FILE_LINKS_INFORMATION, Entry);
  constexpr std::size_t name_offset =
      offsetof(FILE_LINK_ENTRY_INFORMATION, FileName);
  bytes_written = 0;
  rewritten_entries = 0;
  if (!buffer || source_bytes < header_bytes || capacity < header_bytes)
    return STATUS_DATA_ERROR;

  std::vector<unsigned char> source(source_bytes);
  RtlCopyMemory(source.data(), buffer, source_bytes);
  const auto *const source_header =
      reinterpret_cast<const FILE_LINKS_INFORMATION *>(source.data());
  auto *const destination_header =
      reinterpret_cast<FILE_LINKS_INFORMATION *>(buffer);
  destination_header->BytesNeeded = static_cast<ULONG>(header_bytes);
  destination_header->EntriesReturned = 0;
  bytes_written = header_bytes;

  std::size_t source_offset = header_bytes;
  std::size_t previous_destination =
      (std::numeric_limits<std::size_t>::max)();
  std::size_t required = header_bytes;
  for (ULONG index = 0; index != source_header->EntriesReturned; ++index) {
    if (source_offset > source_bytes ||
        name_offset > source_bytes - source_offset)
      return STATUS_DATA_ERROR;

    const auto *const source_entry =
        reinterpret_cast<const FILE_LINK_ENTRY_INFORMATION *>(
            source.data() + source_offset);
    const std::size_t remaining = source_bytes - source_offset;
    const std::size_t source_record_bytes =
        source_entry->NextEntryOffset != 0
            ? static_cast<std::size_t>(source_entry->NextEntryOffset)
            : remaining;
    if (source_record_bytes < name_offset ||
        source_record_bytes > remaining ||
        (source_entry->NextEntryOffset != 0 &&
         (source_entry->NextEntryOffset % 8) != 0) ||
        (index + 1 < source_header->EntriesReturned) !=
            (source_entry->NextEntryOffset != 0) ||
        source_entry->FileNameLength >
            (source_record_bytes - name_offset) / sizeof(wchar_t))
      return STATUS_DATA_ERROR;

    const std::wstring_view source_name{
        source_entry->FileName, source_entry->FileNameLength};
    const bool rewrite =
        source_entry->ParentFileId == real_parent_id &&
        equal_name(source_name, mapping.real_final);
    const std::wstring_view destination_name =
        rewrite ? std::wstring_view{mapping.user_final} : source_name;
    const std::size_t destination_name_bytes =
        destination_name.size() * sizeof(wchar_t);
    const std::size_t destination_record_bytes =
        align_directory_record(name_offset + destination_name_bytes);
    if (destination_record_bytes >
            (std::numeric_limits<ULONG>::max)() ||
        required >
            (std::numeric_limits<ULONG>::max)() -
                destination_record_bytes)
      return STATUS_INTEGER_OVERFLOW;
    required += destination_record_bytes;

    if (bytes_written <= capacity &&
        destination_record_bytes <= capacity - bytes_written) {
      auto *const destination_entry =
          reinterpret_cast<FILE_LINK_ENTRY_INFORMATION *>(buffer +
                                                          bytes_written);
      if (rewrite) {
        RtlZeroMemory(destination_entry, destination_record_bytes);
        destination_entry->ParentFileId = user_parent_id;
        destination_entry->FileNameLength =
            static_cast<ULONG>(destination_name.size());
        if (destination_name_bytes != 0) {
          RtlCopyMemory(destination_entry->FileName, destination_name.data(),
                        destination_name_bytes);
        }
        ++rewritten_entries;
      } else {
        RtlZeroMemory(destination_entry, destination_record_bytes);
        RtlCopyMemory(destination_entry, source_entry,
                      (std::min)(source_record_bytes,
                                 destination_record_bytes));
      }
      destination_entry->NextEntryOffset =
          static_cast<ULONG>(destination_record_bytes);
      if (previous_destination !=
          (std::numeric_limits<std::size_t>::max)()) {
        auto *const previous =
            reinterpret_cast<FILE_LINK_ENTRY_INFORMATION *>(
                buffer + previous_destination);
        previous->NextEntryOffset =
            static_cast<ULONG>(bytes_written - previous_destination);
      }
      previous_destination = bytes_written;
      bytes_written += destination_record_bytes;
      ++destination_header->EntriesReturned;
    }

    if (source_entry->NextEntryOffset == 0) {
      source_offset = source_bytes;
    } else {
      source_offset += source_entry->NextEntryOffset;
    }
  }

  if (source_offset != source_bytes &&
      source_header->EntriesReturned != 0)
    return STATUS_DATA_ERROR;
  if (previous_destination !=
      (std::numeric_limits<std::size_t>::max)()) {
    auto *const previous = reinterpret_cast<FILE_LINK_ENTRY_INFORMATION *>(
        buffer + previous_destination);
    previous->NextEntryOffset = 0;
  }
  destination_header->BytesNeeded = static_cast<ULONG>(required);
  return bytes_written == required ? STATUS_SUCCESS : STATUS_BUFFER_OVERFLOW;
}

ntl::flt::pre_result
pre_query_information(ntl::flt::query_information_callback_data data,
                      ntl::flt::related_objects,
                      ntl::flt::completion_slot<query_information_completion>
                          &completion) noexcept {
  const auto information_class = data.parameters().information_class();
  if (!query_returns_name(information_class))
    return ntl::flt::pre_result::success_no_callback;
  if (data.is_fast_io_operation())
    return ntl::flt::pre_result::disallow_fast_io;
  if (!data.is_irp_operation())
    return ntl::flt::pre_result::success_no_callback;

  auto prepared = ntl::flt::try_prepare_output_buffer(ntl::flt::as_pre(data));
  if (!prepared) {
    data.complete(prepared.status());
    return ntl::flt::pre_result::complete;
  }
  const ntl::status stored =
      completion.try_emplace(std::move(*prepared), information_class);
  if (stored.is_err()) {
    data.complete(stored);
    return ntl::flt::pre_result::complete;
  }
  return ntl::flt::pre_result::success_with_callback;
}

ntl::flt::post_continuation post_query_information(
    ntl::flt::query_information_callback_data, ntl::flt::related_objects,
    ntl::flt::completion_ref<query_information_completion>) noexcept {
  return ntl::flt::post_continuation::when_safe;
}

void safe_post_query_information(
    ntl::flt::safe_query_information_operation operation,
    ntl::flt::related_objects objects,
    ntl::flt::completion_ref<query_information_completion>
        completion) noexcept {
  if (!completion)
    return;

  auto data = operation.data();
  auto mapping = objects.try_get(mapping_context);
  if (!mapping)
    return;

  stable_file_id real_parent_id{};
  stable_file_id user_parent_id{};
  if (completion->information_class == FileHardLinkInformation) {
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
      return;
    const bool ignore_case =
        !objects.file() ||
        (objects.file().native_object()->Flags & FO_OPENED_CASE_SENSITIVE) == 0;
    ntl::status queried =
        query_path_file_id(**mapping, objects.instance(),
                           (*mapping)->real_parent, ignore_case,
                           real_parent_id);
    if (queried.is_ok()) {
      queried = query_path_file_id(**mapping, objects.instance(),
                                   (*mapping)->user_parent, ignore_case,
                                   user_parent_id);
    }
    if (queried.is_err()) {
      data.set_io_status(queried, 0);
      return;
    }
  }

  const ntl::status accessed = completion->prepared_output.try_visit(
      operation, [&](ntl::flt::prepared_output_buffer_view view) noexcept {
        if (NT_ERROR(static_cast<NTSTATUS>(data.io_status())))
          return;

        auto *const buffer = static_cast<unsigned char *>(view.data());
        const std::size_t capacity = view.capacity();
        std::size_t valid_bytes = view.valid_size();
        bool rewritten = false;
        try {
          if (completion->information_class == FileHardLinkInformation) {
            std::uint64_t rewritten_entries = 0;
            const ntl::status status = rewrite_hard_link_information(
                buffer, capacity, valid_bytes, **mapping,
                real_parent_id.legacy, user_parent_id.legacy, valid_bytes,
                rewritten_entries);
            data.set_io_status(status, valid_bytes);
            if (rewritten_entries != 0) {
              hard_link_query_rewrites.fetch_add(rewritten_entries,
                                                 std::memory_order_relaxed);
            }
            return;
          } else if (completion->information_class == FileAllInformation) {
            constexpr std::size_t name_information_offset =
                offsetof(FILE_ALL_INFORMATION, NameInformation);
            rewritten = rewrite_counted_name(
                buffer, capacity, valid_bytes,
                name_information_offset +
                    offsetof(FILE_NAME_INFORMATION, FileNameLength),
                name_information_offset +
                    offsetof(FILE_NAME_INFORMATION, FileName),
                **mapping);
          } else {
            rewritten = rewrite_counted_name(
                buffer, capacity, valid_bytes,
                offsetof(FILE_NAME_INFORMATION, FileNameLength),
                offsetof(FILE_NAME_INFORMATION, FileName), **mapping);
          }
        } catch (...) {
          data.set_io_status(STATUS_INSUFFICIENT_RESOURCES, 0);
          return;
        }
        if (rewritten) {
          data.set_io_status(data.io_status(), valid_bytes);
          query_name_rewrites.fetch_add(1, std::memory_order_relaxed);
        }
      });
  if (accessed.is_err())
    data.set_io_status(accessed, 0);
}

bool equal_file_id(const FILE_ID_128 &left,
                   const FILE_ID_128 &right) noexcept {
  return RtlCompareMemory(&left, &right, sizeof(left)) == sizeof(left);
}

record_validation::usn_name_record_layout
usn_layout(const USN_RECORD_COMMON_HEADER &header) noexcept {
  if (header.MajorVersion == 2) {
    return {2, offsetof(USN_RECORD_V2, FileName),
            offsetof(USN_RECORD_V2, FileNameLength),
            offsetof(USN_RECORD_V2, FileNameOffset)};
  }
  if (header.MajorVersion == 3) {
    return {3, offsetof(USN_RECORD_V3, FileName),
            offsetof(USN_RECORD_V3, FileNameLength),
            offsetof(USN_RECORD_V3, FileNameOffset)};
  }
  return {};
}

ntl::status rewrite_usn_records(
    unsigned char *buffer, std::size_t capacity, std::size_t source_bytes,
    std::size_t records_offset, const instance_state &mapping,
    const stable_file_id &real_parent_id,
    const stable_file_id &user_parent_id, std::size_t &bytes_written,
    std::uint64_t &rewritten_records) {
  bytes_written = 0;
  rewritten_records = 0;
  if (!buffer || records_offset > source_bytes ||
      records_offset > capacity)
    return STATUS_DATA_ERROR;

  std::vector<unsigned char> source(source_bytes);
  RtlCopyMemory(source.data(), buffer, source_bytes);
  if (records_offset != 0)
    RtlCopyMemory(buffer, source.data(), records_offset);
  bytes_written = records_offset;

  std::size_t source_offset = records_offset;
  while (source_offset < source_bytes) {
    if (sizeof(USN_RECORD_COMMON_HEADER) > source_bytes - source_offset)
      return STATUS_DATA_ERROR;
    const auto *const header =
        reinterpret_cast<const USN_RECORD_COMMON_HEADER *>(source.data() +
                                                           source_offset);
    const auto layout = usn_layout(*header);
    if (layout.major_version == 0)
      return STATUS_NOT_SUPPORTED;

    record_validation::bounded_name name;
    if (!record_validation::try_read_usn_name(
            source.data() + source_offset, source_bytes - source_offset,
            layout, name) ||
        header->RecordLength == 0 ||
        (header->RecordLength % 8) != 0)
      return STATUS_DATA_ERROR;

    const std::wstring_view source_name{
        reinterpret_cast<const wchar_t *>(source.data() + source_offset +
                                          name.offset),
        name.size_bytes / sizeof(wchar_t)};
    bool parent_matches = false;
    if (header->MajorVersion == 2) {
      const auto *const record =
          reinterpret_cast<const USN_RECORD_V2 *>(header);
      parent_matches =
          static_cast<LONGLONG>(record->ParentFileReferenceNumber) ==
          real_parent_id.legacy;
    } else {
      const auto *const record =
          reinterpret_cast<const USN_RECORD_V3 *>(header);
      parent_matches = equal_file_id(record->ParentFileReferenceNumber,
                                     real_parent_id.extended);
    }
    const bool rewrite =
        parent_matches && equal_name(source_name, mapping.real_final);
    const std::size_t visible_name_bytes =
        mapping.user_final.size() * sizeof(wchar_t);
    const std::size_t destination_record_bytes =
        rewrite
            ? align_directory_record(name.offset + visible_name_bytes)
            : static_cast<std::size_t>(header->RecordLength);
    if (destination_record_bytes >
        (std::numeric_limits<ULONG>::max)())
      return STATUS_INTEGER_OVERFLOW;
    if (bytes_written > capacity ||
        destination_record_bytes > capacity - bytes_written)
      return bytes_written == records_offset ? STATUS_BUFFER_TOO_SMALL
                                             : STATUS_BUFFER_OVERFLOW;

    auto *const destination = buffer + bytes_written;
    if (!rewrite) {
      RtlCopyMemory(destination, header, destination_record_bytes);
    } else {
      RtlZeroMemory(destination, destination_record_bytes);
      RtlCopyMemory(destination, header, name.offset);
      auto *const destination_header =
          reinterpret_cast<USN_RECORD_COMMON_HEADER *>(destination);
      destination_header->RecordLength =
          static_cast<ULONG>(destination_record_bytes);
      if (header->MajorVersion == 2) {
        auto *const record =
            reinterpret_cast<USN_RECORD_V2 *>(destination);
        record->ParentFileReferenceNumber =
            static_cast<DWORDLONG>(user_parent_id.legacy);
        record->FileNameLength = static_cast<USHORT>(visible_name_bytes);
        record->FileNameOffset = static_cast<USHORT>(name.offset);
      } else {
        auto *const record =
            reinterpret_cast<USN_RECORD_V3 *>(destination);
        record->ParentFileReferenceNumber = user_parent_id.extended;
        record->FileNameLength = static_cast<USHORT>(visible_name_bytes);
        record->FileNameOffset = static_cast<USHORT>(name.offset);
      }
      if (visible_name_bytes != 0) {
        RtlCopyMemory(destination + name.offset, mapping.user_final.data(),
                      visible_name_bytes);
      }
      ++rewritten_records;
    }
    bytes_written += destination_record_bytes;
    source_offset += header->RecordLength;
  }
  return source_offset == source_bytes ? STATUS_SUCCESS : STATUS_DATA_ERROR;
}

ntl::status rewrite_lookup_stream_from_cluster(
    unsigned char *buffer, std::size_t capacity, std::size_t source_bytes,
    const instance_state &mapping, std::size_t &bytes_written,
    std::uint64_t &rewritten_entries) {
  constexpr std::size_t first_entry_offset =
      align_directory_record(sizeof(LOOKUP_STREAM_FROM_CLUSTER_OUTPUT));
  constexpr std::size_t name_offset =
      offsetof(LOOKUP_STREAM_FROM_CLUSTER_ENTRY, FileName);
  bytes_written = 0;
  rewritten_entries = 0;
  if (!buffer ||
      source_bytes < sizeof(LOOKUP_STREAM_FROM_CLUSTER_OUTPUT) ||
      capacity < sizeof(LOOKUP_STREAM_FROM_CLUSTER_OUTPUT))
    return STATUS_DATA_ERROR;

  std::vector<unsigned char> source(source_bytes);
  RtlCopyMemory(source.data(), buffer, source_bytes);
  const auto *const source_header =
      reinterpret_cast<const LOOKUP_STREAM_FROM_CLUSTER_OUTPUT *>(
          source.data());
  auto *const destination_header =
      reinterpret_cast<LOOKUP_STREAM_FROM_CLUSTER_OUTPUT *>(buffer);
  RtlZeroMemory(buffer, (std::min)(capacity, first_entry_offset));
  destination_header->NumberOfMatches = source_header->NumberOfMatches;
  destination_header->BufferSizeRequired =
      static_cast<ULONG>(first_entry_offset);
  bytes_written = first_entry_offset;

  if (source_header->Offset == 0) {
    destination_header->Offset = 0;
    return STATUS_SUCCESS;
  }
  if (source_header->Offset < first_entry_offset ||
      source_header->Offset >= source_bytes)
    return STATUS_DATA_ERROR;

  std::size_t source_offset = source_header->Offset;
  std::size_t previous_destination =
      (std::numeric_limits<std::size_t>::max)();
  std::size_t required = first_entry_offset;
  for (;;) {
    if (source_offset > source_bytes ||
        name_offset > source_bytes - source_offset)
      return STATUS_DATA_ERROR;
    const auto *const source_entry =
        reinterpret_cast<const LOOKUP_STREAM_FROM_CLUSTER_ENTRY *>(
            source.data() + source_offset);
    const std::size_t remaining = source_bytes - source_offset;
    const std::size_t source_record_bytes =
        source_entry->OffsetToNext != 0
            ? static_cast<std::size_t>(source_entry->OffsetToNext)
            : remaining;
    if (source_record_bytes < name_offset ||
        source_record_bytes > remaining ||
        (source_entry->OffsetToNext != 0 &&
         (source_entry->OffsetToNext % 8) != 0))
      return STATUS_DATA_ERROR;

    const std::size_t name_capacity =
        (source_record_bytes - name_offset) / sizeof(wchar_t);
    std::size_t name_characters = 0;
    while (name_characters < name_capacity &&
           source_entry->FileName[name_characters] != L'\0')
      ++name_characters;
    if (name_characters == name_capacity)
      return STATUS_DATA_ERROR;

    const std::wstring_view source_name{source_entry->FileName,
                                        name_characters};
    std::wstring_view remainder;
    std::wstring destination_name;
    bool rewrite = false;
    if (path_prefix(source_name, mapping.real_relative, true, remainder)) {
      destination_name.assign(mapping.user_relative);
      destination_name.append(remainder);
      rewrite = true;
    } else if (path_prefix(source_name, mapping.real_full, true, remainder)) {
      destination_name.assign(mapping.user_full);
      destination_name.append(remainder);
      rewrite = true;
    } else {
      destination_name.assign(source_name);
    }

    const std::size_t destination_name_bytes =
        (destination_name.size() + 1) * sizeof(wchar_t);
    const std::size_t destination_record_bytes =
        align_directory_record(name_offset + destination_name_bytes);
    if (destination_record_bytes >
            (std::numeric_limits<ULONG>::max)() ||
        required >
            (std::numeric_limits<ULONG>::max)() -
                destination_record_bytes)
      return STATUS_INTEGER_OVERFLOW;
    required += destination_record_bytes;

    if (bytes_written <= capacity &&
        destination_record_bytes <= capacity - bytes_written) {
      auto *const destination_entry =
          reinterpret_cast<LOOKUP_STREAM_FROM_CLUSTER_ENTRY *>(
              buffer + bytes_written);
      RtlZeroMemory(destination_entry, destination_record_bytes);
      destination_entry->Flags = source_entry->Flags;
      destination_entry->Reserved = source_entry->Reserved;
      destination_entry->Cluster = source_entry->Cluster;
      RtlCopyMemory(destination_entry->FileName, destination_name.data(),
                    destination_name.size() * sizeof(wchar_t));
      destination_entry->FileName[destination_name.size()] = L'\0';
      destination_entry->OffsetToNext =
          static_cast<ULONG>(destination_record_bytes);
      if (previous_destination !=
          (std::numeric_limits<std::size_t>::max)()) {
        auto *const previous =
            reinterpret_cast<LOOKUP_STREAM_FROM_CLUSTER_ENTRY *>(
                buffer + previous_destination);
        previous->OffsetToNext =
            static_cast<ULONG>(bytes_written - previous_destination);
      } else {
        destination_header->Offset = static_cast<ULONG>(bytes_written);
      }
      previous_destination = bytes_written;
      bytes_written += destination_record_bytes;
      if (rewrite)
        ++rewritten_entries;
    }

    if (source_entry->OffsetToNext == 0)
      break;
    source_offset += source_entry->OffsetToNext;
  }

  if (previous_destination !=
      (std::numeric_limits<std::size_t>::max)()) {
    auto *const previous =
        reinterpret_cast<LOOKUP_STREAM_FROM_CLUSTER_ENTRY *>(
            buffer + previous_destination);
    previous->OffsetToNext = 0;
  } else {
    destination_header->Offset = 0;
  }
  destination_header->BufferSizeRequired = static_cast<ULONG>(required);
  bytes_written = (std::min)(bytes_written, capacity);
  return STATUS_SUCCESS;
}

ntl::status translate_find_by_sid_name(
    std::wstring_view source_name, std::wstring_view request_name,
    const instance_state &mapping, find_by_sid_mode mode,
    std::wstring &destination_name, bool &omit, bool &rewritten) {
  omit = false;
  rewritten = false;
  destination_name.assign(source_name);
  if (mode == find_by_sid_mode::passthrough ||
      mode == find_by_sid_mode::inject_real_mapping)
    return STATUS_SUCCESS;

  std::wstring full_name(request_name);
  if (!source_name.empty()) {
    if (full_name.empty() || full_name.back() != L'\\')
      full_name.push_back(L'\\');
    if (source_name.front() == L'\\')
      source_name.remove_prefix(1);
    full_name.append(source_name);
  }

  std::wstring_view mapping_remainder;
  if (!path_prefix(full_name, mapping.real_full, true, mapping_remainder))
    return STATUS_SUCCESS;
  if (mode == find_by_sid_mode::suppress_real_mapping) {
    omit = true;
    rewritten = true;
    destination_name.clear();
    return STATUS_SUCCESS;
  }

  std::wstring visible_full(mapping.user_full);
  visible_full.append(mapping_remainder);
  std::wstring_view request_remainder;
  if (!path_prefix(visible_full, request_name, true, request_remainder))
    return STATUS_OBJECT_PATH_INVALID;
  if (!request_remainder.empty() && request_remainder.front() == L'\\')
    request_remainder.remove_prefix(1);
  destination_name.assign(request_remainder);
  rewritten = true;
  return STATUS_SUCCESS;
}

bool validate_packed_find_by_sid(const unsigned char *buffer,
                                 std::size_t source_bytes) noexcept {
  constexpr std::size_t name_offset =
      offsetof(FILE_NAME_INFORMATION, FileName);
  std::size_t offset = 0;
  while (offset < source_bytes) {
    if (name_offset > source_bytes - offset)
      return false;
    const std::size_t name_bytes =
        record_validation::load_u32(buffer + offset);
    if ((name_bytes % sizeof(wchar_t)) != 0 ||
        name_bytes > source_bytes - offset - name_offset)
      return false;
    const std::size_t minimum_record_bytes = name_offset + name_bytes;
    const std::size_t aligned_record_bytes =
        align_directory_record(name_offset + name_bytes);
    const std::size_t remaining = source_bytes - offset;
    const std::size_t record_bytes =
        aligned_record_bytes <= remaining ? aligned_record_bytes : remaining;
    if (record_bytes < minimum_record_bytes)
      return false;
    offset += record_bytes;
  }
  return offset == source_bytes;
}

bool validate_find_by_sid_input(const unsigned char *buffer,
                                std::size_t input_bytes) noexcept {
  constexpr std::size_t sid_offset = offsetof(FIND_BY_SID_DATA, Sid);
  constexpr std::size_t sid_header_bytes =
      offsetof(SID, SubAuthority);
  if (!buffer || input_bytes < sid_offset + sid_header_bytes)
    return false;
  const auto *const data =
      reinterpret_cast<const FIND_BY_SID_DATA *>(buffer);
  const auto *const sid = &data->Sid;
  if (sid->Revision != SID_REVISION ||
      sid->SubAuthorityCount > SID_MAX_SUB_AUTHORITIES)
    return false;
  const std::size_t required_sid_bytes =
      RtlLengthRequiredSid(sid->SubAuthorityCount);
  return required_sid_bytes <= input_bytes - sid_offset &&
         RtlValidSid(const_cast<SID *>(sid));
}

ntl::status rewrite_find_files_by_sid(
    unsigned char *buffer, std::size_t capacity, std::size_t source_bytes,
    const instance_state &mapping, std::wstring_view request_name,
    find_by_sid_mode mode, std::size_t &bytes_written,
    std::uint64_t &rewritten_entries) {
  bytes_written = 0;
  rewritten_entries = 0;
  constexpr std::size_t name_offset =
      offsetof(FILE_NAME_INFORMATION, FileName);
  if (!buffer || source_bytes <= name_offset)
    return STATUS_SUCCESS;

  if (!validate_packed_find_by_sid(buffer, source_bytes))
    return STATUS_INVALID_USER_BUFFER;

  std::vector<unsigned char> source(source_bytes);
  RtlCopyMemory(source.data(), buffer, source_bytes);
  std::size_t source_offset = 0;
  while (source_offset < source_bytes) {
    const std::size_t name_bytes =
        record_validation::load_u32(source.data() + source_offset);
    const std::size_t source_record_bytes = (std::min)(
        align_directory_record(name_offset + name_bytes),
        source_bytes - source_offset);
    const std::wstring_view source_name{
        reinterpret_cast<const wchar_t *>(source.data() + source_offset +
                                          name_offset),
        name_bytes / sizeof(wchar_t)};
    std::wstring destination_name;
    bool omit = false;
    bool rewrite = false;
    const ntl::status translated = translate_find_by_sid_name(
        source_name, request_name, mapping, mode, destination_name, omit,
        rewrite);
    if (translated.is_err())
      return translated;
    if (omit) {
      ++rewritten_entries;
      source_offset += source_record_bytes;
      continue;
    }
    const std::size_t destination_name_bytes =
        destination_name.size() * sizeof(wchar_t);
    const std::size_t destination_record_bytes =
        align_directory_record(name_offset + destination_name_bytes);
    if (bytes_written > capacity ||
        destination_record_bytes > capacity - bytes_written)
      return bytes_written == 0 ? STATUS_BUFFER_TOO_SMALL
                                : STATUS_BUFFER_OVERFLOW;

    auto *const destination = buffer + bytes_written;
    RtlZeroMemory(destination, destination_record_bytes);
    record_validation::store_u32(
        destination, static_cast<ULONG>(destination_name_bytes));
    if (destination_name_bytes != 0) {
      RtlCopyMemory(destination + name_offset, destination_name.data(),
                    destination_name_bytes);
    }
    if (rewrite)
      ++rewritten_entries;
    bytes_written += destination_record_bytes;
    source_offset += source_record_bytes;
  }
  return STATUS_SUCCESS;
}

ntl::status append_find_files_by_sid(
    unsigned char *buffer, std::size_t capacity, std::size_t &bytes_written,
    const unsigned char *source, std::size_t source_bytes,
    std::size_t &source_offset,
    const instance_state &mapping, std::uint64_t &injected_entries) {
  injected_entries = 0;
  constexpr std::size_t name_offset =
      offsetof(FILE_NAME_INFORMATION, FileName);
  if (!source || source_bytes <= name_offset ||
      source_offset >= source_bytes)
    return STATUS_SUCCESS;
  if (source_offset == 0 &&
      !validate_packed_find_by_sid(source, source_bytes))
    return STATUS_DATA_ERROR;

  while (source_offset < source_bytes) {
    const std::size_t name_bytes =
        record_validation::load_u32(source + source_offset);
    const std::size_t source_record_bytes = (std::min)(
        align_directory_record(name_offset + name_bytes),
        source_bytes - source_offset);
    const std::wstring_view source_name{
        reinterpret_cast<const wchar_t *>(source + source_offset +
                                          name_offset),
        name_bytes / sizeof(wchar_t)};

    std::wstring destination_name(mapping.user_final);
    if (!source_name.empty()) {
      if (source_name.front() != L'\\')
        destination_name.push_back(L'\\');
      destination_name.append(source_name);
    }

    const std::size_t destination_name_bytes =
        destination_name.size() * sizeof(wchar_t);
    if (destination_name_bytes > (std::numeric_limits<ULONG>::max)())
      return STATUS_INTEGER_OVERFLOW;
    const std::size_t destination_record_bytes =
        align_directory_record(name_offset + destination_name_bytes);
    if (bytes_written > capacity ||
        destination_record_bytes > capacity - bytes_written) {
      return bytes_written == 0 ? STATUS_BUFFER_TOO_SMALL : STATUS_SUCCESS;
    }

    auto *const destination = buffer + bytes_written;
    RtlZeroMemory(destination, destination_record_bytes);
    record_validation::store_u32(
        destination, static_cast<ULONG>(destination_name_bytes));
    if (destination_name_bytes != 0) {
      RtlCopyMemory(destination + name_offset, destination_name.data(),
                    destination_name_bytes);
    }
    bytes_written += destination_record_bytes;
    ++injected_entries;
    source_offset += source_record_bytes;
  }
  return STATUS_SUCCESS;
}

NTSTATUS open_real_mapping_find_by_sid_cursor(
    const instance_state &mapping, ntl::flt::instance instance,
    enumeration_state &state) noexcept {
  constexpr auto maximum_characters =
      (std::numeric_limits<USHORT>::max)() / sizeof(wchar_t);
  if (!instance || mapping.real_full.size() > maximum_characters)
    return STATUS_INVALID_PARAMETER;
  if (state.find_by_sid_file)
    return STATUS_SUCCESS;

  UNICODE_STRING name{};
  name.Buffer = const_cast<PWCH>(mapping.real_full.data());
  name.Length =
      static_cast<USHORT>(mapping.real_full.size() * sizeof(wchar_t));
  name.MaximumLength = name.Length;
  OBJECT_ATTRIBUTES attributes{};
  InitializeObjectAttributes(&attributes, &name,
                             OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
                             nullptr, nullptr);

  IO_STATUS_BLOCK io_status{};
  IO_DRIVER_CREATE_CONTEXT create_context{};
  IoInitializeDriverCreateContext(&create_context);
  const NTSTATUS status = FltCreateFileEx2(
      mapping.filter, instance.native_handle(), &state.find_by_sid_handle,
      &state.find_by_sid_file,
      FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES | FILE_TRAVERSE, &attributes,
      &io_status, nullptr, FILE_ATTRIBUTE_NORMAL,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_OPEN,
      FILE_DIRECTORY_FILE, nullptr, 0, IO_IGNORE_SHARE_ACCESS_CHECK,
      &create_context);
  if (!NT_SUCCESS(status))
    state.close_find_by_sid_cursor();
  return status;
}

NTSTATUS fetch_real_mapping_find_by_sid_page(
    const instance_state &mapping, ntl::flt::instance instance,
    const std::vector<unsigned char> &input,
    enumeration_state &state) noexcept {
  constexpr std::size_t page_capacity = 64 * 1024;
  state.find_by_sid_pending.clear();
  state.find_by_sid_offset = 0;
  if (state.find_by_sid_exhausted)
    return STATUS_NO_MORE_FILES;

  try {
    if (!validate_find_by_sid_input(input.data(), input.size()) ||
        input.size() > (std::numeric_limits<ULONG>::max)())
      return STATUS_INVALID_PARAMETER;

    NTSTATUS status =
        open_real_mapping_find_by_sid_cursor(mapping, instance, state);
    if (!NT_SUCCESS(status))
      return status;

    std::vector<unsigned char> private_input(input);
    auto *const find_data =
        reinterpret_cast<FIND_BY_SID_DATA *>(private_input.data());
    find_data->Restart = state.find_by_sid_started ? FALSE : TRUE;

    state.find_by_sid_pending.resize(page_capacity);
    ULONG returned = 0;
    status = FltFsControlFile(
        instance.native_handle(), state.find_by_sid_file,
        FSCTL_FIND_FILES_BY_SID, private_input.data(),
        static_cast<ULONG>(private_input.size()),
        state.find_by_sid_pending.data(),
        static_cast<ULONG>(state.find_by_sid_pending.size()), &returned);
    state.find_by_sid_started = true;

    if (returned > state.find_by_sid_pending.size()) {
      state.reset_find_by_sid_search();
      return STATUS_DATA_ERROR;
    }
    state.find_by_sid_pending.resize(returned);

    const bool terminal =
        status == STATUS_NO_MORE_FILES || status == STATUS_NO_MORE_ENTRIES ||
        status == STATUS_END_OF_FILE;
    if (terminal || (NT_SUCCESS(status) && returned == 0)) {
      state.find_by_sid_exhausted = true;
      state.close_find_by_sid_cursor();
      return returned == 0 ? STATUS_NO_MORE_FILES : STATUS_SUCCESS;
    }
    if (NT_SUCCESS(status) ||
        ((status == STATUS_BUFFER_OVERFLOW ||
          status == STATUS_BUFFER_TOO_SMALL) &&
         returned != 0))
      return STATUS_SUCCESS;

    state.find_by_sid_pending.clear();
    state.find_by_sid_offset = 0;
    if (!NT_SUCCESS(status))
      state.close_find_by_sid_cursor();
    return status;
  } catch (...) {
    state.reset_find_by_sid_search();
    return STATUS_INSUFFICIENT_RESOURCES;
  }
}

ntl::flt::pre_result pre_file_system_control(
    ntl::flt::file_system_control_callback_data data,
    ntl::flt::related_objects objects,
    ntl::flt::completion_slot<fsctl_completion> &completion) noexcept {
  const ULONG control_code = data.parameters().code();
  if (control_code != FSCTL_ENUM_USN_DATA &&
      control_code != FSCTL_FIND_FILES_BY_SID &&
      control_code != FSCTL_LOOKUP_STREAM_FROM_CLUSTER &&
      control_code != FSCTL_READ_FILE_USN_DATA &&
      control_code != FSCTL_READ_USN_JOURNAL)
    return ntl::flt::pre_result::success_no_callback;
  if (data.is_fast_io_operation())
    return ntl::flt::pre_result::disallow_fast_io;
  if (!data.is_irp_operation())
    return ntl::flt::pre_result::success_no_callback;

  auto mapping = objects.try_get(mapping_context);
  if (!mapping)
    return ntl::flt::pre_result::success_no_callback;
  if (control_code == FSCTL_READ_FILE_USN_DATA) {
    auto name = data.try_query_name(
        FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_DEFAULT |
        FLT_FILE_NAME_REQUEST_FROM_CURRENT_PROVIDER |
        FLT_FILE_NAME_DO_NOT_CACHE);
    if (!name || name->try_parse().is_err())
      return ntl::flt::pre_result::success_no_callback;
    std::wstring_view remainder;
    if (!path_prefix(name->name(), (*mapping)->user_full, true, remainder) ||
        !remainder.empty())
      return ntl::flt::pre_result::success_no_callback;
  }

  std::vector<unsigned char> input;
  std::wstring request_name;
  ntl::flt::context_ref<enumeration_state,
                        ntl::flt::context_scope::stream_handle>
      find_state;
  find_by_sid_mode mode = find_by_sid_mode::passthrough;
  if (control_code == FSCTL_FIND_FILES_BY_SID) {
    const ULONG input_length = data.parameters().input_length();
    void *const input_buffer = data.parameters().input_buffer();
    if (!input_buffer ||
        input_length < offsetof(FIND_BY_SID_DATA, Sid) + 8 ||
        input_length > offsetof(FIND_BY_SID_DATA, Sid) +
                           SECURITY_MAX_SID_SIZE)
      return ntl::flt::pre_result::success_no_callback;
    try {
      input.resize(input_length);
    } catch (...) {
      data.complete(STATUS_INSUFFICIENT_RESOURCES);
      return ntl::flt::pre_result::complete;
    }
    const auto [copied, exception] = ntl::seh::try_except(
        [&] { RtlCopyMemory(input.data(), input_buffer, input_length); });
    (void)exception;
    if (!copied || !validate_find_by_sid_input(input.data(), input.size()))
      return ntl::flt::pre_result::success_no_callback;

    auto name = data.try_query_name(
        FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_DEFAULT |
        FLT_FILE_NAME_REQUEST_FROM_CURRENT_PROVIDER |
        FLT_FILE_NAME_DO_NOT_CACHE);
    if (!name || name->try_parse().is_err())
      return ntl::flt::pre_result::success_no_callback;
    request_name.assign(name->name());
    while (request_name.size() > 1 && request_name.back() == L'\\')
      request_name.pop_back();

    std::wstring_view user_remainder;
    std::wstring_view real_remainder;
    const bool user_ancestor =
        path_prefix((*mapping)->user_full, request_name, true, user_remainder);
    const bool real_ancestor =
        path_prefix((*mapping)->real_full, request_name, true, real_remainder);
    if (!user_ancestor && !real_ancestor)
      return ntl::flt::pre_result::success_no_callback;
    if (user_ancestor && real_ancestor) {
      mode = find_by_sid_mode::translate_real_mapping;
    } else if (real_ancestor) {
      mode = find_by_sid_mode::suppress_real_mapping;
    } else if (!user_remainder.empty()) {
      mode = find_by_sid_mode::inject_real_mapping;
    }

    if (mode == find_by_sid_mode::inject_real_mapping) {
      auto state = objects.try_get_or_create(enumeration_context);
      if (!state) {
        data.complete(state.status());
        return ntl::flt::pre_result::complete;
      }
      find_state = std::move(*state);
      if (!NT_SUCCESS(find_state->find_by_sid_lock_status)) {
        data.complete(find_state->find_by_sid_lock_status);
        return ntl::flt::pre_result::complete;
      }
      if (reinterpret_cast<const FIND_BY_SID_DATA *>(input.data())->Restart) {
        find_by_sid_state_lock lock(*find_state);
        find_state->reset_find_by_sid_search();
      }
    }
  }

  auto prepared = ntl::flt::try_prepare_output_buffer(ntl::flt::as_pre(data));
  if (!prepared)
    return ntl::flt::pre_result::success_no_callback;

  const ntl::status stored = completion.try_emplace(
      std::move(*prepared), control_code, std::move(input),
      std::move(request_name), std::move(find_state), mode);
  return stored.is_ok() ? ntl::flt::pre_result::success_with_callback
                        : ntl::flt::pre_result::success_no_callback;
}

ntl::flt::post_continuation
post_file_system_control(ntl::flt::file_system_control_callback_data,
                         ntl::flt::related_objects,
                         ntl::flt::completion_ref<fsctl_completion>) noexcept {
  return ntl::flt::post_continuation::when_safe;
}

void safe_post_file_system_control(
    ntl::flt::safe_file_system_control_operation operation,
    ntl::flt::related_objects objects,
    ntl::flt::completion_ref<fsctl_completion> completion) noexcept {
  if (!completion)
    return;

  auto data = operation.data();
  auto mapping = objects.try_get(mapping_context);
  if (!mapping)
    return;

  stable_file_id real_parent_id{};
  stable_file_id user_parent_id{};
  if (completion->control_code == FSCTL_ENUM_USN_DATA ||
      completion->control_code == FSCTL_READ_FILE_USN_DATA ||
      completion->control_code == FSCTL_READ_USN_JOURNAL) {
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
      return;
    const bool ignore_case =
        !objects.file() ||
        (objects.file().native_object()->Flags & FO_OPENED_CASE_SENSITIVE) == 0;
    ntl::status queried =
        query_path_file_id(**mapping, objects.instance(),
                           (*mapping)->real_parent, ignore_case,
                           real_parent_id);
    if (queried.is_ok()) {
      queried = query_path_file_id(**mapping, objects.instance(),
                                   (*mapping)->user_parent, ignore_case,
                                   user_parent_id);
    }
    if (queried.is_err()) {
      data.set_io_status(queried, 0);
      return;
    }
  }

  const ntl::status accessed = completion->prepared_output.try_visit(
      operation, [&](ntl::flt::prepared_output_buffer_view view) noexcept {
        const NTSTATUS original_status =
            static_cast<NTSTATUS>(data.io_status());
        const bool find_by_sid_exhausted =
            completion->control_code == FSCTL_FIND_FILES_BY_SID &&
            completion->find_by_sid ==
                find_by_sid_mode::inject_real_mapping &&
            (original_status == STATUS_NO_MORE_FILES ||
             original_status == STATUS_NO_MORE_ENTRIES ||
             original_status == STATUS_END_OF_FILE);
        if (NT_ERROR(original_status) && !find_by_sid_exhausted)
          return;
        auto *const bytes = static_cast<unsigned char *>(view.data());
        const std::size_t returned = view.valid_size();
        if (!bytes)
          return;

        try {
          if (completion->control_code ==
              FSCTL_LOOKUP_STREAM_FROM_CLUSTER) {
            std::size_t written = 0;
            std::uint64_t rewritten = 0;
            const ntl::status status = rewrite_lookup_stream_from_cluster(
                bytes, view.capacity(), returned, **mapping, written,
                rewritten);
            data.set_io_status(status, written);
            if (rewritten != 0) {
              lookup_cluster_rewrites.fetch_add(rewritten,
                                                std::memory_order_relaxed);
            }
            return;
          }
          if (completion->control_code == FSCTL_FIND_FILES_BY_SID) {
            std::size_t written = 0;
            std::uint64_t rewritten = 0;
            ntl::status status = rewrite_find_files_by_sid(
                bytes, view.capacity(), returned, **mapping,
                completion->find_by_sid_request, completion->find_by_sid,
                written, rewritten);
            if (status.is_ok() && written == 0 &&
                completion->find_by_sid ==
                    find_by_sid_mode::inject_real_mapping &&
                completion->find_by_sid_state) {
              auto &state = *completion->find_by_sid_state;
              find_by_sid_state_lock lock(state);
              if (state.find_by_sid_pending.empty() &&
                  !state.find_by_sid_exhausted) {
                const NTSTATUS queried = fetch_real_mapping_find_by_sid_page(
                    **mapping, objects.instance(), completion->input, state);
                if (!NT_SUCCESS(queried) &&
                    queried != STATUS_NO_MORE_FILES) {
                  status = queried;
                  written = 0;
                }
              }
              if (status.is_ok() &&
                  state.find_by_sid_offset <
                      state.find_by_sid_pending.size()) {
                std::uint64_t injected = 0;
                status = append_find_files_by_sid(
                    bytes, view.capacity(), written,
                    state.find_by_sid_pending.data(),
                    state.find_by_sid_pending.size(),
                    state.find_by_sid_offset, **mapping, injected);
                rewritten += injected;
                if (state.find_by_sid_offset >=
                    state.find_by_sid_pending.size()) {
                  state.find_by_sid_pending.clear();
                  state.find_by_sid_offset = 0;
                }
              }
            }
            data.set_io_status(status, written);
            if (rewritten != 0) {
              find_by_sid_rewrites.fetch_add(rewritten,
                                             std::memory_order_relaxed);
            }
            return;
          }

          const std::size_t records_offset =
              completion->control_code == FSCTL_READ_FILE_USN_DATA
                  ? 0
                  : sizeof(USN);
          std::size_t written = 0;
          std::uint64_t rewritten = 0;
          const ntl::status status = rewrite_usn_records(
              bytes, view.capacity(), returned, records_offset, **mapping,
              real_parent_id, user_parent_id, written, rewritten);
          data.set_io_status(status, written);
          if (rewritten == 0)
            return;
          if (completion->control_code == FSCTL_ENUM_USN_DATA) {
            enum_usn_rewrites.fetch_add(rewritten,
                                        std::memory_order_relaxed);
          } else if (completion->control_code ==
                     FSCTL_READ_USN_JOURNAL) {
            read_journal_rewrites.fetch_add(rewritten,
                                            std::memory_order_relaxed);
          } else {
            usn_rewrites.fetch_add(rewritten, std::memory_order_relaxed);
          }
        } catch (...) {
          data.set_io_status(STATUS_INSUFFICIENT_RESOURCES, 0);
        }
      });
  if (accessed.is_err())
    data.set_io_status(accessed, 0);
}

ntl::flt::pre_result
pre_set_information(ntl::flt::set_information_callback_data data,
                    ntl::flt::related_objects objects, void *&) noexcept {
  const auto destination = data.parameters().destination();
  if (!destination)
    return ntl::flt::pre_result::success_no_callback;
  if (data.is_fast_io_operation())
    return ntl::flt::pre_result::disallow_fast_io;

  auto mapping = objects.try_get(mapping_context);
  if (!mapping)
    return ntl::flt::pre_result::success_no_callback;

  auto destination_name = ntl::flt::try_query_destination_name(
      ntl::flt::as_pre(data), FLT_FILE_NAME_OPENED |
                                  FLT_FILE_NAME_QUERY_DEFAULT |
                                  FLT_FILE_NAME_REQUEST_FROM_CURRENT_PROVIDER |
                                  FLT_FILE_NAME_DO_NOT_CACHE);
  if (!destination_name) {
    data.complete(destination_name.status());
    return ntl::flt::pre_result::complete;
  }
  if (destination_name->try_parse().is_err()) {
    data.complete(STATUS_OBJECT_NAME_INVALID);
    return ntl::flt::pre_result::complete;
  }

  const bool ignore_case = (data.operation_flags() & SL_CASE_SENSITIVE) == 0;
  std::wstring_view user_remainder;
  std::wstring_view real_remainder;
  const bool in_user =
      path_prefix(destination_name->name(), (*mapping)->user_full, ignore_case,
                  user_remainder);
  const bool in_real =
      path_prefix(destination_name->name(), (*mapping)->real_full, ignore_case,
                  real_remainder);
  if (in_real) {
    data.complete(real_remainder.empty() ? STATUS_ACCESS_DENIED
                                         : STATUS_OBJECT_PATH_NOT_FOUND);
    return ntl::flt::pre_result::complete;
  }
  if (!in_user)
    return ntl::flt::pre_result::success_no_callback;
  if (user_remainder.empty()) {
    data.complete(STATUS_ACCESS_DENIED);
    return ntl::flt::pre_result::complete;
  }

  try {
    std::wstring replacement((*mapping)->real_full);
    replacement.append(user_remainder);
    const ntl::status reissued =
        ntl::flt::try_reissue_destination(ntl::flt::as_pre(data), replacement);
    if (reissued.is_ok()) {
      if (destination.kind() ==
          ntl::flt::destination_information_kind::rename) {
        rename_reissues.fetch_add(1, std::memory_order_relaxed);
      } else {
        hard_link_reissues.fetch_add(1, std::memory_order_relaxed);
      }
    }
    return ntl::flt::pre_result::complete;
  } catch (...) {
    data.complete(STATUS_INSUFFICIENT_RESOURCES);
    return ntl::flt::pre_result::complete;
  }
}

ntl::status
generate_file_name(ntl::flt::name_generation_request request,
                   ntl::flt::name_generation_output output) noexcept {
  if (!request.target_instance() || !request.target_file() || !output)
    return STATUS_INVALID_PARAMETER;

  auto state = request.target_instance().try_get(mapping_context);
  if (!state)
    return state.status();

  const auto native_options = request.options().native();
  const auto query_method = FltGetFileNameQueryMethod(native_options);
  auto flags = native_options & FLT_VALID_FILE_NAME_FLAGS;
  flags &= ~FLT_FILE_NAME_REQUEST_FROM_CURRENT_PROVIDER;

  const bool return_short = request.options().short_name();
  const FLT_FILE_NAME_OPTIONS comparison_format = request.options().normalized()
                                                      ? FLT_FILE_NAME_NORMALIZED
                                                      : FLT_FILE_NAME_OPENED;
  auto lower =
      request.try_query_lower_name(comparison_format | query_method | flags);
  if (!lower)
    return lower.status();
  const ntl::status parsed = lower->try_parse();
  if (parsed.is_err())
    return parsed;

  const bool ignore_case =
      request.callback_data()
          ? (request.callback_data().operation_flags() & SL_CASE_SENSITIVE) == 0
          : (request.target_file().native_object()->Flags &
             FO_OPENED_CASE_SENSITIVE) == 0;
  std::wstring_view remainder;
  const bool in_real =
      path_prefix(lower->name(), (*state)->real_full, ignore_case, remainder);

  try {
    std::wstring result;
    if (return_short) {
      if (in_real && remainder.empty()) {
        result = (*state)->user_final;
      } else {
        auto short_name = request.try_query_lower_name(FLT_FILE_NAME_SHORT |
                                                       query_method | flags);
        if (!short_name)
          return short_name.status();
        result.assign(short_name->name());
      }
    } else if (in_real) {
      result = (*state)->user_full;
      result.append(remainder);
    } else {
      result.assign(lower->name());
    }

    const ntl::status assigned = output.name().try_assign(result);
    if (assigned.is_err())
      return assigned;
    output.set_cache(true);
    return STATUS_SUCCESS;
  } catch (...) {
    return STATUS_INSUFFICIENT_RESOURCES;
  }
}

ntl::status
normalize_name_component(ntl::flt::name_normalization_request request,
                         ntl::flt::name_normalization_output output) noexcept {
  if (!request.target_instance() || !output.expanded_name())
    return STATUS_INVALID_PARAMETER;

  output.context().set(nullptr);
  auto state = request.target_instance().try_get(mapping_context);
  if (!state)
    return state.status();

  const bool ignore_case = !request.flags().case_sensitive();
  std::wstring_view remainder;
  const bool parent_in_user = path_prefix(
      request.parent_directory(), (*state)->user_full, ignore_case, remainder);
  const bool mapping_component =
      equal_name(request.parent_directory(), (*state)->user_parent,
                 ignore_case) &&
      equal_name(request.component(), (*state)->user_final, ignore_case);

  HANDLE parent_handle = nullptr;
  PFILE_OBJECT parent_file = nullptr;
  try {
    std::wstring parent_name;
    std::wstring component_name;
    if (parent_in_user) {
      parent_name = (*state)->real_full;
      parent_name.append(remainder);
      component_name.assign(request.component());
    } else if (mapping_component) {
      parent_name = (*state)->real_parent;
      component_name = (*state)->real_final;
    } else {
      parent_name.assign(request.parent_directory());
      component_name.assign(request.component());
    }
    constexpr auto maximum_characters =
        (std::numeric_limits<USHORT>::max)() / sizeof(wchar_t);
    if (parent_name.size() > maximum_characters ||
        component_name.size() > maximum_characters)
      return STATUS_NAME_TOO_LONG;

    UNICODE_STRING native_parent{};
    native_parent.Buffer = parent_name.data();
    native_parent.Length =
        static_cast<USHORT>(parent_name.size() * sizeof(wchar_t));
    native_parent.MaximumLength = native_parent.Length;
    UNICODE_STRING native_component{};
    native_component.Buffer = component_name.data();
    native_component.Length =
        static_cast<USHORT>(component_name.size() * sizeof(wchar_t));
    native_component.MaximumLength = native_component.Length;

    OBJECT_ATTRIBUTES attributes{};
    InitializeObjectAttributes(&attributes, &native_parent,
                               OBJ_KERNEL_HANDLE |
                                   (ignore_case ? OBJ_CASE_INSENSITIVE : 0),
                               nullptr, nullptr);
    IO_STATUS_BLOCK io_status{};
    IO_DRIVER_CREATE_CONTEXT create_context{};
    IoInitializeDriverCreateContext(&create_context);
    const NTSTATUS opened = FltCreateFileEx2(
        (*state)->filter, request.target_instance().native_handle(),
        &parent_handle, &parent_file, FILE_LIST_DIRECTORY | FILE_TRAVERSE,
        &attributes, &io_status, nullptr, FILE_ATTRIBUTE_NORMAL,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_OPEN,
        FILE_DIRECTORY_FILE, nullptr, 0, IO_IGNORE_SHARE_ACCESS_CHECK,
        &create_context);
    if (!NT_SUCCESS(opened)) {
      if (parent_handle)
        FltClose(parent_handle);
      if (parent_file)
        ObDereferenceObject(parent_file);
      return opened;
    }

    const NTSTATUS queried = FltQueryDirectoryFile(
        request.target_instance().native_handle(), parent_file,
        output.expanded_name().native(), output.expanded_name().size_bytes(),
        FileNamesInformation, TRUE, &native_component, TRUE, nullptr);
    ntl::status result{queried};
    if (result.is_ok() && mapping_component)
      result = output.expanded_name().try_assign((*state)->user_final);

    FltClose(parent_handle);
    ObDereferenceObject(parent_file);
    parent_handle = nullptr;
    parent_file = nullptr;
    return result;
  } catch (...) {
    if (parent_handle)
      FltClose(parent_handle);
    if (parent_file)
      ObDereferenceObject(parent_file);
    return STATUS_INSUFFICIENT_RESOURCES;
  }
}

bool get_directory_layout(FILE_INFORMATION_CLASS information_class,
                          directory_layout &layout) noexcept {
  switch (information_class) {
  case FileDirectoryInformation:
    layout = {offsetof(FILE_DIRECTORY_INFORMATION, NextEntryOffset),
              offsetof(FILE_DIRECTORY_INFORMATION, FileNameLength),
              offsetof(FILE_DIRECTORY_INFORMATION, FileName),
              offsetof(FILE_DIRECTORY_INFORMATION, FileAttributes)};
    return true;
  case FileFullDirectoryInformation:
    layout = {offsetof(FILE_FULL_DIR_INFORMATION, NextEntryOffset),
              offsetof(FILE_FULL_DIR_INFORMATION, FileNameLength),
              offsetof(FILE_FULL_DIR_INFORMATION, FileName),
              offsetof(FILE_FULL_DIR_INFORMATION, FileAttributes)};
    return true;
  case FileBothDirectoryInformation:
    layout = {offsetof(FILE_BOTH_DIR_INFORMATION, NextEntryOffset),
              offsetof(FILE_BOTH_DIR_INFORMATION, FileNameLength),
              offsetof(FILE_BOTH_DIR_INFORMATION, FileName),
              offsetof(FILE_BOTH_DIR_INFORMATION, FileAttributes),
              offsetof(FILE_BOTH_DIR_INFORMATION, ShortNameLength),
              offsetof(FILE_BOTH_DIR_INFORMATION, ShortName)};
    return true;
  case FileNamesInformation:
    layout = {offsetof(FILE_NAMES_INFORMATION, NextEntryOffset),
              offsetof(FILE_NAMES_INFORMATION, FileNameLength),
              offsetof(FILE_NAMES_INFORMATION, FileName)};
    return true;
  case FileIdBothDirectoryInformation:
    layout = {offsetof(FILE_ID_BOTH_DIR_INFORMATION, NextEntryOffset),
              offsetof(FILE_ID_BOTH_DIR_INFORMATION, FileNameLength),
              offsetof(FILE_ID_BOTH_DIR_INFORMATION, FileName),
              offsetof(FILE_ID_BOTH_DIR_INFORMATION, FileAttributes),
              offsetof(FILE_ID_BOTH_DIR_INFORMATION, ShortNameLength),
              offsetof(FILE_ID_BOTH_DIR_INFORMATION, ShortName)};
    return true;
  case FileIdFullDirectoryInformation:
    layout = {offsetof(FILE_ID_FULL_DIR_INFORMATION, NextEntryOffset),
              offsetof(FILE_ID_FULL_DIR_INFORMATION, FileNameLength),
              offsetof(FILE_ID_FULL_DIR_INFORMATION, FileName),
              offsetof(FILE_ID_FULL_DIR_INFORMATION, FileAttributes)};
    return true;
  case FileIdExtdDirectoryInformation:
    layout = {offsetof(FILE_ID_EXTD_DIR_INFORMATION, NextEntryOffset),
              offsetof(FILE_ID_EXTD_DIR_INFORMATION, FileNameLength),
              offsetof(FILE_ID_EXTD_DIR_INFORMATION, FileName),
              offsetof(FILE_ID_EXTD_DIR_INFORMATION, FileAttributes)};
    return true;
  case FileIdExtdBothDirectoryInformation:
    layout = {offsetof(FILE_ID_EXTD_BOTH_DIR_INFORMATION, NextEntryOffset),
              offsetof(FILE_ID_EXTD_BOTH_DIR_INFORMATION, FileNameLength),
              offsetof(FILE_ID_EXTD_BOTH_DIR_INFORMATION, FileName),
              offsetof(FILE_ID_EXTD_BOTH_DIR_INFORMATION, FileAttributes),
              offsetof(FILE_ID_EXTD_BOTH_DIR_INFORMATION, ShortNameLength),
              offsetof(FILE_ID_EXTD_BOTH_DIR_INFORMATION, ShortName)};
    return true;
  case FileId64ExtdDirectoryInformation:
    layout = {offsetof(FILE_ID_64_EXTD_DIR_INFORMATION, NextEntryOffset),
              offsetof(FILE_ID_64_EXTD_DIR_INFORMATION, FileNameLength),
              offsetof(FILE_ID_64_EXTD_DIR_INFORMATION, FileName),
              offsetof(FILE_ID_64_EXTD_DIR_INFORMATION, FileAttributes)};
    return true;
  case FileId64ExtdBothDirectoryInformation:
    layout = {offsetof(FILE_ID_64_EXTD_BOTH_DIR_INFORMATION, NextEntryOffset),
              offsetof(FILE_ID_64_EXTD_BOTH_DIR_INFORMATION, FileNameLength),
              offsetof(FILE_ID_64_EXTD_BOTH_DIR_INFORMATION, FileName),
              offsetof(FILE_ID_64_EXTD_BOTH_DIR_INFORMATION, FileAttributes),
              offsetof(FILE_ID_64_EXTD_BOTH_DIR_INFORMATION, ShortNameLength),
              offsetof(FILE_ID_64_EXTD_BOTH_DIR_INFORMATION, ShortName)};
    return true;
  default:
    return false;
  }
}

record_validation::linked_name_record_layout
validation_layout(const directory_layout &layout) noexcept {
  return {layout.next_offset, layout.name_length_offset, layout.name_offset, 8};
}

bool pattern_may_match(PUNICODE_STRING pattern, std::wstring_view visible_name,
                       bool ignore_case) noexcept {
  if (!pattern || !pattern->Buffer || pattern->Length == 0)
    return true;
  constexpr auto maximum_characters =
      (std::numeric_limits<USHORT>::max)() / sizeof(wchar_t);
  if (visible_name.size() > maximum_characters)
    return false;

  try {
    std::wstring expression(pattern->Buffer, pattern->Length / sizeof(wchar_t));
    if (ignore_case) {
      for (auto &character : expression)
        character = fold_case(character);
    }

    UNICODE_STRING native_expression{};
    native_expression.Buffer = expression.data();
    native_expression.Length =
        static_cast<USHORT>(expression.size() * sizeof(wchar_t));
    native_expression.MaximumLength = native_expression.Length;
    UNICODE_STRING name{};
    name.Buffer = const_cast<PWCH>(visible_name.data());
    name.Length = static_cast<USHORT>(visible_name.size() * sizeof(wchar_t));
    name.MaximumLength = name.Length;

    BOOLEAN matched = FALSE;
    const auto guarded = ntl::seh::try_except([&] {
      matched = FsRtlIsNameInExpression(&native_expression, &name,
                                        ignore_case ? TRUE : FALSE, nullptr);
    });
    return std::get<0>(guarded) && matched != FALSE;
  } catch (...) {
    return false;
  }
}

ntl::status query_injection_entry(const instance_state &state,
                                  ntl::flt::instance instance,
                                  FILE_INFORMATION_CLASS information_class,
                                  std::vector<unsigned char> &entry) noexcept {
  directory_layout layout{};
  if (!instance || !get_directory_layout(information_class, layout))
    return STATUS_INVALID_PARAMETER;

  struct directory_owner {
    HANDLE handle = nullptr;
    PFILE_OBJECT file = nullptr;

    ~directory_owner() noexcept {
      if (handle)
        FltClose(handle);
      if (file)
        ObDereferenceObject(file);
    }
  } parent;

  try {
    constexpr auto maximum_characters =
        (std::numeric_limits<USHORT>::max)() / sizeof(wchar_t);
    if (state.real_parent.size() > maximum_characters ||
        state.real_final.size() > maximum_characters)
      return STATUS_NAME_TOO_LONG;

    UNICODE_STRING parent_name{};
    parent_name.Buffer = const_cast<PWCH>(state.real_parent.data());
    parent_name.Length =
        static_cast<USHORT>(state.real_parent.size() * sizeof(wchar_t));
    parent_name.MaximumLength = parent_name.Length;
    OBJECT_ATTRIBUTES attributes{};
    InitializeObjectAttributes(&attributes, &parent_name,
                               OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
                               nullptr, nullptr);

    IO_STATUS_BLOCK io_status{};
    IO_DRIVER_CREATE_CONTEXT create_context{};
    IoInitializeDriverCreateContext(&create_context);
    NTSTATUS status = FltCreateFileEx2(
        state.filter, instance.native_handle(), &parent.handle, &parent.file,
        FILE_LIST_DIRECTORY | FILE_TRAVERSE | FILE_READ_ATTRIBUTES, &attributes,
        &io_status, nullptr, FILE_ATTRIBUTE_NORMAL,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_OPEN,
        FILE_DIRECTORY_FILE, nullptr, 0, IO_IGNORE_SHARE_ACCESS_CHECK,
        &create_context);
    if (!NT_SUCCESS(status))
      return status;

    const std::size_t name_bytes =
        (std::max)(state.real_final.size(), state.user_final.size()) *
        sizeof(wchar_t);
    const std::size_t capacity =
        align_directory_record(layout.name_offset + name_bytes);
    if (capacity > (std::numeric_limits<ULONG>::max)())
      return STATUS_NAME_TOO_LONG;

    std::vector<unsigned char> query_buffer(capacity);
    UNICODE_STRING real_name{};
    real_name.Buffer = const_cast<PWCH>(state.real_final.data());
    real_name.Length =
        static_cast<USHORT>(state.real_final.size() * sizeof(wchar_t));
    real_name.MaximumLength = real_name.Length;
    ULONG returned = 0;
    status = FltQueryDirectoryFile(
        instance.native_handle(), parent.file, query_buffer.data(),
        static_cast<ULONG>(query_buffer.size()), information_class, TRUE,
        &real_name, TRUE, &returned);
    if (!NT_SUCCESS(status))
      return status;
    if (!record_validation::validate_linked_name_record_chain(
            query_buffer.data(), returned, validation_layout(layout)))
      return STATUS_DATA_ERROR;

    const std::size_t user_name_bytes =
        state.user_final.size() * sizeof(wchar_t);
    const std::size_t entry_bytes =
        align_directory_record(layout.name_offset + user_name_bytes);
    entry.assign(entry_bytes, 0);
    RtlCopyMemory(
        entry.data(), query_buffer.data(),
        (std::min)(layout.name_offset, static_cast<std::size_t>(returned)));
    *reinterpret_cast<ULONG *>(entry.data() + layout.next_offset) = 0;
    *reinterpret_cast<ULONG *>(entry.data() + layout.name_length_offset) =
        static_cast<ULONG>(user_name_bytes);
    if (layout.short_name_length_offset !=
        (std::numeric_limits<std::size_t>::max)()) {
      *reinterpret_cast<CCHAR *>(entry.data() +
                                 layout.short_name_length_offset) = 0;
    }
    if (user_name_bytes != 0) {
      RtlCopyMemory(entry.data() + layout.name_offset, state.user_final.data(),
                    user_name_bytes);
    }
    return STATUS_SUCCESS;
  } catch (...) {
    entry.clear();
    return STATUS_INSUFFICIENT_RESOURCES;
  }
}

ntl::flt::pre_result pre_directory(
    ntl::flt::directory_control_callback_data data,
    ntl::flt::related_objects objects,
    ntl::flt::completion_slot<directory_completion> &completion) noexcept {
  const auto parameters = data.parameters();
  if (!data.is_irp_operation())
    return ntl::flt::pre_result::success_no_callback;

  auto mapping = objects.try_get(mapping_context);
  if (!mapping)
    return ntl::flt::pre_result::success_no_callback;

  auto name = data.try_query_name(FLT_FILE_NAME_NORMALIZED |
                                  FLT_FILE_NAME_QUERY_DEFAULT);
  if (!name || name->try_parse().is_err())
    return ntl::flt::pre_result::success_no_callback;

  if (parameters.is_notify() || parameters.is_notify_ex()) {
    std::wstring_view remainder;
    if (path_prefix(name->name(), (*mapping)->user_full, true, remainder) ||
        path_prefix(name->name(), (*mapping)->real_full, true, remainder))
      notification_requests.fetch_add(1, std::memory_order_relaxed);
    return ntl::flt::pre_result::success_no_callback;
  }
  if (!parameters.is_query())
    return ntl::flt::pre_result::success_no_callback;
  switch (parameters.information_class()) {
  case FileIdExtdDirectoryInformation:
  case FileIdExtdBothDirectoryInformation:
  case FileId64ExtdDirectoryInformation:
  case FileId64ExtdBothDirectoryInformation:
    extended_directory_queries.fetch_add(1, std::memory_order_relaxed);
    break;
  default:
    break;
  }

  directory_role role = directory_role::none;
  if (equal_name(name->name(), (*mapping)->user_parent))
    role = directory_role::user_parent;
  else if (equal_name(name->name(), (*mapping)->real_parent))
    role = directory_role::real_parent;
  if (role == directory_role::none)
    return ntl::flt::pre_result::success_no_callback;

  auto state = objects.try_get_or_create(enumeration_context);
  if (!state)
    return ntl::flt::pre_result::success_no_callback;
  const bool restart = (data.operation_flags() & SL_RESTART_SCAN) != 0;
  if (restart)
    (*state)->user_entry_emitted.store(0, std::memory_order_release);

  if (role == directory_role::user_parent &&
      (restart || parameters.file_name() != nullptr)) {
    const bool ignore_case =
        !objects.file() ||
        (objects.file().native_object()->Flags & FO_OPENED_CASE_SENSITIVE) == 0;
    (*state)->user_pattern_matches.store(
        pattern_may_match(parameters.file_name(), (*mapping)->user_final,
                          ignore_case)
            ? 1
            : 0,
        std::memory_order_release);
  }

  const bool may_emit =
      role == directory_role::user_parent &&
      (*state)->user_pattern_matches.load(std::memory_order_acquire) != 0;
  const bool return_single =
      (data.operation_flags() & SL_RETURN_SINGLE_ENTRY) != 0;
  std::vector<unsigned char> injection_entry;
  if (may_emit &&
      (*state)->user_entry_emitted.load(std::memory_order_acquire) == 0) {
    const ntl::status queried =
        query_injection_entry(**mapping, objects.instance(),
                              parameters.information_class(), injection_entry);
    if (queried.is_err())
      injection_entry.clear();
  }

  auto prepared = ntl::flt::try_prepare_output_buffer(ntl::flt::as_pre(data));
  if (!prepared)
    return ntl::flt::pre_result::success_no_callback;

  const ntl::status stored =
      completion.try_emplace(std::move(*prepared), std::move(*state), role,
                             may_emit && !injection_entry.empty(),
                             return_single, std::move(injection_entry));
  return stored.is_ok() ? ntl::flt::pre_result::success_with_callback
                        : ntl::flt::pre_result::success_no_callback;
}

ntl::flt::post_continuation
post_directory(ntl::flt::directory_control_callback_data,
               ntl::flt::related_objects,
               ntl::flt::completion_ref<directory_completion>) noexcept {
  return ntl::flt::post_continuation::when_safe;
}

std::wstring_view
directory_entry_name(const unsigned char *entry, std::size_t available,
                     const directory_layout &layout) noexcept {
  record_validation::bounded_name name;
  if (!record_validation::try_read_counted_name(
          entry, available, layout.name_length_offset, layout.name_offset,
          name))
    return {};
  return {reinterpret_cast<const wchar_t *>(entry + name.offset),
          name.size_bytes / sizeof(wchar_t)};
}

bool hide_directory_entry(unsigned char *buffer, std::size_t bytes,
                          const directory_layout &layout,
                          std::wstring_view hidden_name,
                          std::size_t &new_bytes) noexcept {
  new_bytes = bytes;
  if (!record_validation::validate_linked_name_record_chain(
          buffer, bytes, validation_layout(layout)))
    return false;

  std::size_t offset = 0;
  std::size_t previous = (std::numeric_limits<std::size_t>::max)();

  while (offset < bytes && bytes - offset >= layout.name_offset) {
    auto *entry = buffer + offset;
    const ULONG next = record_validation::load_u32(entry + layout.next_offset);
    const std::size_t record_bytes =
        next != 0 ? static_cast<std::size_t>(next) : bytes - offset;
    if (record_bytes > bytes - offset || record_bytes < layout.name_offset)
      break;

    if (!equal_name(directory_entry_name(entry, record_bytes, layout),
                    hidden_name)) {
      previous = offset;
      if (next == 0)
        break;
      offset += next;
      continue;
    }

    const std::size_t tail = bytes - offset - record_bytes;
    if (tail != 0)
      RtlMoveMemory(entry, entry + record_bytes, tail);
    bytes -= record_bytes;

    if (previous != (std::numeric_limits<std::size_t>::max)()) {
      auto *previous_entry = buffer + previous;
      record_validation::store_u32(
          previous_entry + layout.next_offset,
          offset < bytes ? static_cast<ULONG>(offset - previous) : 0);
    }
    if (tail == 0)
      break;
  }
  new_bytes = bytes;
  return true;
}

bool append_directory_entry(unsigned char *buffer, std::size_t used,
                            std::size_t capacity,
                            const directory_layout &layout,
                            const std::vector<unsigned char> &injection_entry,
                            std::size_t &new_used) noexcept {
  const std::size_t record_bytes = injection_entry.size();
  if (record_bytes < layout.name_offset ||
      !record_validation::validate_linked_name_record_chain(
          injection_entry.data(), record_bytes, validation_layout(layout)) ||
      record_validation::load_u32(injection_entry.data() +
                                  layout.next_offset) != 0)
    return false;
  const std::size_t append_offset =
      used == 0 ? 0 : align_directory_record(used);
  if (append_offset > capacity || record_bytes > capacity - append_offset)
    return false;

  std::size_t previous = (std::numeric_limits<std::size_t>::max)();
  if (used != 0) {
    if (!record_validation::validate_linked_name_record_chain(
            buffer, used, validation_layout(layout)))
      return false;
    std::size_t offset = 0;
    while (offset < used && used - offset >= layout.name_offset) {
      const ULONG next = record_validation::load_u32(
          buffer + offset + layout.next_offset);
      previous = offset;
      if (next == 0)
        break;
      if (next > used - offset)
        return false;
      offset += next;
    }
    if (previous == (std::numeric_limits<std::size_t>::max)())
      return false;
  }

  auto *entry = buffer + append_offset;
  RtlCopyMemory(entry, injection_entry.data(), record_bytes);
  record_validation::store_u32(entry + layout.next_offset, 0);

  if (previous != (std::numeric_limits<std::size_t>::max)())
    record_validation::store_u32(buffer + previous + layout.next_offset,
                                 static_cast<ULONG>(append_offset - previous));
  new_used = append_offset + record_bytes;
  return true;
}

bool make_synthetic_directory_entry(
    const directory_layout &layout, std::wstring_view name,
    std::vector<unsigned char> &entry) noexcept {
  try {
    const std::size_t name_bytes = name.size() * sizeof(wchar_t);
    const std::size_t record_bytes =
        align_directory_record(layout.name_offset + name_bytes);
    entry.assign(record_bytes, 0xa5);
    record_validation::store_u32(entry.data() + layout.next_offset, 0);
    record_validation::store_u32(
        entry.data() + layout.name_length_offset,
        static_cast<ULONG>(name_bytes));
    if (layout.short_name_length_offset !=
        (std::numeric_limits<std::size_t>::max)()) {
      *reinterpret_cast<CCHAR *>(
          entry.data() + layout.short_name_length_offset) = 0;
    }
    if (name_bytes != 0) {
      RtlCopyMemory(entry.data() + layout.name_offset, name.data(), name_bytes);
    }
    return record_validation::validate_linked_name_record_chain(
        entry.data(), entry.size(), validation_layout(layout));
  } catch (...) {
    entry.clear();
    return false;
  }
}

bool test_synthetic_directory_layout(
    FILE_INFORMATION_CLASS information_class) noexcept {
  directory_layout layout{};
  if (!get_directory_layout(information_class, layout))
    return false;

  try {
    constexpr std::wstring_view existing_name = L"existing-entry";
    constexpr std::wstring_view injected_name = L"visible-entry";
    std::vector<unsigned char> existing;
    std::vector<unsigned char> injected;
    if (!make_synthetic_directory_entry(layout, existing_name, existing) ||
        !make_synthetic_directory_entry(layout, injected_name, injected))
      return false;

    const std::size_t append_offset = align_directory_record(existing.size());
    std::vector<unsigned char> buffer(append_offset + injected.size() + 32,
                                      0xcc);
    RtlCopyMemory(buffer.data(), existing.data(), existing.size());

    std::size_t used = existing.size();
    if (!append_directory_entry(buffer.data(), used, buffer.size(), layout,
                                injected, used) ||
        used != append_offset + injected.size() ||
        record_validation::load_u32(buffer.data() + layout.next_offset) !=
            append_offset ||
        directory_entry_name(buffer.data(), append_offset, layout) !=
            existing_name ||
        directory_entry_name(buffer.data() + append_offset, injected.size(),
                             layout) != injected_name ||
        RtlCompareMemory(buffer.data() + append_offset, injected.data(),
                         injected.size()) != injected.size()) {
      return false;
    }

    std::size_t filtered = used;
    if (!hide_directory_entry(buffer.data(), used, layout, existing_name,
                              filtered) ||
        filtered != injected.size() ||
        directory_entry_name(buffer.data(), filtered, layout) !=
            injected_name ||
        !record_validation::validate_linked_name_record_chain(
            buffer.data(), filtered, validation_layout(layout))) {
      return false;
    }

    used = filtered;
    return hide_directory_entry(buffer.data(), used, layout, injected_name,
                                filtered) &&
           filtered == 0;
  } catch (...) {
    return false;
  }
}

std::uint64_t test_synthetic_file_id_64_layouts() noexcept {
  std::uint64_t passed = 0;
  if (test_synthetic_directory_layout(FileId64ExtdDirectoryInformation))
    ++passed;
  if (test_synthetic_directory_layout(FileId64ExtdBothDirectoryInformation))
    ++passed;
  return passed;
}

void safe_post_directory(
    ntl::flt::safe_directory_control_operation operation,
    ntl::flt::related_objects objects,
    ntl::flt::completion_ref<directory_completion> completion) noexcept {
  if (!completion)
    return;

  auto data = operation.data();
  directory_layout layout{};
  if (!get_directory_layout(data.parameters().information_class(), layout))
    return;

  auto mapping = objects.try_get(mapping_context);
  if (!mapping)
    return;

  const ntl::status accessed = completion->prepared_output.try_visit(
      operation, [&](ntl::flt::prepared_output_buffer_view view) noexcept {
        auto *const buffer = static_cast<unsigned char *>(view.data());
        const std::size_t capacity = view.capacity();
        std::size_t used = view.valid_size();

        if (completion->role == directory_role::real_parent) {
          ntl::status query_status = data.io_status();
          for (;;) {
            if (used != 0) {
              std::size_t filtered = used;
              if (!hide_directory_entry(buffer, used, layout,
                                        (*mapping)->real_final, filtered)) {
                data.set_io_status(STATUS_DATA_ERROR, 0);
                return;
              }
              used = filtered;
              if (used != 0) {
                data.set_io_status(query_status, used);
                return;
              }
            }

            if (KeGetCurrentIrql() != PASSIVE_LEVEL || !objects.file()) {
              data.set_io_status(STATUS_NO_MORE_FILES, 0);
              return;
            }

            ULONG returned = 0;
            const NTSTATUS queried = FltQueryDirectoryFile(
                objects.instance().native_handle(),
                objects.file().native_object(), buffer,
                static_cast<ULONG>(capacity),
                data.parameters().information_class(),
                completion->return_single_entry ? TRUE : FALSE, nullptr, FALSE,
                &returned);
            query_status = ntl::status{queried};
            if ((!NT_SUCCESS(queried) && queried != STATUS_BUFFER_OVERFLOW) ||
                returned == 0) {
              data.set_io_status(query_status, returned);
              return;
            }
            used = (std::min)(static_cast<std::size_t>(returned), capacity);
          }
        }

        if (!completion->may_emit_user_entry ||
            (completion->return_single_entry && used != 0) ||
            completion->state->user_entry_emitted.exchange(
                1, std::memory_order_acq_rel) != 0)
          return;

        std::size_t new_used = used;
        if (!append_directory_entry(buffer, used, capacity, layout,
                                    completion->injection_entry, new_used)) {
          completion->state->user_entry_emitted.store(
              0, std::memory_order_release);
          return;
        }
        data.set_io_status(STATUS_SUCCESS, new_used);
      });
  if (accessed.is_err())
    data.set_io_status(accessed, 0);
}

} // namespace

ntl::status ntl::flt::main(ntl::flt::driver &driver,
                           std::wstring_view registry_path) {
  const std::uint64_t synthetic_layouts =
      test_synthetic_file_id_64_layouts();
  synthetic_file_id_64_layouts.store(synthetic_layouts,
                                     std::memory_order_relaxed);
  if (synthetic_layouts != 2)
    return STATUS_DATA_ERROR;

  const ntl::status configuration = initialize_configuration(registry_path);
  if (configuration.is_err())
    return configuration;

  ntl::flt::communication_server messages;
  messages.contract(1)
      .on(query_generated_name_count,
          []() noexcept {
            return generated_visible_names.load(std::memory_order_relaxed);
          })
      .on(query_observations, []() noexcept {
        observations result;
        result.generated_names =
            generated_visible_names.load(std::memory_order_relaxed);
        result.query_name_rewrites =
            query_name_rewrites.load(std::memory_order_relaxed);
        result.rename_reissues =
            rename_reissues.load(std::memory_order_relaxed);
        result.hard_link_reissues =
            hard_link_reissues.load(std::memory_order_relaxed);
        result.notification_requests =
            notification_requests.load(std::memory_order_relaxed);
        result.usn_rewrites = usn_rewrites.load(std::memory_order_relaxed);
        result.extended_directory_queries =
            extended_directory_queries.load(std::memory_order_relaxed);
        result.network_query_retries =
            network_query_retries.load(std::memory_order_relaxed);
        result.synthetic_file_id_64_layouts =
            synthetic_file_id_64_layouts.load(std::memory_order_relaxed);
        result.hard_link_query_rewrites =
            hard_link_query_rewrites.load(std::memory_order_relaxed);
        result.enum_usn_rewrites =
            enum_usn_rewrites.load(std::memory_order_relaxed);
        result.read_journal_rewrites =
            read_journal_rewrites.load(std::memory_order_relaxed);
        result.lookup_cluster_rewrites =
            lookup_cluster_rewrites.load(std::memory_order_relaxed);
        result.find_by_sid_rewrites =
            find_by_sid_rewrites.load(std::memory_order_relaxed);
        return result;
      });
  const ntl::status port_status =
      driver.add_communication_port(port_name, std::move(messages));
  if (port_status.is_err())
    return port_status;

  ntl::flt::registration callbacks;
  callbacks
      .on_instance_setup([](ntl::flt::related_objects objects,
                            FLT_INSTANCE_SETUP_FLAGS, DEVICE_TYPE,
                            FLT_FILESYSTEM_TYPE filesystem_type) noexcept {
        return attach_mapping(objects, filesystem_type);
      })
      .on_with_completion<create_completion>(ntl::flt::operation::create,
                                             &pre_create, &post_create)
      .on(ntl::flt::operation::network_query_open, &pre_network_query_open)
      .on_with_completion<query_information_completion>(
          ntl::flt::operation::query_information, &pre_query_information,
          &post_query_information, &safe_post_query_information)
      .on(ntl::flt::operation::set_information, &pre_set_information)
      .on_with_completion<fsctl_completion>(
          ntl::flt::operation::file_system_control, &pre_file_system_control,
          &post_file_system_control, &safe_post_file_system_control)
      .on_with_completion<directory_completion>(
          ntl::flt::operation::directory_control, &pre_directory,
          &post_directory, &safe_post_directory)
      .on_generate_file_name(&generate_file_name)
      .on_normalize_name_component(&normalize_name_component)
#if FLT_MGR_LONGHORN
      .on_normalize_name_component_ex(&normalize_name_component)
#endif
      .on_unload([](ntl::flt::unload_flags) noexcept {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
                   "[crtsys FLT NameChanger runtime] unload\n");
        return ntl::status::ok();
      })
      .context(mapping_context)
      .context(enumeration_context);

  return driver.start(std::move(callbacks));
}
