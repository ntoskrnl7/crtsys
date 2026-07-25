#include "namespace_tests.hpp"

#include "../name_changer_shared/name_changer_runtime.hpp"
#include "../name_changer_shared/output_record_validation.hpp"

#include <Aclapi.h>
#include <Windows.h>
#include <winioctl.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace crtsys_flt_name_changer_runtime_test {
namespace {

namespace fs = std::filesystem;

struct native_io_status_block {
  union {
    LONG status;
    void *pointer;
  };
  ULONG_PTR information;
};

using nt_query_information_file_fn = LONG(NTAPI *)(HANDLE,
                                                   native_io_status_block *,
                                                   void *, ULONG, ULONG);

struct file_name_information_record {
  ULONG file_name_length;
  WCHAR file_name[1];
};

struct file_basic_information_record {
  LARGE_INTEGER creation_time;
  LARGE_INTEGER last_access_time;
  LARGE_INTEGER last_write_time;
  LARGE_INTEGER change_time;
  ULONG file_attributes;
};

struct file_standard_information_record {
  LARGE_INTEGER allocation_size;
  LARGE_INTEGER end_of_file;
  ULONG number_of_links;
  BOOLEAN delete_pending;
  BOOLEAN directory;
};

struct file_all_information_record {
  file_basic_information_record basic_information;
  file_standard_information_record standard_information;
  LARGE_INTEGER internal_information;
  ULONG ea_size;
  ACCESS_MASK access_flags;
  LARGE_INTEGER current_byte_offset;
  ULONG mode;
  ULONG alignment_requirement;
  file_name_information_record name_information;
};

struct file_link_entry_information_record {
  ULONG next_entry_offset;
  LONGLONG parent_file_id;
  ULONG file_name_length;
  WCHAR file_name[1];
};

struct file_links_information_record {
  ULONG bytes_needed;
  ULONG entries_returned;
  file_link_entry_information_record entry;
};

struct usn_common_header {
  DWORD record_length;
  WORD major_version;
  WORD minor_version;
};

struct usn_v2_prefix {
  DWORD record_length;
  WORD major_version;
  WORD minor_version;
  DWORDLONG file_reference_number;
  DWORDLONG parent_file_reference_number;
  LONGLONG usn;
  LARGE_INTEGER time_stamp;
  DWORD reason;
  DWORD source_info;
  DWORD security_id;
  DWORD file_attributes;
  WORD file_name_length;
  WORD file_name_offset;
};

struct usn_v3_prefix {
  DWORD record_length;
  WORD major_version;
  WORD minor_version;
  FILE_ID_128 file_reference_number;
  FILE_ID_128 parent_file_reference_number;
  LONGLONG usn;
  LARGE_INTEGER time_stamp;
  DWORD reason;
  DWORD source_info;
  DWORD security_id;
  DWORD file_attributes;
  WORD file_name_length;
  WORD file_name_offset;
};

struct file_id_information_record {
  ULONGLONG volume_serial_number;
  FILE_ID_128 file_id;
};

struct stable_file_id_record {
  LONGLONG legacy = 0;
  FILE_ID_128 extended{};
};

bool write_small_file(const fs::path &path, std::string &failure);

class unique_handle {
public:
  unique_handle() noexcept = default;
  explicit unique_handle(HANDLE value) noexcept : value_(value) {}
  unique_handle(const unique_handle &) = delete;
  unique_handle &operator=(const unique_handle &) = delete;
  unique_handle(unique_handle &&other) noexcept
      : value_(std::exchange(other.value_, INVALID_HANDLE_VALUE)) {}
  unique_handle &operator=(unique_handle &&other) noexcept {
    if (this != &other) {
      if (valid())
        CloseHandle(value_);
      value_ = std::exchange(other.value_, INVALID_HANDLE_VALUE);
    }
    return *this;
  }
  ~unique_handle() {
    if (valid())
      CloseHandle(value_);
  }

  bool valid() const noexcept {
    return value_ != nullptr && value_ != INVALID_HANDLE_VALUE;
  }
  HANDLE get() const noexcept { return value_; }

private:
  HANDLE value_ = INVALID_HANDLE_VALUE;
};

nt_query_information_file_fn nt_query_information_file() noexcept {
  static const auto function = reinterpret_cast<nt_query_information_file_fn>(
      GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationFile"));
  return function;
}

bool equal_name(std::wstring_view left, std::wstring_view right) {
  return _wcsicmp(std::wstring(left).c_str(), std::wstring(right).c_str()) == 0;
}

bool contains_case_insensitive(std::wstring_view value,
                               std::wstring_view needle) {
  std::wstring value_copy(value);
  std::wstring needle_copy(needle);
  std::transform(value_copy.begin(), value_copy.end(), value_copy.begin(),
                 towupper);
  std::transform(needle_copy.begin(), needle_copy.end(), needle_copy.begin(),
                 towupper);
  return value_copy.find(needle_copy) != std::wstring::npos;
}

bool contains_mapping_path(std::wstring_view value,
                           std::wstring_view mapping) {
  return contains_case_insensitive(value, mapping) ||
         (!mapping.empty() && mapping.front() == L'\\' &&
          contains_case_insensitive(value, mapping.substr(1)));
}

bool unsupported_control_error(DWORD error) noexcept {
  return error == ERROR_INVALID_FUNCTION || error == ERROR_NOT_SUPPORTED ||
         error == ERROR_INVALID_PARAMETER;
}

std::wstring volume_device_path(const fs::path &path) {
  return L"\\\\.\\" + path.root_name().wstring();
}

bool query_name(HANDLE file, ULONG information_class, std::wstring &name,
                LONG &status, std::string &failure) {
  const auto query = nt_query_information_file();
  if (!query) {
    failure = "NtQueryInformationFile is unavailable";
    return false;
  }

  std::array<unsigned char, 65536> buffer{};
  native_io_status_block io_status{};
  status = query(file, &io_status, buffer.data(),
                 static_cast<ULONG>(buffer.size()), information_class);
  if (status < 0)
    return true;

  std::size_t name_information_offset = 0;
  if (information_class == 18)
    name_information_offset =
        offsetof(file_all_information_record, name_information);
  if (name_information_offset > buffer.size() ||
      sizeof(ULONG) > buffer.size() - name_information_offset) {
    failure = "name information offset exceeded the query buffer";
    return false;
  }

  const auto *const information =
      reinterpret_cast<const file_name_information_record *>(
          buffer.data() + name_information_offset);
  const std::size_t name_offset =
      name_information_offset +
      offsetof(file_name_information_record, file_name);
  if ((information->file_name_length % sizeof(wchar_t)) != 0 ||
      name_offset > buffer.size() ||
      information->file_name_length > buffer.size() - name_offset) {
    failure = "NtQueryInformationFile returned an invalid counted name";
    return false;
  }
  name.assign(reinterpret_cast<const wchar_t *>(buffer.data() + name_offset),
              information->file_name_length / sizeof(wchar_t));
  return true;
}

bool require_visible_query_name(HANDLE file, ULONG information_class,
                                const char *label, std::string &failure) {
  std::wstring name;
  LONG status = 0;
  if (!query_name(file, information_class, name, status, failure))
    return false;
  if (status < 0) {
    failure = std::string(label) + " failed with NTSTATUS " +
              std::to_string(static_cast<unsigned long>(status));
    return false;
  }
  if (!contains_case_insensitive(name, user_mapping) ||
      contains_case_insensitive(name, real_mapping)) {
    failure = std::string(label) +
              " exposed a backing name instead of the visible graft";
    return false;
  }
  return true;
}

bool verify_query_information(const fs::path &visible_mapping,
                              const fs::path &visible_file,
                              std::string &failure) {
  unique_handle file(
      CreateFileW(visible_file.c_str(), FILE_READ_ATTRIBUTES | GENERIC_READ,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
  if (!file.valid()) {
    failure = "failed to open the visible file for name queries: " +
              std::to_string(GetLastError());
    return false;
  }

  if (!require_visible_query_name(file.get(), 9, "FileNameInformation",
                                  failure) ||
      !require_visible_query_name(file.get(), 48,
                                  "FileNormalizedNameInformation", failure) ||
      !require_visible_query_name(file.get(), 18, "FileAllInformation",
                                  failure))
    return false;

  unique_handle mapping(
      CreateFileW(visible_mapping.c_str(), FILE_READ_ATTRIBUTES,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
  if (!mapping.valid()) {
    failure = "failed to open the visible mapping for alternate-name query";
    return false;
  }

  std::wstring alternate;
  LONG status = 0;
  if (!query_name(mapping.get(), 21, alternate, status, failure))
    return false;
  if (status >= 0 && !equal_name(alternate, user_mapping_final_component)) {
    failure = "FileAlternateNameInformation exposed the backing component";
    return false;
  }
  return true;
}

bool query_internal_file_id(HANDLE file, LONGLONG &file_id,
                            std::string &failure) {
  const auto query = nt_query_information_file();
  if (!query) {
    failure = "NtQueryInformationFile is unavailable";
    return false;
  }
  LARGE_INTEGER information{};
  native_io_status_block io_status{};
  const LONG status =
      query(file, &io_status, &information, sizeof(information), 6);
  if (status < 0) {
    failure = "FileInternalInformation failed with NTSTATUS " +
              std::to_string(static_cast<unsigned long>(status));
    return false;
  }
  file_id = information.QuadPart;
  return true;
}

bool query_stable_file_id(HANDLE file, stable_file_id_record &file_id,
                          std::string &failure) {
  if (!query_internal_file_id(file, file_id.legacy, failure))
    return false;
  std::memset(&file_id.extended, 0, sizeof(file_id.extended));
  std::memcpy(&file_id.extended, &file_id.legacy, sizeof(file_id.legacy));

  const auto query = nt_query_information_file();
  file_id_information_record information{};
  native_io_status_block io_status{};
  const LONG status =
      query(file, &io_status, &information, sizeof(information), 59);
  if (status >= 0)
    file_id.extended = information.file_id;
  return true;
}

bool equal_file_id(const FILE_ID_128 &left,
                   const FILE_ID_128 &right) noexcept {
  return std::memcmp(&left, &right, sizeof(left)) == 0;
}

bool verify_hard_link_information(const fs::path &visible_mapping,
                                  std::string &failure) {
  unique_handle mapping(
      CreateFileW(visible_mapping.c_str(), FILE_READ_ATTRIBUTES,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
  unique_handle visible_parent(
      CreateFileW(visible_mapping.parent_path().c_str(), FILE_READ_ATTRIBUTES,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
  if (!mapping.valid() || !visible_parent.valid()) {
    failure = "failed to open the visible mapping hierarchy for hard-link "
              "information";
    return false;
  }

  LONGLONG visible_parent_id = 0;
  if (!query_internal_file_id(visible_parent.get(), visible_parent_id, failure))
    return false;

  const auto query = nt_query_information_file();
  if (!query) {
    failure = "NtQueryInformationFile is unavailable";
    return false;
  }
  std::array<unsigned char, 65536> buffer{};
  native_io_status_block io_status{};
  const LONG status =
      query(mapping.get(), &io_status, buffer.data(),
            static_cast<ULONG>(buffer.size()), 46);
  if (status < 0) {
    failure = "FileHardLinkInformation failed with NTSTATUS " +
              std::to_string(static_cast<unsigned long>(status));
    return false;
  }

  constexpr std::size_t header_bytes =
      offsetof(file_links_information_record, entry);
  constexpr std::size_t name_offset =
      offsetof(file_link_entry_information_record, file_name);
  const std::size_t returned =
      static_cast<std::size_t>(io_status.information);
  if (returned < header_bytes) {
    failure = "FileHardLinkInformation returned a truncated header";
    return false;
  }

  const auto *const information =
      reinterpret_cast<const file_links_information_record *>(buffer.data());
  std::size_t offset = header_bytes;
  bool found_visible_mapping = false;
  for (ULONG index = 0; index != information->entries_returned; ++index) {
    if (offset > returned || name_offset > returned - offset) {
      failure = "FileHardLinkInformation returned a truncated entry";
      return false;
    }
    const auto *const entry =
        reinterpret_cast<const file_link_entry_information_record *>(
            buffer.data() + offset);
    const std::size_t remaining = returned - offset;
    const std::size_t record_bytes =
        entry->next_entry_offset != 0 ? entry->next_entry_offset : remaining;
    if (record_bytes < name_offset || record_bytes > remaining ||
        entry->file_name_length >
            (record_bytes - name_offset) / sizeof(wchar_t) ||
        (index + 1 < information->entries_returned) !=
            (entry->next_entry_offset != 0)) {
      failure = "FileHardLinkInformation returned an invalid entry chain";
      return false;
    }

    const std::wstring_view name{entry->file_name, entry->file_name_length};
    if (equal_name(name, real_mapping_final_component)) {
      failure = "FileHardLinkInformation exposed the backing mapping";
      return false;
    }
    if (entry->parent_file_id == visible_parent_id &&
        equal_name(name, user_mapping_final_component)) {
      found_visible_mapping = true;
    }
    if (entry->next_entry_offset == 0) {
      offset = returned;
    } else {
      offset += entry->next_entry_offset;
    }
  }
  if (!found_visible_mapping) {
    failure = "FileHardLinkInformation omitted the visible graft parent/name";
    return false;
  }
  return true;
}

bool find_visible_mapping_usn_record(
    const unsigned char *buffer, std::size_t returned,
    std::size_t records_offset, const stable_file_id_record &visible_parent_id,
    bool &found, std::string &failure) {
  if (!buffer || records_offset > returned) {
    failure = "USN result omitted its continuation prefix";
    return false;
  }

  std::size_t offset = records_offset;
  while (offset < returned) {
    if (sizeof(usn_common_header) > returned - offset) {
      failure = "USN result ended with a truncated record header";
      return false;
    }
    const auto *const header =
        reinterpret_cast<const usn_common_header *>(buffer + offset);
    record_validation::usn_name_record_layout layout{};
    bool parent_matches = false;
    if (header->major_version == 2) {
      layout = {2, offsetof(usn_v2_prefix, file_name_offset) + sizeof(WORD),
                offsetof(usn_v2_prefix, file_name_length),
                offsetof(usn_v2_prefix, file_name_offset)};
      if (header->record_length <= returned - offset) {
        const auto *const record =
            reinterpret_cast<const usn_v2_prefix *>(header);
        parent_matches =
            static_cast<LONGLONG>(record->parent_file_reference_number) ==
            visible_parent_id.legacy;
      }
    } else if (header->major_version == 3) {
      layout = {3, offsetof(usn_v3_prefix, file_name_offset) + sizeof(WORD),
                offsetof(usn_v3_prefix, file_name_length),
                offsetof(usn_v3_prefix, file_name_offset)};
      if (header->record_length <= returned - offset) {
        const auto *const record =
            reinterpret_cast<const usn_v3_prefix *>(header);
        parent_matches = equal_file_id(
            record->parent_file_reference_number, visible_parent_id.extended);
      }
    } else {
      failure = "USN result returned an unsupported record version";
      return false;
    }

    record_validation::bounded_name name_bounds;
    if (!record_validation::try_read_usn_name(
            buffer + offset, returned - offset, layout, name_bounds) ||
        header->record_length == 0 || (header->record_length % 8) != 0) {
      failure = "USN result returned an invalid record";
      return false;
    }
    const std::wstring_view name{
        reinterpret_cast<const wchar_t *>(buffer + offset +
                                          name_bounds.offset),
        name_bounds.size_bytes / sizeof(wchar_t)};
    if (parent_matches &&
        equal_name(name, user_mapping_final_component))
      found = true;
    offset += header->record_length;
  }
  return offset == returned;
}

bool open_volume_for_fsctl(const fs::path &path, unique_handle &volume,
                           std::string &failure) {
  const std::wstring device = volume_device_path(path);
  volume = unique_handle(CreateFileW(
      device.c_str(), GENERIC_READ | GENERIC_WRITE,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
  if (volume.valid())
    return true;
  failure = "failed to open the volume for namespace FSCTLs: " +
            std::to_string(GetLastError());
  return false;
}

bool verify_enum_usn_data(const fs::path &visible_mapping, bool &supported,
                          std::string &failure) {
  supported = false;
  unique_handle volume;
  if (!open_volume_for_fsctl(visible_mapping, volume, failure))
    return false;

  unique_handle visible_parent(
      CreateFileW(visible_mapping.parent_path().c_str(), FILE_READ_ATTRIBUTES,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
  stable_file_id_record visible_parent_id;
  if (!visible_parent.valid() ||
      !query_stable_file_id(visible_parent.get(), visible_parent_id, failure))
    return false;

  MFT_ENUM_DATA_V1 input{};
  input.LowUsn = 0;
  input.HighUsn = (std::numeric_limits<LONGLONG>::max)();
  input.MinMajorVersion = 2;
  input.MaxMajorVersion = 3;
  std::array<unsigned char, 65536> output{};
  for (unsigned iteration = 0; iteration != 4096; ++iteration) {
    DWORD returned = 0;
    if (!DeviceIoControl(volume.get(), FSCTL_ENUM_USN_DATA, &input,
                         sizeof(input), output.data(),
                         static_cast<DWORD>(output.size()), &returned,
                         nullptr)) {
      const DWORD error = GetLastError();
      if (iteration == 0 && unsupported_control_error(error)) {
        std::cout << "fsctl_enum_usn_data=UNSUPPORTED error=" << error
                  << '\n';
        return true;
      }
      if (error == ERROR_HANDLE_EOF)
        break;
      failure = "FSCTL_ENUM_USN_DATA failed: " + std::to_string(error);
      return false;
    }
    supported = true;
    if (returned < sizeof(DWORDLONG)) {
      failure = "FSCTL_ENUM_USN_DATA returned a truncated continuation";
      return false;
    }
    bool found = false;
    if (!find_visible_mapping_usn_record(
            output.data(), returned, sizeof(DWORDLONG), visible_parent_id,
            found, failure))
      return false;
    if (found) {
      std::cout << "fsctl_enum_usn_data=PASS\n";
      return true;
    }

    const auto next =
        *reinterpret_cast<const DWORDLONG *>(output.data());
    if (next <= input.StartFileReferenceNumber)
      break;
    input.StartFileReferenceNumber = next;
  }
  failure = "FSCTL_ENUM_USN_DATA omitted the visible graft record";
  return false;
}

bool query_or_create_usn_journal(HANDLE volume, USN_JOURNAL_DATA_V0 &journal,
                                 bool &supported, std::string &failure) {
  DWORD returned = 0;
  if (DeviceIoControl(volume, FSCTL_QUERY_USN_JOURNAL, nullptr, 0, &journal,
                      sizeof(journal), &returned, nullptr)) {
    supported = true;
    return true;
  }
  DWORD error = GetLastError();
  if (error == ERROR_JOURNAL_NOT_ACTIVE) {
    CREATE_USN_JOURNAL_DATA create{};
    create.MaximumSize = 32ull * 1024ull * 1024ull;
    create.AllocationDelta = 8ull * 1024ull * 1024ull;
    if (!DeviceIoControl(volume, FSCTL_CREATE_USN_JOURNAL, &create,
                         sizeof(create), nullptr, 0, &returned, nullptr)) {
      error = GetLastError();
    } else if (DeviceIoControl(volume, FSCTL_QUERY_USN_JOURNAL, nullptr, 0,
                               &journal, sizeof(journal), &returned,
                               nullptr)) {
      supported = true;
      return true;
    } else {
      error = GetLastError();
    }
  }
  if (unsupported_control_error(error)) {
    supported = false;
    std::cout << "fsctl_read_usn_journal=UNSUPPORTED error=" << error << '\n';
    return true;
  }
  failure = "failed to query/create the USN journal: " +
            std::to_string(error);
  return false;
}

bool touch_mapping_directory(const fs::path &visible_mapping,
                             std::string &failure) {
  unique_handle mapping(
      CreateFileW(visible_mapping.c_str(), FILE_WRITE_ATTRIBUTES,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
  FILETIME now{};
  GetSystemTimeAsFileTime(&now);
  if (mapping.valid() &&
      SetFileTime(mapping.get(), nullptr, nullptr, &now))
    return true;
  failure = "failed to update the mapping directory for the USN journal: " +
            std::to_string(GetLastError());
  return false;
}

bool verify_read_usn_journal(const fs::path &visible_mapping, bool &supported,
                             std::string &failure) {
  supported = false;
  unique_handle volume;
  if (!open_volume_for_fsctl(visible_mapping, volume, failure))
    return false;

  USN_JOURNAL_DATA_V0 journal{};
  if (!query_or_create_usn_journal(volume.get(), journal, supported, failure) ||
      !supported)
    return failure.empty();
  const USN start_usn = journal.NextUsn;
  if (!touch_mapping_directory(visible_mapping, failure))
    return false;

  unique_handle visible_parent(
      CreateFileW(visible_mapping.parent_path().c_str(), FILE_READ_ATTRIBUTES,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
  stable_file_id_record visible_parent_id;
  if (!visible_parent.valid() ||
      !query_stable_file_id(visible_parent.get(), visible_parent_id, failure))
    return false;

  READ_USN_JOURNAL_DATA_V1 input{};
  input.StartUsn = start_usn;
  input.ReasonMask = 0xffffffffu;
  input.ReturnOnlyOnClose = FALSE;
  input.UsnJournalID = journal.UsnJournalID;
  input.MinMajorVersion = 2;
  input.MaxMajorVersion = 3;
  std::array<unsigned char, 65536> output{};
  for (unsigned iteration = 0; iteration != 128; ++iteration) {
    DWORD returned = 0;
    if (!DeviceIoControl(volume.get(), FSCTL_READ_USN_JOURNAL, &input,
                         sizeof(input), output.data(),
                         static_cast<DWORD>(output.size()), &returned,
                         nullptr)) {
      const DWORD error = GetLastError();
      if (unsupported_control_error(error)) {
        supported = false;
        std::cout << "fsctl_read_usn_journal=UNSUPPORTED error=" << error
                  << '\n';
        return true;
      }
      failure = "FSCTL_READ_USN_JOURNAL failed: " + std::to_string(error);
      return false;
    }
    if (returned < sizeof(USN)) {
      failure = "FSCTL_READ_USN_JOURNAL returned a truncated continuation";
      return false;
    }
    bool found = false;
    if (!find_visible_mapping_usn_record(
            output.data(), returned, sizeof(USN), visible_parent_id, found,
            failure))
      return false;
    if (found) {
      std::cout << "fsctl_read_usn_journal=PASS\n";
      return true;
    }
    const USN next = *reinterpret_cast<const USN *>(output.data());
    if (next <= input.StartUsn)
      break;
    input.StartUsn = next;
  }
  failure = "FSCTL_READ_USN_JOURNAL omitted the visible graft record";
  return false;
}

bool enable_privilege(const wchar_t *name, std::string &failure) {
  HANDLE raw_token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(),
                        TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &raw_token)) {
    failure = "OpenProcessToken failed: " + std::to_string(GetLastError());
    return false;
  }
  unique_handle token(raw_token);
  TOKEN_PRIVILEGES privileges{};
  privileges.PrivilegeCount = 1;
  if (!LookupPrivilegeValueW(nullptr, name,
                             &privileges.Privileges[0].Luid)) {
    failure = "LookupPrivilegeValueW failed: " +
              std::to_string(GetLastError());
    return false;
  }
  privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
  SetLastError(ERROR_SUCCESS);
  if (!AdjustTokenPrivileges(token.get(), FALSE, &privileges, 0, nullptr,
                             nullptr) ||
      GetLastError() == ERROR_NOT_ALL_ASSIGNED) {
    failure = "AdjustTokenPrivileges failed: " +
              std::to_string(GetLastError());
    return false;
  }
  return true;
}

bool verify_lookup_stream_from_cluster(const fs::path &visible_mapping,
  bool &supported,
                                       std::string &failure) {
  supported = false;
  if (!enable_privilege(L"SeManageVolumePrivilege", failure))
    return false;

  const fs::path probe = visible_mapping / L"cluster-lookup-probe.bin";
  LARGE_INTEGER cluster{};
  {
    unique_handle file(
        CreateFileW(probe.c_str(), GENERIC_READ | GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file.valid()) {
      failure = "failed to create the cluster lookup probe";
      return false;
    }
    std::vector<unsigned char> contents(1024 * 1024, 0x5a);
    DWORD written = 0;
    if (!WriteFile(file.get(), contents.data(),
                   static_cast<DWORD>(contents.size()), &written, nullptr) ||
        written != static_cast<DWORD>(contents.size()) ||
        !FlushFileBuffers(file.get())) {
      failure = "failed to allocate the cluster lookup probe";
      return false;
    }

    STARTING_VCN_INPUT_BUFFER input{};
    std::array<unsigned char, 4096> retrieval_storage{};
    DWORD returned = 0;
    const BOOL queried = DeviceIoControl(
        file.get(), FSCTL_GET_RETRIEVAL_POINTERS, &input, sizeof(input),
        retrieval_storage.data(), static_cast<DWORD>(retrieval_storage.size()),
        &returned, nullptr);
    const DWORD error = queried ? ERROR_SUCCESS : GetLastError();
    if (!queried && error != ERROR_MORE_DATA) {
      failure = "FSCTL_GET_RETRIEVAL_POINTERS failed: " +
                std::to_string(error);
      return false;
    }
    if (returned < offsetof(RETRIEVAL_POINTERS_BUFFER, Extents) +
                       sizeof(((PRETRIEVAL_POINTERS_BUFFER)nullptr)
                                  ->Extents[0])) {
      failure = "FSCTL_GET_RETRIEVAL_POINTERS returned no extent";
      return false;
    }
    const auto *const retrieval =
        reinterpret_cast<const RETRIEVAL_POINTERS_BUFFER *>(
            retrieval_storage.data());
    cluster = retrieval->Extents[0].Lcn;
  }

  unique_handle volume;
  if (!open_volume_for_fsctl(visible_mapping, volume, failure)) {
    (void)DeleteFileW(probe.c_str());
    return false;
  }
  LOOKUP_STREAM_FROM_CLUSTER_INPUT input{};
  input.NumberOfClusters = 1;
  input.Cluster[0] = cluster;
  std::array<unsigned char, 65536> output{};
  DWORD returned = 0;
  if (!DeviceIoControl(volume.get(), FSCTL_LOOKUP_STREAM_FROM_CLUSTER, &input,
                       sizeof(input), output.data(),
                       static_cast<DWORD>(output.size()), &returned,
                       nullptr)) {
    const DWORD error = GetLastError();
    (void)DeleteFileW(probe.c_str());
    if (unsupported_control_error(error)) {
      std::cout << "fsctl_lookup_stream_from_cluster=UNSUPPORTED error="
                << error << '\n';
      return true;
    }
    failure = "FSCTL_LOOKUP_STREAM_FROM_CLUSTER failed: " +
              std::to_string(error);
    return false;
  }
  supported = true;

  if (returned < sizeof(LOOKUP_STREAM_FROM_CLUSTER_OUTPUT)) {
    failure = "FSCTL_LOOKUP_STREAM_FROM_CLUSTER returned a truncated header";
    (void)DeleteFileW(probe.c_str());
    return false;
  }
  const auto *const information =
      reinterpret_cast<const LOOKUP_STREAM_FROM_CLUSTER_OUTPUT *>(
          output.data());
  std::size_t offset = information->Offset;
  bool found_visible = false;
  while (offset != 0 && offset < returned) {
    constexpr std::size_t name_offset =
        offsetof(LOOKUP_STREAM_FROM_CLUSTER_ENTRY, FileName);
    if (name_offset > returned - offset) {
      failure = "cluster lookup returned a truncated entry";
      (void)DeleteFileW(probe.c_str());
      return false;
    }
    const auto *const entry =
        reinterpret_cast<const LOOKUP_STREAM_FROM_CLUSTER_ENTRY *>(
            output.data() + offset);
    const std::size_t record_bytes =
        entry->OffsetToNext != 0 ? entry->OffsetToNext : returned - offset;
    if (record_bytes < name_offset || record_bytes > returned - offset) {
      failure = "cluster lookup returned an invalid entry chain";
      (void)DeleteFileW(probe.c_str());
      return false;
    }
    const std::size_t name_capacity =
        (record_bytes - name_offset) / sizeof(wchar_t);
    std::size_t name_characters = 0;
    while (name_characters < name_capacity &&
           entry->FileName[name_characters] != L'\0')
      ++name_characters;
    if (name_characters == name_capacity) {
      failure = "cluster lookup returned an unterminated name";
      (void)DeleteFileW(probe.c_str());
      return false;
    }
    const std::wstring_view name{entry->FileName, name_characters};
    if (contains_case_insensitive(name, real_mapping)) {
      failure = "cluster lookup exposed the backing mapping";
      (void)DeleteFileW(probe.c_str());
      return false;
    }
    if (contains_case_insensitive(name, user_mapping) &&
        contains_case_insensitive(name, probe.filename().wstring()))
      found_visible = true;
    if (entry->OffsetToNext == 0)
      break;
    offset += entry->OffsetToNext;
  }
  (void)DeleteFileW(probe.c_str());
  if (!found_visible) {
    failure = "cluster lookup omitted the visible probe path";
    return false;
  }
  std::cout << "fsctl_lookup_stream_from_cluster=PASS\n";
  return true;
}

bool parse_find_by_sid_names(const unsigned char *buffer,
                             std::size_t returned,
                             std::vector<std::wstring> &names,
                             std::string &failure) {
  names.clear();
  if (returned == 0)
    return true;

  constexpr std::size_t packed_name_offset =
      offsetof(file_name_information_record, file_name);
  std::size_t offset = 0;
  while (offset < returned) {
    if (packed_name_offset > returned - offset) {
      failure = "FSCTL_FIND_FILES_BY_SID returned a truncated packed entry";
      return false;
    }
    const auto *const entry =
        reinterpret_cast<const file_name_information_record *>(buffer +
                                                               offset);
    if ((entry->file_name_length % sizeof(wchar_t)) != 0 ||
        entry->file_name_length >
            returned - offset - packed_name_offset) {
      failure = "FSCTL_FIND_FILES_BY_SID returned an invalid packed name";
      return false;
    }
    names.emplace_back(entry->file_name,
                       entry->file_name_length / sizeof(wchar_t));
    const std::size_t record_bytes =
        (packed_name_offset + entry->file_name_length + 7u) &
        ~std::size_t{7u};
    if (record_bytes > returned - offset) {
      failure = "FSCTL_FIND_FILES_BY_SID returned an invalid packed record";
      return false;
    }
    offset += record_bytes;
  }
  return offset == returned;
}

std::wstring find_by_sid_stress_file_name(std::uint32_t index) {
  std::wstring name = L"sid-";
  const std::wstring digits = std::to_wstring(index);
  name.append(6 - digits.size(), L'0');
  name.append(digits);
  name.push_back(L'-');
  name.append(175, L'x');
  name.append(L".tmp");
  return name;
}

bool verify_find_files_by_sid_stress(
    const fs::path &visible_mapping, FIND_BY_SID_DATA *input,
    DWORD input_bytes, std::string &failure) {
  unique_handle directory(
      CreateFileW(visible_mapping.parent_path().c_str(),
                  FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS,
                  nullptr));
  if (!directory.valid()) {
    failure = "failed to open the visible parent for Find-by-SID stress";
    return false;
  }

  constexpr std::size_t packed_name_offset =
      offsetof(file_name_information_record, file_name);
  const std::wstring source_prefix =
      std::wstring(L"find-by-sid-stress\\");
  std::uint64_t expected_source_bytes = 0;
  for (std::uint32_t index = 0; index != find_by_sid_stress_file_count;
       ++index) {
    const std::size_t name_bytes =
        (source_prefix.size() + find_by_sid_stress_file_name(index).size()) *
        sizeof(wchar_t);
    expected_source_bytes +=
        (packed_name_offset + name_bytes + 7u) & ~std::size_t{7u};
  }
  constexpr std::uint64_t old_internal_limit = 16ull * 1024 * 1024;
  if (expected_source_bytes <= old_internal_limit) {
    failure = "the Find-by-SID stress fixture does not exceed 16 MiB";
    return false;
  }

  std::vector<unsigned char> seen(find_by_sid_stress_file_count, 0);
  std::array<unsigned char, 65536> output{};
  std::uint32_t found = 0;
  std::uint64_t returned_bytes = 0;
  input->Restart = TRUE;
  for (unsigned iteration = 0; iteration != 8192; ++iteration) {
    output.fill(0);
    DWORD returned = 0;
    const BOOL completed = DeviceIoControl(
        directory.get(), FSCTL_FIND_FILES_BY_SID, input, input_bytes,
        output.data(), static_cast<DWORD>(output.size()), &returned, nullptr);
    if (!completed) {
      const DWORD error = GetLastError();
      if (error == ERROR_NO_MORE_FILES || error == ERROR_HANDLE_EOF)
        break;
      if (error != ERROR_MORE_DATA || returned == 0) {
        failure = "FSCTL_FIND_FILES_BY_SID stress failed: " +
                  std::to_string(error);
        return false;
      }
    }

    returned_bytes += returned;
    std::vector<std::wstring> names;
    if (!parse_find_by_sid_names(output.data(), returned, names, failure))
      return false;
    for (const auto &name : names) {
      std::wstring_view relative{name};
      if (!relative.empty() && relative.front() == L'\\')
        relative.remove_prefix(1);

      const std::wstring visible_prefix =
          std::wstring(user_mapping_final_component) +
          L"\\find-by-sid-stress\\sid-";
      if (!relative.starts_with(visible_prefix))
        continue;
      if (relative.size() < visible_prefix.size() + 6 ||
          relative[visible_prefix.size() + 6] != L'-') {
        failure = "Find-by-SID stress returned a malformed fixture name";
        return false;
      }

      std::uint32_t index = 0;
      for (std::size_t digit = 0; digit != 6; ++digit) {
        const wchar_t character = relative[visible_prefix.size() + digit];
        if (character < L'0' || character > L'9') {
          failure = "Find-by-SID stress returned a nonnumeric fixture name";
          return false;
        }
        index = index * 10 + static_cast<std::uint32_t>(character - L'0');
      }
      if (index >= find_by_sid_stress_file_count) {
        failure = "Find-by-SID stress returned an out-of-range fixture name";
        return false;
      }
      if (seen[index] != 0) {
        failure = "Find-by-SID stress returned a duplicate fixture name";
        return false;
      }
      seen[index] = 1;
      ++found;
    }
    input->Restart = FALSE;
    if (returned == 0)
      break;
  }

  if (found != find_by_sid_stress_file_count) {
    failure = "Find-by-SID stress returned " + std::to_string(found) +
              " of " + std::to_string(find_by_sid_stress_file_count) +
              " fixture files";
    return false;
  }
  if (returned_bytes <= old_internal_limit) {
    failure = "Find-by-SID stress did not traverse more than 16 MiB";
    return false;
  }

  std::cout << "fsctl_find_files_by_sid_stress=PASS files=" << found
            << " source_bytes=" << expected_source_bytes
            << " returned_bytes=" << returned_bytes << '\n';
  return true;
}

bool verify_find_files_by_sid(const fs::path &visible_mapping,
                              bool run_stress, bool &supported,
                              std::string &failure) {
  supported = false;
  const fs::path target = visible_mapping / payload_name;

  PSID owner = nullptr;
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  const DWORD security_status = GetNamedSecurityInfoW(
      const_cast<PWSTR>(target.c_str()), SE_FILE_OBJECT,
      OWNER_SECURITY_INFORMATION, &owner, nullptr, nullptr, nullptr,
      &descriptor);
  if (security_status != ERROR_SUCCESS || !owner || !IsValidSid(owner)) {
    if (descriptor)
      LocalFree(descriptor);
    failure = "failed to obtain the find-by-SID probe owner: " +
              std::to_string(security_status);
    return false;
  }

  const DWORD sid_bytes = GetLengthSid(owner);
  std::vector<unsigned char> input_storage(
      offsetof(FIND_BY_SID_DATA, Sid) + sid_bytes);
  auto *const input =
      reinterpret_cast<FIND_BY_SID_DATA *>(input_storage.data());
  input->Restart = TRUE;
  if (!CopySid(sid_bytes, &input->Sid, owner)) {
    LocalFree(descriptor);
    failure = "CopySid failed for FSCTL_FIND_FILES_BY_SID";
    return false;
  }
  LocalFree(descriptor);

  unique_handle root(
      CreateFileW(visible_mapping.root_path().c_str(),
                  FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS,
                  nullptr));
  if (!root.valid()) {
    failure = "failed to open the volume root for FSCTL_FIND_FILES_BY_SID";
    return false;
  }

  std::array<unsigned char, 65536> output{};
  bool found_visible = false;
  std::string observed_names;
  unsigned completed_calls = 0;
  for (unsigned iteration = 0; iteration != 4096; ++iteration) {
    DWORD returned = 0;
    if (!DeviceIoControl(root.get(), FSCTL_FIND_FILES_BY_SID, input,
                         static_cast<DWORD>(input_storage.size()),
                         output.data(), static_cast<DWORD>(output.size()),
                         &returned, nullptr)) {
      const DWORD error = GetLastError();
      if (iteration == 0 && unsupported_control_error(error)) {
        std::cout << "fsctl_find_files_by_sid=UNSUPPORTED error=" << error
                  << '\n';
        return true;
      }
      if (error == ERROR_NO_MORE_FILES || error == ERROR_HANDLE_EOF)
        break;
      failure = "FSCTL_FIND_FILES_BY_SID failed: " +
                std::to_string(error);
      return false;
    }
    supported = true;
    ++completed_calls;
    if (returned == 0)
      break;
    std::vector<std::wstring> names;
    if (!parse_find_by_sid_names(output.data(), returned, names, failure)) {
      return false;
    }
    for (const auto &name : names) {
      if (observed_names.size() < 2048) {
        if (!observed_names.empty())
          observed_names.push_back(',');
        for (const wchar_t character : name)
          observed_names.push_back(static_cast<char>(character));
      }
      if (!contains_case_insensitive(name, target.filename().wstring()))
        continue;
      if (contains_mapping_path(name, real_mapping)) {
        failure = "FSCTL_FIND_FILES_BY_SID exposed the backing mapping";
        return false;
      }
      if (contains_mapping_path(name, user_mapping))
        found_visible = true;
    }
    if (found_visible)
      break;
    input->Restart = FALSE;
  }
  if (!found_visible) {
    failure =
        "FSCTL_FIND_FILES_BY_SID omitted the visible payload path from the "
        "volume-root query; calls=" +
        std::to_string(completed_calls) + " observed=" + observed_names;
    return false;
  }

  unique_handle visible_parent(
      CreateFileW(visible_mapping.parent_path().c_str(),
                  FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS,
                  nullptr));
  if (!visible_parent.valid()) {
    failure =
        "failed to open the visible parent for FSCTL_FIND_FILES_BY_SID";
    return false;
  }

  std::wstring expected(user_mapping_final_component);
  expected.push_back(L'\\');
  expected.append(payload_name);
  std::wstring backing(real_mapping_final_component);
  backing.push_back(L'\\');
  backing.append(payload_name);

  auto verify_visible_parent_calls =
      [&](HANDLE directory, unsigned char *query_output,
          DWORD query_output_bytes, std::string_view label) -> bool {
    input->Restart = TRUE;
    unsigned payload_matches = 0;
    for (unsigned iteration = 0; iteration != 4096; ++iteration) {
      std::fill_n(query_output, query_output_bytes,
                  static_cast<unsigned char>(0));
      DWORD returned = 0;
      const BOOL completed = DeviceIoControl(
          directory, FSCTL_FIND_FILES_BY_SID, input,
          static_cast<DWORD>(input_storage.size()), query_output,
          query_output_bytes, &returned, nullptr);
      if (!completed) {
        const DWORD error = GetLastError();
        if (error == ERROR_NO_MORE_FILES || error == ERROR_HANDLE_EOF)
          break;
        if (error != ERROR_MORE_DATA || returned == 0) {
          failure = "FSCTL_FIND_FILES_BY_SID failed during " +
                    std::string(label) + ": " + std::to_string(error);
          return false;
        }
      }

      std::vector<std::wstring> names;
      if (!parse_find_by_sid_names(query_output, returned, names, failure))
        return false;
      for (const auto &name : names) {
        std::wstring_view relative{name};
        if (!relative.empty() && relative.front() == L'\\')
          relative.remove_prefix(1);
        if (contains_mapping_path(relative, real_mapping) ||
            equal_name(relative, backing)) {
          failure =
              "the visible-parent FSCTL_FIND_FILES_BY_SID query exposed the "
              "backing mapping during " +
              std::string(label);
          return false;
        }
        if (equal_name(relative, expected))
          ++payload_matches;
      }
      input->Restart = FALSE;
      if (returned == 0)
        break;
    }
    if (payload_matches != 1) {
      failure = "FSCTL_FIND_FILES_BY_SID returned graft\\payload.txt " +
                std::to_string(payload_matches) + " times during " +
                std::string(label);
      return false;
    }
    return true;
  };

  if (!verify_visible_parent_calls(
          visible_parent.get(), output.data(),
          static_cast<DWORD>(output.size()), "large-buffer continuation"))
    return false;

  if (!run_stress) {
    unique_handle paged_visible_parent(
        CreateFileW(visible_mapping.parent_path().c_str(),
                    FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS,
                    nullptr));
    std::array<unsigned char, 128> paged_output{};
    if (!paged_visible_parent.valid() ||
        !verify_visible_parent_calls(
            paged_visible_parent.get(), paged_output.data(),
            static_cast<DWORD>(paged_output.size()),
            "small-buffer continuation")) {
      if (!paged_visible_parent.valid())
        failure =
            "failed to reopen the visible parent for paged find-by-SID";
      return false;
    }
  } else {
    std::cout << "fsctl_find_files_by_sid_small_buffer="
                 "COVERED_BY_STANDARD_FIXTURE\n";
  }

  const fs::path backing_parent =
      visible_mapping.root_path() / L"crtsys-namechanger-store";
  unique_handle backing_parent_handle(
      CreateFileW(backing_parent.c_str(),
                  FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS,
                  nullptr));
  if (!backing_parent_handle.valid()) {
    failure =
        "failed to open the backing parent for FSCTL_FIND_FILES_BY_SID";
    return false;
  }

  input->Restart = TRUE;
  output.fill(0);
  DWORD returned = 0;
  if (!DeviceIoControl(backing_parent_handle.get(), FSCTL_FIND_FILES_BY_SID,
                       input, static_cast<DWORD>(input_storage.size()),
                       output.data(), static_cast<DWORD>(output.size()),
                       &returned, nullptr)) {
    const DWORD error = GetLastError();
    if (error != ERROR_NO_MORE_FILES && error != ERROR_HANDLE_EOF) {
      failure =
          "FSCTL_FIND_FILES_BY_SID failed on the backing parent: " +
          std::to_string(error);
      return false;
    }
    returned = 0;
  }

  std::vector<std::wstring> backing_names;
  if (!parse_find_by_sid_names(output.data(), returned, backing_names, failure))
    return false;
  for (const auto &name : backing_names) {
    std::wstring_view relative{name};
    if (!relative.empty() && relative.front() == L'\\')
      relative.remove_prefix(1);
    if (equal_name(relative, backing) ||
        contains_mapping_path(relative, real_mapping)) {
      failure =
          "the backing-parent FSCTL_FIND_FILES_BY_SID query exposed the "
          "hidden real mapping";
      return false;
    }
  }

  if (run_stress &&
      !verify_find_files_by_sid_stress(
          visible_mapping, input,
          static_cast<DWORD>(input_storage.size()), failure))
    return false;

  std::cout << "fsctl_find_files_by_sid=PASS\n";
  return true;
}

bool same_file_identity(const fs::path &left, const fs::path &right,
                        std::string &failure) {
  unique_handle left_file(
      CreateFileW(left.c_str(), FILE_READ_ATTRIBUTES,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
  unique_handle right_file(
      CreateFileW(right.c_str(), FILE_READ_ATTRIBUTES,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
  BY_HANDLE_FILE_INFORMATION left_information{};
  BY_HANDLE_FILE_INFORMATION right_information{};
  if (!left_file.valid() || !right_file.valid() ||
      !GetFileInformationByHandle(left_file.get(), &left_information) ||
      !GetFileInformationByHandle(right_file.get(), &right_information)) {
    failure = "failed to query hard-link file identity";
    return false;
  }
  if (left_information.dwVolumeSerialNumber !=
          right_information.dwVolumeSerialNumber ||
      left_information.nFileIndexHigh != right_information.nFileIndexHigh ||
      left_information.nFileIndexLow != right_information.nFileIndexLow) {
    failure = "the visible hard link refers to a different file";
    return false;
  }
  return true;
}

bool write_small_file(const fs::path &path, std::string &failure) {
  unique_handle file(
      CreateFileW(path.c_str(), GENERIC_WRITE,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
  constexpr char contents[] = "notification\n";
  DWORD written = 0;
  if (!file.valid() ||
      !WriteFile(file.get(), contents, sizeof(contents) - 1, &written,
                 nullptr) ||
      written != sizeof(contents) - 1) {
    failure = "failed to create the notification test file: " +
              std::to_string(GetLastError());
    return false;
  }
  return true;
}

template <class Action>
bool observe_directory_change(HANDLE directory, std::wstring_view expected_name,
                              Action action, std::string &failure) {
  std::array<unsigned char, 8192> buffer{};
  unique_handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
  if (!event.valid()) {
    failure = "failed to create the directory notification event";
    return false;
  }

  std::string observed_names;
  bool action_invoked = false;
  for (unsigned attempt = 0; attempt != 4; ++attempt) {
    std::fill(buffer.begin(), buffer.end(), static_cast<unsigned char>(0));
    (void)ResetEvent(event.get());
    OVERLAPPED overlapped{};
    overlapped.hEvent = event.get();
    if (!ReadDirectoryChangesW(
            directory, buffer.data(), static_cast<DWORD>(buffer.size()), FALSE,
            FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_SIZE |
                FILE_NOTIFY_CHANGE_LAST_WRITE,
            nullptr, &overlapped, nullptr) &&
        GetLastError() != ERROR_IO_PENDING) {
      failure = "ReadDirectoryChangesW failed to arm: " +
                std::to_string(GetLastError());
      return false;
    }

    if (!action_invoked) {
      action_invoked = true;
      if (!action()) {
        (void)CancelIoEx(directory, &overlapped);
        return false;
      }
    }
    if (WaitForSingleObject(event.get(), 10000) != WAIT_OBJECT_0) {
      (void)CancelIoEx(directory, &overlapped);
      failure = "directory notification timed out";
      return false;
    }

    DWORD returned = 0;
    if (!GetOverlappedResult(directory, &overlapped, &returned, FALSE)) {
      failure = "GetOverlappedResult failed for directory notification: " +
                std::to_string(GetLastError());
      return false;
    }

    const record_validation::linked_name_record_layout notification_layout{
        offsetof(FILE_NOTIFY_INFORMATION, NextEntryOffset),
        offsetof(FILE_NOTIFY_INFORMATION, FileNameLength),
        offsetof(FILE_NOTIFY_INFORMATION, FileName), 4};
    if (!record_validation::validate_linked_name_record_chain(
            buffer.data(), returned, notification_layout)) {
      failure = "directory notification returned an invalid record chain";
      return false;
    }

    std::size_t offset = 0;
    while (offset < returned) {
      if (returned - offset < offsetof(FILE_NOTIFY_INFORMATION, FileName)) {
        failure = "directory notification returned a truncated record";
        return false;
      }
      const auto *const record =
          reinterpret_cast<const FILE_NOTIFY_INFORMATION *>(buffer.data() +
                                                            offset);
      const std::size_t name_offset =
          offset + offsetof(FILE_NOTIFY_INFORMATION, FileName);
      if ((record->FileNameLength % sizeof(wchar_t)) != 0 ||
          name_offset > returned ||
          record->FileNameLength > returned - name_offset) {
        failure = "directory notification returned an invalid name";
        return false;
      }
      const std::wstring_view name{record->FileName,
                                   record->FileNameLength / sizeof(wchar_t)};
      if (equal_name(name, expected_name))
        return true;
      if (!observed_names.empty())
        observed_names.append(",");
      for (const wchar_t character : name)
        observed_names.push_back(static_cast<char>(character));
      if (record->NextEntryOffset == 0 ||
          record->NextEntryOffset > returned - offset)
        break;
      offset += record->NextEntryOffset;
    }
  }
  failure = "directory notification omitted the expected visible child name; "
            "observed=" +
            observed_names;
  return false;
}

bool verify_notifications(const fs::path &visible_mapping,
                          std::string &failure) {
  unique_handle directory(
      CreateFileW(visible_mapping.c_str(), FILE_LIST_DIRECTORY,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING,
                  FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr));
  if (!directory.valid()) {
    failure = "failed to open the visible mapping for notifications: " +
              std::to_string(GetLastError());
    return false;
  }

  const fs::path created = visible_mapping / notification_created_name;
  const fs::path renamed = visible_mapping / notification_renamed_name;
  if (!observe_directory_change(
          directory.get(), notification_created_name,
          [&] { return write_small_file(created, failure); }, failure))
    return false;

  return observe_directory_change(
      directory.get(), notification_renamed_name,
      [&] {
        if (MoveFileExW(created.c_str(), renamed.c_str(),
                        MOVEFILE_REPLACE_EXISTING))
          return true;
        failure = "failed to rename the notification test file: " +
                  std::to_string(GetLastError());
        return false;
      },
      failure);
}

bool verify_read_file_usn(const fs::path &visible_mapping,
                          std::string &failure) {
  unique_handle mapping(
      CreateFileW(visible_mapping.c_str(), GENERIC_READ,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
  if (!mapping.valid()) {
    failure = "failed to open the visible mapping for USN query";
    return false;
  }

  constexpr std::size_t output_bytes = 4096;
  std::array<unsigned char, output_bytes + 64> storage{};
  for (std::uint32_t iteration = 0; iteration != usn_query_iterations;
       ++iteration) {
    const std::size_t offset = (iteration % 8) * 8;
    auto *const buffer = storage.data() + offset;
    std::fill_n(buffer, output_bytes, static_cast<unsigned char>(0));

    DWORD returned = 0;
    if (!DeviceIoControl(mapping.get(), FSCTL_READ_FILE_USN_DATA, nullptr, 0,
                         buffer, static_cast<DWORD>(output_bytes), &returned,
                         nullptr)) {
      failure = "FSCTL_READ_FILE_USN_DATA failed at iteration " +
                std::to_string(iteration) + ": " +
                std::to_string(GetLastError());
      return false;
    }
    if (returned < sizeof(usn_common_header)) {
      failure =
          "FSCTL_READ_FILE_USN_DATA returned a truncated record at iteration " +
          std::to_string(iteration);
      return false;
    }

    const auto *const header =
        reinterpret_cast<const usn_common_header *>(buffer);
    record_validation::usn_name_record_layout layout{};
    if (header->major_version == 2) {
      layout = {2, offsetof(usn_v2_prefix, file_name_offset) + sizeof(WORD),
                offsetof(usn_v2_prefix, file_name_length),
                offsetof(usn_v2_prefix, file_name_offset)};
    } else if (header->major_version == 3) {
      layout = {3, offsetof(usn_v3_prefix, file_name_offset) + sizeof(WORD),
                offsetof(usn_v3_prefix, file_name_length),
                offsetof(usn_v3_prefix, file_name_offset)};
    } else {
      failure =
          "FSCTL_READ_FILE_USN_DATA returned an unsupported USN version at "
          "iteration " +
          std::to_string(iteration);
      return false;
    }

    record_validation::bounded_name name_bounds;
    if (!record_validation::try_read_usn_name(buffer, returned, layout,
                                              name_bounds)) {
      failure =
          "FSCTL_READ_FILE_USN_DATA returned an invalid name at iteration " +
          std::to_string(iteration);
      return false;
    }
    const std::wstring_view name{
        reinterpret_cast<const wchar_t *>(buffer + name_bounds.offset),
        name_bounds.size_bytes / sizeof(wchar_t)};
    if (!equal_name(name, user_mapping_final_component)) {
      failure = "FSCTL_READ_FILE_USN_DATA exposed the backing component at "
                "iteration " +
                std::to_string(iteration);
      return false;
    }
  }
  return true;
}

} // namespace

bool prepare_find_by_sid_stress(const fs::path &physical_mapping,
                                std::string &failure) {
  const fs::path directory = physical_mapping / L"find-by-sid-stress";
  std::error_code error;
  if (!fs::create_directories(directory, error) && error) {
    failure = "failed to create the stress directory: " + error.message();
    return false;
  }

  for (std::uint32_t index = 0; index != find_by_sid_stress_file_count;
       ++index) {
    const fs::path file = directory / find_by_sid_stress_file_name(index);
    unique_handle created(
        CreateFileW(file.c_str(), FILE_WRITE_ATTRIBUTES,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!created.valid()) {
      failure = "failed to create Find-by-SID stress file " +
                std::to_string(index) + ": " +
                std::to_string(GetLastError());
      return false;
    }
    if ((index + 1) % 4096 == 0)
      std::cout << "find_by_sid_stress_prepared=" << index + 1 << '/'
                << find_by_sid_stress_file_count << '\n';
  }
  std::cout << "find_by_sid_stress_prepared="
            << find_by_sid_stress_file_count << '/'
            << find_by_sid_stress_file_count << '\n';
  return true;
}

bool run_namespace_tests(const fs::path &visible_mapping,
                         const fs::path &visible_created,
                         const fs::path &visible_renamed,
                         const fs::path &visible_hard_link,
                         bool run_find_by_sid_stress,
                         namespace_feature_support &support,
                         std::string &failure) {
  if (!verify_query_information(visible_mapping, visible_created, failure))
    return false;

  if (!MoveFileExW(visible_created.c_str(), visible_renamed.c_str(),
                   MOVEFILE_REPLACE_EXISTING)) {
    failure = "rename through the visible mapping failed: " +
              std::to_string(GetLastError());
    return false;
  }
  if (!verify_query_information(visible_mapping, visible_renamed, failure))
    return false;

  if (!CreateHardLinkW(visible_hard_link.c_str(), visible_renamed.c_str(),
                       nullptr)) {
    failure = "hard-link creation through the visible mapping failed: " +
              std::to_string(GetLastError());
    return false;
  }
  if (!same_file_identity(visible_renamed, visible_hard_link, failure))
    return false;
  if (!verify_hard_link_information(visible_mapping, failure))
    return false;
  if (!verify_notifications(visible_mapping, failure))
    return false;
  if (!verify_read_file_usn(visible_mapping, failure))
    return false;
  if (!verify_enum_usn_data(visible_mapping, support.enum_usn, failure))
    return false;
  if (!verify_read_usn_journal(visible_mapping, support.read_usn_journal,
                               failure))
    return false;
  if (!verify_find_files_by_sid(visible_mapping, run_find_by_sid_stress,
                                support.find_files_by_sid, failure))
    return false;
  return verify_lookup_stream_from_cluster(
      visible_mapping, support.lookup_stream_from_cluster, failure);
}

} // namespace crtsys_flt_name_changer_runtime_test
