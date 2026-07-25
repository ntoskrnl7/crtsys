#define WIN32_NO_STATUS
#include <Windows.h>
#undef WIN32_NO_STATUS
using NTSTATUS = LONG;
#include <ntstatus.h>

#include "directory_tests.hpp"

#include "../name_changer_shared/name_changer_runtime.hpp"
#include "../name_changer_shared/output_record_validation.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace crtsys_flt_name_changer_runtime_test {
namespace {

namespace fs = std::filesystem;

constexpr std::size_t absent_offset = (std::numeric_limits<std::size_t>::max)();
constexpr std::size_t guard_bytes = 32;

struct native_unicode_string {
  USHORT length = 0;
  USHORT maximum_length = 0;
  PWSTR buffer = nullptr;
};

struct native_io_status_block {
  union {
    LONG status;
    void *pointer;
  };
  ULONG_PTR information;
};

using nt_query_directory_file_fn = LONG(NTAPI *)(HANDLE, HANDLE, void *, void *,
                                                 native_io_status_block *,
                                                 void *, ULONG, ULONG, BOOLEAN,
                                                 native_unicode_string *,
                                                 BOOLEAN);

struct file_directory_information_record {
  ULONG next_entry_offset;
  ULONG file_index;
  LARGE_INTEGER creation_time;
  LARGE_INTEGER last_access_time;
  LARGE_INTEGER last_write_time;
  LARGE_INTEGER change_time;
  LARGE_INTEGER end_of_file;
  LARGE_INTEGER allocation_size;
  ULONG file_attributes;
  ULONG file_name_length;
  WCHAR file_name[1];
};

struct file_full_directory_information_record {
  ULONG next_entry_offset;
  ULONG file_index;
  LARGE_INTEGER creation_time;
  LARGE_INTEGER last_access_time;
  LARGE_INTEGER last_write_time;
  LARGE_INTEGER change_time;
  LARGE_INTEGER end_of_file;
  LARGE_INTEGER allocation_size;
  ULONG file_attributes;
  ULONG file_name_length;
  ULONG ea_size;
  WCHAR file_name[1];
};

struct file_both_directory_information_record {
  ULONG next_entry_offset;
  ULONG file_index;
  LARGE_INTEGER creation_time;
  LARGE_INTEGER last_access_time;
  LARGE_INTEGER last_write_time;
  LARGE_INTEGER change_time;
  LARGE_INTEGER end_of_file;
  LARGE_INTEGER allocation_size;
  ULONG file_attributes;
  ULONG file_name_length;
  ULONG ea_size;
  UCHAR short_name_length;
  WCHAR short_name[12];
  WCHAR file_name[1];
};

struct file_names_information_record {
  ULONG next_entry_offset;
  ULONG file_index;
  ULONG file_name_length;
  WCHAR file_name[1];
};

struct file_id_both_directory_information_record {
  ULONG next_entry_offset;
  ULONG file_index;
  LARGE_INTEGER creation_time;
  LARGE_INTEGER last_access_time;
  LARGE_INTEGER last_write_time;
  LARGE_INTEGER change_time;
  LARGE_INTEGER end_of_file;
  LARGE_INTEGER allocation_size;
  ULONG file_attributes;
  ULONG file_name_length;
  ULONG ea_size;
  UCHAR short_name_length;
  WCHAR short_name[12];
  LARGE_INTEGER file_id;
  WCHAR file_name[1];
};

struct file_id_full_directory_information_record {
  ULONG next_entry_offset;
  ULONG file_index;
  LARGE_INTEGER creation_time;
  LARGE_INTEGER last_access_time;
  LARGE_INTEGER last_write_time;
  LARGE_INTEGER change_time;
  LARGE_INTEGER end_of_file;
  LARGE_INTEGER allocation_size;
  ULONG file_attributes;
  ULONG file_name_length;
  ULONG ea_size;
  LARGE_INTEGER file_id;
  WCHAR file_name[1];
};

struct file_id_128_record {
  UCHAR identifier[16];
};

struct file_id_extd_directory_information_record {
  ULONG next_entry_offset;
  ULONG file_index;
  LARGE_INTEGER creation_time;
  LARGE_INTEGER last_access_time;
  LARGE_INTEGER last_write_time;
  LARGE_INTEGER change_time;
  LARGE_INTEGER end_of_file;
  LARGE_INTEGER allocation_size;
  ULONG file_attributes;
  ULONG file_name_length;
  ULONG ea_size;
  ULONG reparse_point_tag;
  file_id_128_record file_id;
  WCHAR file_name[1];
};

struct file_id_extd_both_directory_information_record {
  ULONG next_entry_offset;
  ULONG file_index;
  LARGE_INTEGER creation_time;
  LARGE_INTEGER last_access_time;
  LARGE_INTEGER last_write_time;
  LARGE_INTEGER change_time;
  LARGE_INTEGER end_of_file;
  LARGE_INTEGER allocation_size;
  ULONG file_attributes;
  ULONG file_name_length;
  ULONG ea_size;
  ULONG reparse_point_tag;
  file_id_128_record file_id;
  UCHAR short_name_length;
  WCHAR short_name[12];
  WCHAR file_name[1];
};

struct file_id_64_extd_directory_information_record {
  ULONG next_entry_offset;
  ULONG file_index;
  LARGE_INTEGER creation_time;
  LARGE_INTEGER last_access_time;
  LARGE_INTEGER last_write_time;
  LARGE_INTEGER change_time;
  LARGE_INTEGER end_of_file;
  LARGE_INTEGER allocation_size;
  ULONG file_attributes;
  ULONG file_name_length;
  ULONG ea_size;
  ULONG reparse_point_tag;
  LARGE_INTEGER file_id;
  WCHAR file_name[1];
};

struct file_id_64_extd_both_directory_information_record {
  ULONG next_entry_offset;
  ULONG file_index;
  LARGE_INTEGER creation_time;
  LARGE_INTEGER last_access_time;
  LARGE_INTEGER last_write_time;
  LARGE_INTEGER change_time;
  LARGE_INTEGER end_of_file;
  LARGE_INTEGER allocation_size;
  ULONG file_attributes;
  ULONG file_name_length;
  ULONG ea_size;
  ULONG reparse_point_tag;
  LARGE_INTEGER file_id;
  UCHAR short_name_length;
  WCHAR short_name[12];
  WCHAR file_name[1];
};

struct directory_class {
  const char *name;
  ULONG value;
  std::size_t next_offset;
  std::size_t name_length_offset;
  std::size_t name_offset;
  std::size_t creation_time_offset = absent_offset;
  std::size_t last_access_time_offset = absent_offset;
  std::size_t last_write_time_offset = absent_offset;
  std::size_t change_time_offset = absent_offset;
  std::size_t end_of_file_offset = absent_offset;
  std::size_t allocation_size_offset = absent_offset;
  std::size_t attributes_offset = absent_offset;
  std::size_t ea_size_offset = absent_offset;
  std::size_t file_id_offset = absent_offset;
  std::size_t file_id_size = 0;
  std::size_t short_name_length_offset = absent_offset;
  std::size_t short_name_offset = absent_offset;
};

#define COMMON_DIRECTORY_OFFSETS(type)                                         \
  offsetof(type, creation_time), offsetof(type, last_access_time),             \
      offsetof(type, last_write_time), offsetof(type, change_time),            \
      offsetof(type, end_of_file), offsetof(type, allocation_size),            \
      offsetof(type, file_attributes)

constexpr std::array<directory_class, 10> directory_classes{{
    {"FileDirectoryInformation", 1,
     offsetof(file_directory_information_record, next_entry_offset),
     offsetof(file_directory_information_record, file_name_length),
     offsetof(file_directory_information_record, file_name),
     COMMON_DIRECTORY_OFFSETS(file_directory_information_record)},
    {"FileFullDirectoryInformation", 2,
     offsetof(file_full_directory_information_record, next_entry_offset),
     offsetof(file_full_directory_information_record, file_name_length),
     offsetof(file_full_directory_information_record, file_name),
     COMMON_DIRECTORY_OFFSETS(file_full_directory_information_record),
     offsetof(file_full_directory_information_record, ea_size)},
    {"FileBothDirectoryInformation", 3,
     offsetof(file_both_directory_information_record, next_entry_offset),
     offsetof(file_both_directory_information_record, file_name_length),
     offsetof(file_both_directory_information_record, file_name),
     COMMON_DIRECTORY_OFFSETS(file_both_directory_information_record),
     offsetof(file_both_directory_information_record, ea_size), absent_offset,
     0,
     offsetof(file_both_directory_information_record, short_name_length),
     offsetof(file_both_directory_information_record, short_name)},
    {"FileNamesInformation", 12,
     offsetof(file_names_information_record, next_entry_offset),
     offsetof(file_names_information_record, file_name_length),
     offsetof(file_names_information_record, file_name)},
    {"FileIdBothDirectoryInformation", 37,
     offsetof(file_id_both_directory_information_record, next_entry_offset),
     offsetof(file_id_both_directory_information_record, file_name_length),
     offsetof(file_id_both_directory_information_record, file_name),
     COMMON_DIRECTORY_OFFSETS(file_id_both_directory_information_record),
     offsetof(file_id_both_directory_information_record, ea_size),
     offsetof(file_id_both_directory_information_record, file_id),
     sizeof(LARGE_INTEGER),
     offsetof(file_id_both_directory_information_record, short_name_length),
     offsetof(file_id_both_directory_information_record, short_name)},
    {"FileIdFullDirectoryInformation", 38,
     offsetof(file_id_full_directory_information_record, next_entry_offset),
     offsetof(file_id_full_directory_information_record, file_name_length),
     offsetof(file_id_full_directory_information_record, file_name),
     COMMON_DIRECTORY_OFFSETS(file_id_full_directory_information_record),
     offsetof(file_id_full_directory_information_record, ea_size),
     offsetof(file_id_full_directory_information_record, file_id),
     sizeof(LARGE_INTEGER)},
    {"FileIdExtdDirectoryInformation", 60,
     offsetof(file_id_extd_directory_information_record, next_entry_offset),
     offsetof(file_id_extd_directory_information_record, file_name_length),
     offsetof(file_id_extd_directory_information_record, file_name),
     COMMON_DIRECTORY_OFFSETS(file_id_extd_directory_information_record),
     offsetof(file_id_extd_directory_information_record, ea_size),
     offsetof(file_id_extd_directory_information_record, file_id),
     sizeof(file_id_128_record)},
    {"FileIdExtdBothDirectoryInformation", 63,
     offsetof(file_id_extd_both_directory_information_record,
              next_entry_offset),
     offsetof(file_id_extd_both_directory_information_record,
              file_name_length),
     offsetof(file_id_extd_both_directory_information_record, file_name),
     COMMON_DIRECTORY_OFFSETS(file_id_extd_both_directory_information_record),
     offsetof(file_id_extd_both_directory_information_record, ea_size),
     offsetof(file_id_extd_both_directory_information_record, file_id),
     sizeof(file_id_128_record),
     offsetof(file_id_extd_both_directory_information_record,
              short_name_length),
     offsetof(file_id_extd_both_directory_information_record, short_name)},
    {"FileId64ExtdDirectoryInformation", 78,
     offsetof(file_id_64_extd_directory_information_record,
              next_entry_offset),
     offsetof(file_id_64_extd_directory_information_record,
              file_name_length),
     offsetof(file_id_64_extd_directory_information_record, file_name),
     COMMON_DIRECTORY_OFFSETS(file_id_64_extd_directory_information_record),
     offsetof(file_id_64_extd_directory_information_record, ea_size),
     offsetof(file_id_64_extd_directory_information_record, file_id),
     sizeof(LARGE_INTEGER)},
    {"FileId64ExtdBothDirectoryInformation", 79,
     offsetof(file_id_64_extd_both_directory_information_record,
              next_entry_offset),
     offsetof(file_id_64_extd_both_directory_information_record,
              file_name_length),
     offsetof(file_id_64_extd_both_directory_information_record, file_name),
     COMMON_DIRECTORY_OFFSETS(
         file_id_64_extd_both_directory_information_record),
     offsetof(file_id_64_extd_both_directory_information_record, ea_size),
     offsetof(file_id_64_extd_both_directory_information_record, file_id),
     sizeof(LARGE_INTEGER),
     offsetof(file_id_64_extd_both_directory_information_record,
              short_name_length),
     offsetof(file_id_64_extd_both_directory_information_record, short_name)},
}};

#undef COMMON_DIRECTORY_OFFSETS

struct directory_record {
  std::wstring name;
  std::wstring short_name;
  directory_record_metadata metadata;
};

struct enumeration_result {
  std::vector<directory_record> records;
  std::size_t calls = 0;
};

class directory_handle {
public:
  explicit directory_handle(const fs::path &path) noexcept
      : value_(CreateFileW(
            path.c_str(), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr)) {}

  directory_handle(const directory_handle &) = delete;
  directory_handle &operator=(const directory_handle &) = delete;

  ~directory_handle() {
    if (valid())
      CloseHandle(value_);
  }

  bool valid() const noexcept { return value_ != INVALID_HANDLE_VALUE; }
  HANDLE get() const noexcept { return value_; }

private:
  HANDLE value_ = INVALID_HANDLE_VALUE;
};

nt_query_directory_file_fn nt_query_directory_file() noexcept {
  static const auto function = reinterpret_cast<nt_query_directory_file_fn>(
      GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryDirectoryFile"));
  return function;
}

std::string status_text(LONG status) {
  std::ostringstream output;
  output << "0x" << std::hex << std::uppercase
         << static_cast<unsigned long>(status);
  return output.str();
}

std::string narrow_ascii(std::wstring_view value) {
  std::string result;
  result.reserve(value.size());
  for (const wchar_t character : value)
    result.push_back(static_cast<char>(character));
  return result;
}

template <class T>
bool read_field(const unsigned char *record, std::size_t record_bytes,
                std::size_t offset, T &value) noexcept {
  if (offset == absent_offset || offset > record_bytes ||
      sizeof(T) > record_bytes - offset)
    return false;
  std::memcpy(&value, record + offset, sizeof(T));
  return true;
}

bool parse_directory_buffer(const directory_class &information_class,
                            const unsigned char *buffer, std::size_t bytes,
                            std::vector<directory_record> &records,
                            std::string &failure) {
  const record_validation::linked_name_record_layout validation_layout{
      information_class.next_offset, information_class.name_length_offset,
      information_class.name_offset, 8};
  if (!record_validation::validate_linked_name_record_chain(
          buffer, bytes, validation_layout)) {
    failure = std::string(information_class.name) +
              " returned an invalid record chain";
    return false;
  }

  std::size_t offset = 0;
  while (offset < bytes) {
    if (bytes - offset < information_class.name_offset) {
      failure = std::string(information_class.name) +
                " returned a truncated directory record";
      return false;
    }

    const auto *record = buffer + offset;
    ULONG next = 0;
    ULONG name_bytes = 0;
    if (!read_field(record, bytes - offset, information_class.next_offset,
                    next) ||
        !read_field(record, bytes - offset,
                    information_class.name_length_offset, name_bytes)) {
      failure = std::string(information_class.name) +
                " omitted required directory fields";
      return false;
    }

    const std::size_t record_bytes =
        next != 0 ? static_cast<std::size_t>(next) : bytes - offset;
    if (record_bytes > bytes - offset ||
        record_bytes < information_class.name_offset ||
        (next != 0 && (next & 7u) != 0) ||
        (name_bytes % sizeof(wchar_t)) != 0 ||
        name_bytes > record_bytes - information_class.name_offset) {
      failure = std::string(information_class.name) +
                " returned an invalid record chain";
      return false;
    }

    directory_record parsed;
    parsed.name.assign(reinterpret_cast<const wchar_t *>(
                           record + information_class.name_offset),
                       name_bytes / sizeof(wchar_t));

    auto &metadata = parsed.metadata;
    LARGE_INTEGER value{};
    if (read_field(record, record_bytes, information_class.creation_time_offset,
                   value)) {
      metadata.has_common_metadata = true;
      metadata.creation_time = value.QuadPart;
      read_field(record, record_bytes,
                 information_class.last_access_time_offset, value);
      metadata.last_access_time = value.QuadPart;
      read_field(record, record_bytes, information_class.last_write_time_offset,
                 value);
      metadata.last_write_time = value.QuadPart;
      read_field(record, record_bytes, information_class.change_time_offset,
                 value);
      metadata.change_time = value.QuadPart;
      read_field(record, record_bytes, information_class.end_of_file_offset,
                 value);
      metadata.end_of_file = value.QuadPart;
      read_field(record, record_bytes, information_class.allocation_size_offset,
                 value);
      metadata.allocation_size = value.QuadPart;
      read_field(record, record_bytes, information_class.attributes_offset,
                 metadata.attributes);
    }
    if (read_field(record, record_bytes, information_class.ea_size_offset,
                   metadata.ea_size)) {
      metadata.has_ea_size = true;
    }
    if (information_class.file_id_size != 0 &&
        information_class.file_id_size <= metadata.file_id.size() &&
        information_class.file_id_offset <= record_bytes &&
        information_class.file_id_size <=
            record_bytes - information_class.file_id_offset) {
      metadata.has_file_id = true;
      metadata.file_id_size =
          static_cast<std::uint8_t>(information_class.file_id_size);
      std::memcpy(metadata.file_id.data(),
                  record + information_class.file_id_offset,
                  information_class.file_id_size);
    }

    UCHAR short_name_bytes = 0;
    if (read_field(record, record_bytes,
                   information_class.short_name_length_offset,
                   short_name_bytes)) {
      constexpr std::size_t short_name_capacity = 12 * sizeof(wchar_t);
      if (short_name_bytes > short_name_capacity ||
          (short_name_bytes % sizeof(wchar_t)) != 0 ||
          information_class.short_name_offset > record_bytes ||
          short_name_bytes >
              record_bytes - information_class.short_name_offset) {
        failure = std::string(information_class.name) +
                  " returned an invalid short name";
        return false;
      }
      parsed.short_name.assign(
          reinterpret_cast<const wchar_t *>(
              record + information_class.short_name_offset),
          short_name_bytes / sizeof(wchar_t));
    }

    records.push_back(std::move(parsed));
    if (next == 0)
      return true;
    offset += next;
  }
  return true;
}

bool terminal_enumeration_status(LONG status) noexcept {
  return status == STATUS_NO_MORE_FILES || status == STATUS_NO_SUCH_FILE ||
         status == STATUS_OBJECT_NAME_NOT_FOUND;
}

bool unsupported_information_class_status(LONG status) noexcept {
  return status == STATUS_NOT_IMPLEMENTED ||
         status == STATUS_INVALID_INFO_CLASS ||
         status == STATUS_INVALID_PARAMETER || status == STATUS_NOT_SUPPORTED;
}

bool enumerate_pass(HANDLE directory, const directory_class &information_class,
                    std::size_t buffer_bytes, bool return_single,
                    std::wstring_view pattern, bool restart,
                    enumeration_result &result, std::string &failure) {
  const auto query = nt_query_directory_file();
  if (!query) {
    failure = "NtQueryDirectoryFile is unavailable";
    return false;
  }
  if (buffer_bytes > (std::numeric_limits<ULONG>::max)()) {
    failure = "directory test buffer is too large";
    return false;
  }

  std::wstring pattern_storage(pattern);
  native_unicode_string native_pattern{};
  if (!pattern_storage.empty()) {
    native_pattern.buffer = pattern_storage.data();
    native_pattern.length =
        static_cast<USHORT>(pattern_storage.size() * sizeof(wchar_t));
    native_pattern.maximum_length = native_pattern.length;
  }

  bool first = true;
  for (std::size_t attempt = 0; attempt != 4096; ++attempt) {
    std::vector<unsigned char> buffer(buffer_bytes + guard_bytes, 0xA5);
    std::fill_n(buffer.begin(), buffer_bytes, static_cast<unsigned char>(0));
    native_io_status_block io_status{};
    const LONG status =
        query(directory, nullptr, nullptr, nullptr, &io_status, buffer.data(),
              static_cast<ULONG>(buffer_bytes), information_class.value,
              return_single ? TRUE : FALSE,
              first && !pattern_storage.empty() ? &native_pattern : nullptr,
              first && restart ? TRUE : FALSE);
    ++result.calls;

    if (!std::all_of(buffer.begin() + buffer_bytes, buffer.end(),
                     [](unsigned char value) { return value == 0xA5; })) {
      failure = std::string(information_class.name) +
                " wrote beyond the supplied directory buffer";
      return false;
    }

    const std::size_t returned =
        (std::min)(static_cast<std::size_t>(io_status.information),
                   buffer_bytes);
    if (returned != 0 &&
        !parse_directory_buffer(information_class, buffer.data(), returned,
                                result.records, failure)) {
      return false;
    }

    if (terminal_enumeration_status(status))
      return true;
    if (status < 0 && status != STATUS_BUFFER_OVERFLOW) {
      failure = std::string(information_class.name) + " query failed with " +
                status_text(status);
      return false;
    }
    if (returned == 0) {
      failure = std::string(information_class.name) +
                " made no progress during enumeration";
      return false;
    }
    first = false;
  }

  failure =
      std::string(information_class.name) + " exceeded the query call limit";
  return false;
}

bool enumerate_directory(const fs::path &path,
                         const directory_class &information_class,
                         std::size_t buffer_bytes, bool return_single,
                         std::wstring_view pattern, enumeration_result &result,
                         std::string &failure) {
  directory_handle directory(path);
  if (!directory.valid()) {
    failure = "failed to open directory " + path.string() + ": " +
              std::to_string(GetLastError());
    return false;
  }
  return enumerate_pass(directory.get(), information_class, buffer_bytes,
                        return_single, pattern, true, result, failure);
}

bool probe_directory_class(const fs::path &path,
                           const directory_class &information_class,
                           bool &supported, std::string &failure) {
  directory_handle directory(path);
  if (!directory.valid()) {
    failure = "failed to open directory for information-class probe";
    return false;
  }
  const auto query = nt_query_directory_file();
  if (!query) {
    failure = "NtQueryDirectoryFile is unavailable";
    return false;
  }

  std::array<unsigned char, 4096> buffer{};
  native_io_status_block io_status{};
  const LONG status =
      query(directory.get(), nullptr, nullptr, nullptr, &io_status,
            buffer.data(), static_cast<ULONG>(buffer.size()),
            information_class.value, TRUE, nullptr, TRUE);
  if (unsupported_information_class_status(status)) {
    supported = false;
    return true;
  }
  if (status < 0 && status != STATUS_BUFFER_OVERFLOW &&
      !terminal_enumeration_status(status)) {
    failure = std::string(information_class.name) +
              " probe failed with " + status_text(status);
    return false;
  }
  supported = true;
  return true;
}

bool enumerate_twice_with_restart(const fs::path &path,
                                  const directory_class &information_class,
                                  std::size_t buffer_bytes, bool return_single,
                                  std::wstring_view pattern,
                                  enumeration_result &first,
                                  enumeration_result &second,
                                  std::string &failure) {
  directory_handle directory(path);
  if (!directory.valid()) {
    failure = "failed to open restart-test directory " + path.string();
    return false;
  }
  return enumerate_pass(directory.get(), information_class, buffer_bytes,
                        return_single, pattern, true, first, failure) &&
         enumerate_pass(directory.get(), information_class, buffer_bytes,
                        return_single, pattern, true, second, failure);
}

bool equal_name(std::wstring_view left, std::wstring_view right) {
  return _wcsicmp(std::wstring(left).c_str(), std::wstring(right).c_str()) == 0;
}

std::size_t count_name(const enumeration_result &result,
                       std::wstring_view name) {
  return static_cast<std::size_t>(
      std::count_if(result.records.begin(), result.records.end(),
                    [&](const directory_record &record) {
                      return equal_name(record.name, name);
                    }));
}

const directory_record *find_name(const enumeration_result &result,
                                  std::wstring_view name) {
  const auto found = std::find_if(result.records.begin(), result.records.end(),
                                  [&](const directory_record &record) {
                                    return equal_name(record.name, name);
                                  });
  return found == result.records.end() ? nullptr : &*found;
}

bool expect_name_count(const enumeration_result &result, std::wstring_view name,
                       std::size_t expected, std::string_view scenario,
                       std::string &failure) {
  const std::size_t actual = count_name(result, name);
  if (actual == expected)
    return true;
  failure = std::string(scenario) + " expected " + std::to_string(expected) +
            " occurrence(s) of " + narrow_ascii(name) + ", observed " +
            std::to_string(actual);
  return false;
}

bool create_directory(const fs::path &path, std::string &failure) {
  std::error_code error;
  if (fs::create_directory(path, error) || (!error && fs::is_directory(path)))
    return true;
  failure =
      "failed to create directory " + path.string() + ": " + error.message();
  return false;
}

bool remove_directory(const fs::path &path, std::string &failure) {
  std::error_code error;
  if (!fs::exists(path, error))
    return !error;
  if (fs::remove(path, error))
    return true;
  failure =
      "failed to remove directory " + path.string() + ": " + error.message();
  return false;
}

bool metadata_equal(const directory_record_metadata &expected,
                    const directory_record_metadata &actual) noexcept {
  if (expected.has_common_metadata != actual.has_common_metadata ||
      expected.has_ea_size != actual.has_ea_size ||
      expected.has_file_id != actual.has_file_id)
    return false;
  if (expected.has_common_metadata &&
      (expected.creation_time != actual.creation_time ||
       expected.last_write_time != actual.last_write_time ||
       expected.change_time != actual.change_time ||
       expected.end_of_file != actual.end_of_file ||
       expected.allocation_size != actual.allocation_size ||
       expected.attributes != actual.attributes))
    return false;
  if (expected.has_ea_size && expected.ea_size != actual.ea_size)
    return false;
  return !expected.has_file_id ||
         (expected.file_id_size == actual.file_id_size &&
          expected.file_id == actual.file_id);
}

std::string metadata_difference(const directory_record_metadata &expected,
                                const directory_record_metadata &actual) {
#define REPORT_METADATA_DIFFERENCE(field)                                      \
  if (expected.field != actual.field) {                                        \
    return #field " expected " + std::to_string(expected.field) +              \
           ", observed " + std::to_string(actual.field);                       \
  }

  REPORT_METADATA_DIFFERENCE(has_common_metadata)
  REPORT_METADATA_DIFFERENCE(has_ea_size)
  REPORT_METADATA_DIFFERENCE(has_file_id)
  if (expected.has_common_metadata) {
    REPORT_METADATA_DIFFERENCE(creation_time)
    REPORT_METADATA_DIFFERENCE(last_write_time)
    REPORT_METADATA_DIFFERENCE(change_time)
    REPORT_METADATA_DIFFERENCE(end_of_file)
    REPORT_METADATA_DIFFERENCE(allocation_size)
    REPORT_METADATA_DIFFERENCE(attributes)
  }
  if (expected.has_ea_size)
    REPORT_METADATA_DIFFERENCE(ea_size)
  if (expected.has_file_id && expected.file_id != actual.file_id)
    return "file_id differs";
#undef REPORT_METADATA_DIFFERENCE
  return "unknown metadata mismatch";
}

bool capture_physical_metadata(
    const fs::path &store_parent,
    const std::array<bool, directory_classes.size()> &supported,
    std::array<directory_record_metadata, directory_classes.size()> &snapshot,
    std::string &failure) {
  for (std::size_t index = 0; index != directory_classes.size(); ++index) {
    if (!supported[index])
      continue;

    enumeration_result result;
    if (!enumerate_directory(store_parent, directory_classes[index], 4096, true,
                             real_mapping_final_component, result, failure))
      return false;
    const auto *record = find_name(result, real_mapping_final_component);
    if (!record || count_name(result, real_mapping_final_component) != 1) {
      failure = std::string(directory_classes[index].name) +
                " could not capture physical real metadata";
      return false;
    }
    const bool zero_file_id =
        record->metadata.has_file_id &&
        std::all_of(record->metadata.file_id.begin(),
                    record->metadata.file_id.begin() +
                        record->metadata.file_id_size,
                    [](std::uint8_t value) { return value == 0; });
    if (zero_file_id) {
      failure = std::string(directory_classes[index].name) +
                " returned a zero physical file ID";
      return false;
    }
    snapshot[index] = record->metadata;
  }
  return true;
}

bool configure_position_siblings(const fs::path &store_parent,
                                 bool include_before, bool include_after,
                                 std::string &failure) {
  const fs::path before = store_parent / L"rea-before";
  const fs::path after = store_parent / L"real-z-after";
  if (!remove_directory(before, failure) || !remove_directory(after, failure))
    return false;
  return (!include_before || create_directory(before, failure)) &&
         (!include_after || create_directory(after, failure));
}

bool verify_physical_position(const fs::path &store_parent, bool include_before,
                              bool include_after,
                              std::size_t expected_real_index,
                              std::string &failure) {
  if (!configure_position_siblings(store_parent, include_before, include_after,
                                   failure))
    return false;

  enumeration_result result;
  if (!enumerate_directory(store_parent, directory_classes[3], 4096, false,
                           L"rea*", result, failure))
    return false;
  const auto found = std::find_if(
      result.records.begin(), result.records.end(),
      [](const directory_record &record) {
        return equal_name(record.name, real_mapping_final_component);
      });
  if (found == result.records.end() ||
      static_cast<std::size_t>(found - result.records.begin()) !=
          expected_real_index) {
    failure = "the physical NTFS ordering did not place real at the expected "
              "buffer position";
    return false;
  }
  return true;
}

bool verify_filtered_position(const fs::path &store_parent, bool include_before,
                              bool include_after, std::string &failure) {
  if (!configure_position_siblings(store_parent, include_before, include_after,
                                   failure))
    return false;

  enumeration_result result;
  if (!enumerate_directory(store_parent, directory_classes[3], 256, true,
                           L"rea*", result, failure))
    return false;
  if (!expect_name_count(result, real_mapping_final_component, 0,
                         "single-entry real suppression", failure))
    return false;
  if (!expect_name_count(result, L"rea-before", include_before ? 1 : 0,
                         "single-entry preceding sibling", failure))
    return false;
  return expect_name_count(result, L"real-z-after", include_after ? 1 : 0,
                           "single-entry following sibling", failure);
}

} // namespace

bool prepare_directory_enumeration_tests(
    const fs::path &visible_parent, const fs::path &store_parent,
    directory_enumeration_baseline &baseline, std::string &failure) {
  if (!nt_query_directory_file()) {
    failure = "NtQueryDirectoryFile is unavailable";
    return false;
  }

  for (const wchar_t *name : {L"ga-visible", L"x-visible"}) {
    if (!create_directory(visible_parent / name, failure))
      return false;
  }
  for (int index = 0; index != 20; ++index) {
    std::wostringstream name;
    name << L"visible-page-" << std::setw(2) << std::setfill(L'0') << index;
    if (!create_directory(visible_parent / name.str(), failure))
      return false;
  }
  for (int index = 0; index != 20; ++index) {
    std::wostringstream name;
    name << L"store-page-" << std::setw(2) << std::setfill(L'0') << index;
    if (!create_directory(store_parent / name.str(), failure))
      return false;
  }

  for (std::size_t index = 0; index != directory_classes.size(); ++index) {
    if (!probe_directory_class(store_parent, directory_classes[index],
                               baseline.supported[index], failure))
      return false;
  }

  if (!verify_physical_position(store_parent, false, true, 0, failure) ||
      !verify_physical_position(store_parent, true, false, 1, failure) ||
      !verify_physical_position(store_parent, true, true, 1, failure) ||
      !configure_position_siblings(store_parent, true, true, failure))
    return false;

  // NTFS and ReFS may finalize a newly populated directory's timestamps after
  // the mutating handle has closed or after its first enumeration. Do not let
  // that filesystem timing become a false NameChanger metadata failure.
  constexpr DWORD stabilization_delay_ms = 25;
  constexpr std::size_t stabilization_attempts = 10;
  std::array<directory_record_metadata, directory_classes.size()> previous{};
  if (!capture_physical_metadata(store_parent, baseline.supported, previous,
                                 failure))
    return false;

  for (std::size_t attempt = 0; attempt != stabilization_attempts; ++attempt) {
    Sleep(stabilization_delay_ms);
    std::array<directory_record_metadata, directory_classes.size()> current{};
    if (!capture_physical_metadata(store_parent, baseline.supported, current,
                                   failure))
      return false;

    bool stable = true;
    for (std::size_t index = 0; index != directory_classes.size(); ++index) {
      if (baseline.supported[index] &&
          !metadata_equal(previous[index], current[index])) {
        stable = false;
        break;
      }
    }
    if (stable) {
      baseline.real_mapping_metadata = current;
      return true;
    }
    previous = current;
  }

  failure = "physical real metadata did not stabilize";
  return false;
}

bool run_directory_enumeration_tests(
    const fs::path &visible_parent, const fs::path &visible_mapping,
    const fs::path &store_parent,
    const directory_enumeration_baseline &baseline, std::string &failure) {
  for (std::size_t index = 0; index != directory_classes.size(); ++index) {
    const auto &information_class = directory_classes[index];
    if (!baseline.supported[index])
      continue;

    enumeration_result visible;
    if (!enumerate_directory(visible_parent, information_class, 65536, false,
                             L"*", visible, failure) ||
        !expect_name_count(visible, user_mapping_final_component, 1,
                           information_class.name, failure))
      return false;

    enumeration_result backing;
    if (!enumerate_directory(store_parent, information_class, 65536, false,
                             L"*", backing, failure) ||
        !expect_name_count(backing, real_mapping_final_component, 0,
                           information_class.name, failure))
      return false;

    enumeration_result mapped_children;
    if (!enumerate_directory(visible_mapping, information_class, 65536, false,
                             L"*", mapped_children, failure) ||
        !expect_name_count(mapped_children, payload_name, 1,
                           "mapped child enumeration", failure))
      return false;

    enumeration_result injected;
    if (!enumerate_directory(visible_parent, information_class, 4096, true,
                             user_mapping_final_component, injected, failure))
      return false;
    const auto *graft = find_name(injected, user_mapping_final_component);
    if (!graft || count_name(injected, user_mapping_final_component) != 1) {
      failure = std::string(information_class.name) +
                " did not return one exact graft entry";
      return false;
    }
    if (!metadata_equal(baseline.real_mapping_metadata[index],
                        graft->metadata)) {
      failure = std::string(information_class.name) +
                " did not preserve backing metadata on the graft: " +
                metadata_difference(baseline.real_mapping_metadata[index],
                                    graft->metadata);
      return false;
    }
    if (!graft->short_name.empty()) {
      failure = std::string(information_class.name) +
                " leaked the backing short name";
      return false;
    }

    enumeration_result hidden_exact;
    if (!enumerate_directory(store_parent, information_class, 4096, true,
                             real_mapping_final_component, hidden_exact,
                             failure) ||
        !expect_name_count(hidden_exact, real_mapping_final_component, 0,
                           "exact backing suppression", failure))
      return false;
  }

  for (const wchar_t *pattern : {L"g*", L"?raft", L"*aft"}) {
    enumeration_result wildcard;
    if (!enumerate_directory(visible_parent, directory_classes[3], 256, true,
                             pattern, wildcard, failure) ||
        !expect_name_count(wildcard, user_mapping_final_component, 1,
                           "wildcard graft injection", failure))
      return false;
  }

  enumeration_result nonmatching;
  if (!enumerate_directory(visible_parent, directory_classes[3], 256, true,
                           L"x*", nonmatching, failure) ||
      !expect_name_count(nonmatching, L"x-visible", 1,
                         "nonmatching physical entry", failure) ||
      !expect_name_count(nonmatching, user_mapping_final_component, 0,
                         "cached nonmatching pattern", failure))
    return false;

  enumeration_result paged_visible;
  if (!enumerate_directory(visible_parent, directory_classes[4], 192, false,
                           L"*", paged_visible, failure) ||
      paged_visible.calls < 2 ||
      !expect_name_count(paged_visible, user_mapping_final_component, 1,
                         "small-buffer graft injection", failure)) {
    if (failure.empty())
      failure = "small-buffer visible enumeration did not paginate";
    return false;
  }

  enumeration_result paged_backing;
  if (!enumerate_directory(store_parent, directory_classes[4], 192, false, L"*",
                           paged_backing, failure) ||
      paged_backing.calls < 2 ||
      !expect_name_count(paged_backing, real_mapping_final_component, 0,
                         "small-buffer backing suppression", failure)) {
    if (failure.empty())
      failure = "small-buffer backing enumeration did not paginate";
    return false;
  }

  enumeration_result single_visible;
  if (!enumerate_directory(visible_parent, directory_classes[4], 512, true,
                           L"*", single_visible, failure) ||
      single_visible.calls <= single_visible.records.size() ||
      !expect_name_count(single_visible, user_mapping_final_component, 1,
                         "single-entry graft injection", failure)) {
    if (failure.empty())
      failure = "single-entry visible enumeration did not terminate correctly";
    return false;
  }

  enumeration_result first_restart;
  enumeration_result second_restart;
  if (!enumerate_twice_with_restart(visible_parent, directory_classes[3], 256,
                                    true, L"g*", first_restart, second_restart,
                                    failure) ||
      !expect_name_count(first_restart, user_mapping_final_component, 1,
                         "first restart pass", failure) ||
      !expect_name_count(second_restart, user_mapping_final_component, 1,
                         "second restart pass", failure))
    return false;

  if (!verify_filtered_position(store_parent, false, true, failure) ||
      !verify_filtered_position(store_parent, true, false, failure) ||
      !verify_filtered_position(store_parent, true, true, failure))
    return false;
  return configure_position_siblings(store_parent, true, true, failure);
}

} // namespace crtsys_flt_name_changer_runtime_test
