// clang-format off

#include <ntifs.h>
#include <aux_klib.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

extern "C" {
using _Stacktrace_string_fill_callback =
    size_t(__stdcall *)(char *Data, size_t Size, void *Context) noexcept;

using _Stacktrace_string_fill =
    size_t(__stdcall *)(size_t Size, void *String, void *Context,
                        _Stacktrace_string_fill_callback Callback);
} // extern "C"

namespace {
constexpr size_t stacktrace_address_text_size = 2048;
constexpr size_t stacktrace_max_pdb_size = 128ull * 1024ull * 1024ull;

struct stacktrace_text {
  const char *data;
  size_t size;
};

struct stacktrace_pe_section {
  std::uint32_t rva;
  std::uint32_t size;
};

struct stacktrace_pdb_symbol {
  std::uint32_t rva;
  std::uint32_t length;
  std::string name;
};

struct stacktrace_pdb_line {
  std::uint32_t rva;
  std::uint32_t end_rva;
  std::uint32_t line;
  std::string file;
};

struct stacktrace_pdb_identity {
  std::uint8_t guid[16];
  std::uint32_t age;
};

struct stacktrace_pdb_stream_ref {
  std::uint32_t size;
  std::vector<std::uint32_t> blocks;
};

struct stacktrace_pdb_module_info {
  std::uint16_t stream_index;
  std::uint32_t symbol_bytes;
  std::uint32_t c11_bytes;
  std::uint32_t c13_bytes;
};

struct stacktrace_pdb_file_checksum {
  std::uint32_t checksum_offset;
  std::string file;
};

struct stacktrace_pdb_cache {
  std::uintptr_t module_base{};
  bool attempted{};
  bool loaded{};
  std::vector<stacktrace_pdb_symbol> symbols;
  std::vector<stacktrace_pdb_line> lines;
};

struct stacktrace_format_context {
  std::vector<AUX_MODULE_EXTENDED_INFO> modules;
  std::vector<stacktrace_pdb_cache> pdb_caches;
  bool have_modules{};
};

size_t stacktrace_copy_text(char *data, size_t size, void *context) noexcept {
  const auto *text = static_cast<const stacktrace_text *>(context);
  const size_t bytes_to_copy = (std::min)(size, text->size);
  if (bytes_to_copy != 0) {
    std::memcpy(data, text->data, bytes_to_copy);
  }
  return bytes_to_copy;
}

void stacktrace_fill_text(void *string, _Stacktrace_string_fill fill,
                          const char *data, size_t size) {
  if (fill == nullptr) {
    return;
  }

  stacktrace_text text{data, size};
  fill(size, string, &text, stacktrace_copy_text);
}

void stacktrace_fill_cstr(void *string, _Stacktrace_string_fill fill,
                          const char *data) {
  stacktrace_fill_text(string, fill, data, std::strlen(data));
}

size_t stacktrace_bounded_strlen(const char *data, size_t max_size) noexcept {
  size_t size = 0;
  while (size != max_size && data[size] != '\0') {
    ++size;
  }
  return size;
}

const char *stacktrace_module_name(const AUX_MODULE_EXTENDED_INFO &module,
                                   size_t *name_size) noexcept {
  const auto *full_path =
      reinterpret_cast<const char *>(module.FullPathName);
  size_t offset = module.FileNameOffset;

  if (offset >= AUX_KLIB_MODULE_PATH_LEN || full_path[offset] == '\0') {
    offset = 0;
    const size_t full_path_size =
        stacktrace_bounded_strlen(full_path, AUX_KLIB_MODULE_PATH_LEN);
    for (size_t index = 0; index != full_path_size; ++index) {
      if (full_path[index] == '\\' || full_path[index] == '/') {
        offset = index + 1;
      }
    }
  }

  if (offset >= AUX_KLIB_MODULE_PATH_LEN || full_path[offset] == '\0') {
    return nullptr;
  }

  *name_size = stacktrace_bounded_strlen(
      full_path + offset, AUX_KLIB_MODULE_PATH_LEN - offset);
  return *name_size != 0 ? full_path + offset : nullptr;
}

const char *stacktrace_module_path(const AUX_MODULE_EXTENDED_INFO &module,
                                   size_t *path_size) noexcept {
  const auto *full_path = reinterpret_cast<const char *>(module.FullPathName);
  *path_size = stacktrace_bounded_strlen(full_path, AUX_KLIB_MODULE_PATH_LEN);
  return *path_size != 0 ? full_path : nullptr;
}

bool stacktrace_query_modules(
    std::vector<AUX_MODULE_EXTENDED_INFO> &modules) {
  modules.clear();

  if (KeGetCurrentIrql() != PASSIVE_LEVEL) {
    return false;
  }

  if (!NT_SUCCESS(AuxKlibInitialize())) {
    return false;
  }

  ULONG bytes = 0;
  NTSTATUS status = AuxKlibQueryModuleInformation(
      &bytes, sizeof(AUX_MODULE_EXTENDED_INFO), nullptr);
  if (bytes == 0 ||
      (!NT_SUCCESS(status) && status != STATUS_BUFFER_TOO_SMALL &&
       status != STATUS_BUFFER_OVERFLOW &&
       status != STATUS_INFO_LENGTH_MISMATCH)) {
    return false;
  }

  modules.resize(bytes / sizeof(AUX_MODULE_EXTENDED_INFO) + 1);
  bytes = static_cast<ULONG>(modules.size() * sizeof(AUX_MODULE_EXTENDED_INFO));
  status = AuxKlibQueryModuleInformation(
      &bytes, sizeof(AUX_MODULE_EXTENDED_INFO), modules.data());
  if (!NT_SUCCESS(status) || bytes == 0) {
    modules.clear();
    return false;
  }

  modules.resize(bytes / sizeof(AUX_MODULE_EXTENDED_INFO));
  return !modules.empty();
}

bool stacktrace_is_absolute_dos_path(const std::string &path) {
  return path.size() >= 3 && path[1] == ':' &&
         ((path[0] >= 'A' && path[0] <= 'Z') ||
          (path[0] >= 'a' && path[0] <= 'z')) &&
         (path[2] == '\\' || path[2] == '/');
}

bool stacktrace_starts_with(const std::string &value, const char *prefix) {
  const size_t prefix_size = std::strlen(prefix);
  return value.size() >= prefix_size &&
         _strnicmp(value.data(), prefix, prefix_size) == 0;
}

std::string stacktrace_path_basename(const std::string &path) {
  const size_t slash = path.find_last_of("\\/");
  if (slash == std::string::npos) {
    return path;
  }
  return path.substr(slash + 1);
}

std::string stacktrace_path_directory(const std::string &path) {
  const size_t slash = path.find_last_of("\\/");
  if (slash == std::string::npos) {
    return {};
  }
  return path.substr(0, slash + 1);
}

std::string stacktrace_replace_extension(const std::string &path,
                                         const char *extension) {
  const size_t slash = path.find_last_of("\\/");
  const size_t dot = path.find_last_of('.');
  if (dot == std::string::npos ||
      (slash != std::string::npos && dot < slash)) {
    return path + extension;
  }
  return path.substr(0, dot) + extension;
}

std::wstring stacktrace_to_nt_path(const std::string &path) {
  std::string normalized = path;
  std::replace(normalized.begin(), normalized.end(), '/', '\\');

  if (stacktrace_starts_with(normalized, "\\\\?\\")) {
    normalized.replace(0, 4, "\\??\\");
  } else if (stacktrace_is_absolute_dos_path(normalized)) {
    normalized.insert(0, "\\??\\");
  }

  std::wstring wide;
  wide.reserve(normalized.size());
  for (const char ch : normalized) {
    wide.push_back(static_cast<unsigned char>(ch));
  }
  return wide;
}

bool stacktrace_read_file(const std::string &path,
                          std::vector<std::uint8_t> &contents) {
  contents.clear();

  if (KeGetCurrentIrql() != PASSIVE_LEVEL) {
    return false;
  }

  const std::wstring nt_path = stacktrace_to_nt_path(path);
  if (nt_path.empty()) {
    return false;
  }

  UNICODE_STRING name{};
  RtlInitUnicodeString(&name, nt_path.c_str());

  OBJECT_ATTRIBUTES attributes{};
  InitializeObjectAttributes(&attributes, &name,
                             OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, nullptr,
                             nullptr);

  IO_STATUS_BLOCK io_status{};
  HANDLE file{};
  NTSTATUS status = ZwCreateFile(
      &file, GENERIC_READ | SYNCHRONIZE, &attributes, &io_status, nullptr,
      FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ | FILE_SHARE_DELETE, FILE_OPEN,
      FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT, nullptr, 0);
  if (!NT_SUCCESS(status)) {
    return false;
  }

  FILE_STANDARD_INFORMATION standard_info{};
  status = ZwQueryInformationFile(file, &io_status, &standard_info,
                                  sizeof(standard_info),
                                  FileStandardInformation);
  if (!NT_SUCCESS(status) || standard_info.EndOfFile.QuadPart <= 0 ||
      static_cast<unsigned long long>(standard_info.EndOfFile.QuadPart) >
          stacktrace_max_pdb_size) {
    ZwClose(file);
    return false;
  }

  contents.resize(static_cast<size_t>(standard_info.EndOfFile.QuadPart));
  LARGE_INTEGER offset{};
  status = ZwReadFile(file, nullptr, nullptr, nullptr, &io_status,
                      contents.data(), static_cast<ULONG>(contents.size()),
                      &offset, nullptr);
  ZwClose(file);

  if (!NT_SUCCESS(status) || io_status.Information != contents.size()) {
    contents.clear();
    return false;
  }

  return true;
}

bool stacktrace_get_pe_sections(const void *image_base,
                                std::vector<stacktrace_pe_section> &sections) {
  sections.clear();

  const auto *dos = static_cast<const IMAGE_DOS_HEADER *>(image_base);
  if (dos == nullptr || dos->e_magic != IMAGE_DOS_SIGNATURE ||
      dos->e_lfanew <= 0) {
    return false;
  }

  const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(
      static_cast<const std::uint8_t *>(image_base) + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE ||
      nt->FileHeader.NumberOfSections == 0) {
    return false;
  }

  const auto *section = IMAGE_FIRST_SECTION(nt);
  sections.reserve(nt->FileHeader.NumberOfSections);
  for (std::uint16_t index = 0; index != nt->FileHeader.NumberOfSections;
       ++index) {
    const std::uint32_t size =
        (std::max)(section[index].Misc.VirtualSize,
                   section[index].SizeOfRawData);
    if (section[index].VirtualAddress != 0 && size != 0) {
      sections.push_back({section[index].VirtualAddress, size});
    }
  }

  return !sections.empty();
}

bool stacktrace_get_rsds_pdb_name(const void *image_base, std::string &pdb_name,
                                  stacktrace_pdb_identity &identity) {
  pdb_name.clear();
  std::memset(&identity, 0, sizeof(identity));

  const auto *dos = static_cast<const IMAGE_DOS_HEADER *>(image_base);
  if (dos == nullptr || dos->e_magic != IMAGE_DOS_SIGNATURE ||
      dos->e_lfanew <= 0) {
    return false;
  }

  const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(
      static_cast<const std::uint8_t *>(image_base) + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE) {
    return false;
  }

  const auto &directory =
      nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
  if (directory.VirtualAddress == 0 ||
      directory.Size < sizeof(IMAGE_DEBUG_DIRECTORY)) {
    return false;
  }

  const auto *debug_directory = reinterpret_cast<const IMAGE_DEBUG_DIRECTORY *>(
      static_cast<const std::uint8_t *>(image_base) + directory.VirtualAddress);
  const size_t entry_count = directory.Size / sizeof(IMAGE_DEBUG_DIRECTORY);

  for (size_t index = 0; index != entry_count; ++index) {
    const auto &entry = debug_directory[index];
    if (entry.Type != IMAGE_DEBUG_TYPE_CODEVIEW ||
        entry.SizeOfData <= 24 || entry.AddressOfRawData == 0) {
      continue;
    }

    const auto *record = static_cast<const char *>(image_base) +
                         entry.AddressOfRawData;
    if (std::memcmp(record, "RSDS", 4) != 0) {
      continue;
    }

    std::memcpy(identity.guid, record + 4, sizeof(identity.guid));
    std::memcpy(&identity.age, record + 20, sizeof(identity.age));

    const char *name = record + 24;
    const size_t max_name = entry.SizeOfData - 24;
    const size_t name_size = stacktrace_bounded_strlen(name, max_name);
    if (name_size == 0 || name_size == max_name) {
      continue;
    }

    pdb_name.assign(name, name_size);
    return true;
  }

  return false;
}

template <class T>
bool stacktrace_read_unaligned(const std::uint8_t *data, size_t size,
                               size_t offset, T *value) {
  if (offset > size || sizeof(T) > size - offset) {
    return false;
  }
  std::memcpy(value, data + offset, sizeof(T));
  return true;
}

bool stacktrace_msf_read_stream_bytes(
    const std::vector<std::uint8_t> &pdb, std::uint32_t block_size,
    const std::vector<std::uint32_t> &blocks, size_t stream_size,
    size_t stream_offset, void *buffer, size_t bytes) {
  if (stream_offset > stream_size || bytes > stream_size - stream_offset) {
    return false;
  }

  auto *output = static_cast<std::uint8_t *>(buffer);
  size_t remaining = bytes;
  size_t output_offset = 0;
  size_t current_offset = stream_offset;

  while (remaining != 0) {
    const size_t block_index = current_offset / block_size;
    const size_t offset_in_block = current_offset % block_size;
    if (block_index >= blocks.size()) {
      return false;
    }

    const size_t pdb_offset =
        static_cast<size_t>(blocks[block_index]) * block_size + offset_in_block;
    if (pdb_offset >= pdb.size()) {
      return false;
    }

    const size_t bytes_in_block =
        (std::min)(remaining, static_cast<size_t>(block_size) - offset_in_block);
    if (bytes_in_block > pdb.size() - pdb_offset) {
      return false;
    }

    std::memcpy(output + output_offset, pdb.data() + pdb_offset,
                bytes_in_block);
    output_offset += bytes_in_block;
    current_offset += bytes_in_block;
    remaining -= bytes_in_block;
  }

  return true;
}

bool stacktrace_msf_copy_stream(const std::vector<std::uint8_t> &pdb,
                                std::uint32_t block_size,
                                const std::vector<std::uint32_t> &blocks,
                                size_t stream_size,
                                std::vector<std::uint8_t> &stream) {
  stream.resize(stream_size);
  if (stream_size == 0) {
    return true;
  }
  return stacktrace_msf_read_stream_bytes(pdb, block_size, blocks, stream_size,
                                          0, stream.data(), stream_size);
}

size_t stacktrace_align4(size_t value) {
  return (value + 3) & ~static_cast<size_t>(3);
}

bool stacktrace_pdb_read_stream_directory(
    const std::vector<std::uint8_t> &pdb, std::uint32_t &block_size,
    std::vector<stacktrace_pdb_stream_ref> &streams) {
  block_size = 0;
  streams.clear();

  static constexpr char msf70_magic[] =
      "Microsoft C/C++ MSF 7.00\r\n\x1a""DS\0\0\0";
  if (pdb.size() < 56 ||
      std::memcmp(pdb.data(), msf70_magic, sizeof(msf70_magic) - 1) != 0) {
    return false;
  }

  std::uint32_t num_blocks = 0;
  std::uint32_t directory_bytes = 0;
  std::uint32_t block_map_block = 0;
  if (!stacktrace_read_unaligned(pdb.data(), pdb.size(), 32, &block_size) ||
      !stacktrace_read_unaligned(pdb.data(), pdb.size(), 40, &num_blocks) ||
      !stacktrace_read_unaligned(pdb.data(), pdb.size(), 44,
                                 &directory_bytes) ||
      !stacktrace_read_unaligned(pdb.data(), pdb.size(), 52,
                                 &block_map_block)) {
    return false;
  }

  if (block_size < 512 || block_size > 8192 || num_blocks == 0 ||
      directory_bytes == 0 ||
      static_cast<unsigned long long>(block_size) * num_blocks > pdb.size()) {
    return false;
  }

  const size_t directory_block_count =
      (directory_bytes + block_size - 1ull) / block_size;
  const size_t block_map_offset =
      static_cast<size_t>(block_map_block) * block_size;
  if (block_map_offset >= pdb.size() ||
      directory_block_count * sizeof(std::uint32_t) >
          pdb.size() - block_map_offset) {
    return false;
  }

  std::vector<std::uint32_t> directory_blocks(directory_block_count);
  std::memcpy(directory_blocks.data(), pdb.data() + block_map_offset,
              directory_blocks.size() * sizeof(std::uint32_t));

  std::vector<std::uint8_t> directory(directory_bytes);
  for (size_t index = 0; index != directory_blocks.size(); ++index) {
    const size_t source_offset =
        static_cast<size_t>(directory_blocks[index]) * block_size;
    if (source_offset >= pdb.size()) {
      return false;
    }

    const size_t bytes =
        (std::min)(static_cast<size_t>(block_size),
                   directory.size() - index * static_cast<size_t>(block_size));
    if (bytes > pdb.size() - source_offset) {
      return false;
    }

    std::memcpy(directory.data() + index * static_cast<size_t>(block_size),
                pdb.data() + source_offset, bytes);
  }

  size_t offset = 0;
  std::uint32_t stream_count = 0;
  if (!stacktrace_read_unaligned(directory.data(), directory.size(), offset,
                                 &stream_count)) {
    return false;
  }
  offset += sizeof(std::uint32_t);

  if (stream_count == 0 || stream_count > 4096 ||
      stream_count * sizeof(std::uint32_t) > directory.size() - offset) {
    return false;
  }

  std::vector<std::uint32_t> stream_sizes(stream_count);
  std::memcpy(stream_sizes.data(), directory.data() + offset,
              stream_sizes.size() * sizeof(std::uint32_t));
  offset += stream_sizes.size() * sizeof(std::uint32_t);

  streams.reserve(stream_count);
  for (std::uint32_t stream_index = 0; stream_index != stream_count;
       ++stream_index) {
    const std::uint32_t stream_size = stream_sizes[stream_index];
    if (stream_size == 0 || stream_size == 0xffffffffu) {
      streams.push_back({stream_size, {}});
      continue;
    }

    const size_t block_count = (stream_size + block_size - 1ull) / block_size;
    if (block_count * sizeof(std::uint32_t) > directory.size() - offset) {
      return false;
    }

    std::vector<std::uint32_t> blocks(block_count);
    std::memcpy(blocks.data(), directory.data() + offset,
                blocks.size() * sizeof(std::uint32_t));
    offset += blocks.size() * sizeof(std::uint32_t);

    streams.push_back({stream_size, std::move(blocks)});
  }

  return streams.size() == stream_count;
}

bool stacktrace_pdb_copy_stream(
    const std::vector<std::uint8_t> &pdb, std::uint32_t block_size,
    const std::vector<stacktrace_pdb_stream_ref> &streams,
    std::uint32_t stream_index, std::vector<std::uint8_t> &stream) {
  stream.clear();
  if (stream_index >= streams.size()) {
    return false;
  }

  const auto &ref = streams[stream_index];
  if (ref.size == 0 || ref.size == 0xffffffffu) {
    return false;
  }

  return stacktrace_msf_copy_stream(pdb, block_size, ref.blocks, ref.size,
                                    stream);
}

bool stacktrace_pdb_validate_identity(
    const std::vector<std::uint8_t> &pdb, std::uint32_t block_size,
    const std::vector<stacktrace_pdb_stream_ref> &streams,
    const stacktrace_pdb_identity &expected_identity) {
  if (streams.size() <= 1 || streams[1].size < 28) {
    return false;
  }

  std::uint8_t info[28]{};
  if (!stacktrace_msf_read_stream_bytes(pdb, block_size, streams[1].blocks,
                                        streams[1].size, 0, info,
                                        sizeof(info))) {
    return false;
  }

  std::uint32_t age = 0;
  std::memcpy(&age, info + 8, sizeof(age));
  return age == expected_identity.age &&
         std::memcmp(info + 12, expected_identity.guid,
                     sizeof(expected_identity.guid)) == 0;
}

bool stacktrace_pdb_read_cstr(const std::vector<std::uint8_t> &data,
                              size_t offset, std::string &value) {
  value.clear();
  if (offset >= data.size()) {
    return false;
  }

  const char *text = reinterpret_cast<const char *>(data.data() + offset);
  const size_t max_size = data.size() - offset;
  const size_t size = stacktrace_bounded_strlen(text, max_size);
  if (size == max_size) {
    return false;
  }

  value.assign(text, size);
  return true;
}

bool stacktrace_pdb_skip_serialized_bitset(const std::vector<std::uint8_t> &data,
                                           size_t &offset) {
  std::uint32_t word_count = 0;
  if (!stacktrace_read_unaligned(data.data(), data.size(), offset,
                                 &word_count)) {
    return false;
  }
  offset += sizeof(word_count);

  const size_t bytes = static_cast<size_t>(word_count) * sizeof(std::uint32_t);
  if (bytes > data.size() - offset) {
    return false;
  }

  offset += bytes;
  return true;
}

bool stacktrace_pdb_find_named_stream(
    const std::vector<std::uint8_t> &pdb, std::uint32_t block_size,
    const std::vector<stacktrace_pdb_stream_ref> &streams, const char *name,
    std::uint32_t &stream_index) {
  stream_index = 0;

  std::vector<std::uint8_t> pdb_stream;
  if (!stacktrace_pdb_copy_stream(pdb, block_size, streams, 1, pdb_stream) ||
      pdb_stream.size() < 32) {
    return false;
  }

  std::uint32_t string_bytes = 0;
  if (!stacktrace_read_unaligned(pdb_stream.data(), pdb_stream.size(), 28,
                                 &string_bytes) ||
      string_bytes > pdb_stream.size() - 32) {
    return false;
  }

  const size_t strings_offset = 32;
  size_t offset = strings_offset + string_bytes;
  std::uint32_t entry_count = 0;
  std::uint32_t capacity = 0;
  if (!stacktrace_read_unaligned(pdb_stream.data(), pdb_stream.size(), offset,
                                 &entry_count)) {
    return false;
  }
  offset += sizeof(entry_count);

  if (!stacktrace_read_unaligned(pdb_stream.data(), pdb_stream.size(), offset,
                                 &capacity)) {
    return false;
  }
  offset += sizeof(capacity);
  (void)capacity;

  if (entry_count > 4096 ||
      !stacktrace_pdb_skip_serialized_bitset(pdb_stream, offset) ||
      !stacktrace_pdb_skip_serialized_bitset(pdb_stream, offset) ||
      static_cast<size_t>(entry_count) * sizeof(std::uint32_t) * 2 >
          pdb_stream.size() - offset) {
    return false;
  }

  const auto *strings = pdb_stream.data() + strings_offset;
  for (std::uint32_t index = 0; index != entry_count; ++index) {
    std::uint32_t name_offset = 0;
    std::uint32_t value = 0;
    std::memcpy(&name_offset, pdb_stream.data() + offset, sizeof(name_offset));
    offset += sizeof(name_offset);
    std::memcpy(&value, pdb_stream.data() + offset, sizeof(value));
    offset += sizeof(value);

    if (name_offset >= string_bytes) {
      continue;
    }

    const char *entry_name =
        reinterpret_cast<const char *>(strings + name_offset);
    const size_t max_name = string_bytes - name_offset;
    const size_t entry_name_size =
        stacktrace_bounded_strlen(entry_name, max_name);
    if (entry_name_size == max_name) {
      continue;
    }

    if (std::strlen(name) == entry_name_size &&
        std::memcmp(entry_name, name, entry_name_size) == 0) {
      stream_index = value;
      return true;
    }
  }

  return false;
}

bool stacktrace_pdb_read_names(
    const std::vector<std::uint8_t> &pdb, std::uint32_t block_size,
    const std::vector<stacktrace_pdb_stream_ref> &streams,
    std::vector<std::uint8_t> &names) {
  names.clear();

  std::uint32_t names_stream_index = 0;
  if (!stacktrace_pdb_find_named_stream(pdb, block_size, streams, "/names",
                                        names_stream_index)) {
    return false;
  }

  std::vector<std::uint8_t> stream;
  if (!stacktrace_pdb_copy_stream(pdb, block_size, streams, names_stream_index,
                                  stream) ||
      stream.size() < 12) {
    return false;
  }

  std::uint32_t signature = 0;
  std::uint32_t version = 0;
  std::uint32_t string_bytes = 0;
  std::memcpy(&signature, stream.data(), sizeof(signature));
  std::memcpy(&version, stream.data() + 4, sizeof(version));
  std::memcpy(&string_bytes, stream.data() + 8, sizeof(string_bytes));

  if (signature != 0xeffeeffeu || version != 1 ||
      string_bytes > stream.size() - 12) {
    return false;
  }

  names.assign(stream.begin() + 12, stream.begin() + 12 + string_bytes);
  return true;
}

bool stacktrace_pdb_read_modules(const std::vector<std::uint8_t> &dbi,
                                 std::vector<stacktrace_pdb_module_info> &modules) {
  modules.clear();
  if (dbi.size() < 64) {
    return false;
  }

  std::int32_t module_info_size = 0;
  if (!stacktrace_read_unaligned(dbi.data(), dbi.size(), 24,
                                 &module_info_size) ||
      module_info_size <= 0 ||
      static_cast<size_t>(module_info_size) > dbi.size() - 64) {
    return false;
  }

  size_t offset = 64;
  const size_t end = 64 + static_cast<size_t>(module_info_size);
  while (offset + 64 <= end) {
    std::uint16_t stream_index = 0;
    std::uint32_t symbol_bytes = 0;
    std::uint32_t c11_bytes = 0;
    std::uint32_t c13_bytes = 0;
    std::memcpy(&stream_index, dbi.data() + offset + 34,
                sizeof(stream_index));
    std::memcpy(&symbol_bytes, dbi.data() + offset + 36,
                sizeof(symbol_bytes));
    std::memcpy(&c11_bytes, dbi.data() + offset + 40, sizeof(c11_bytes));
    std::memcpy(&c13_bytes, dbi.data() + offset + 44, sizeof(c13_bytes));
    modules.push_back({stream_index, symbol_bytes, c11_bytes, c13_bytes});

    const size_t names_offset = offset + 64;
    const void *first_end = std::memchr(dbi.data() + names_offset, '\0',
                                        end - names_offset);
    if (first_end == nullptr) {
      break;
    }

    const size_t second_start =
        static_cast<const std::uint8_t *>(first_end) - dbi.data() + 1;
    if (second_start >= end) {
      break;
    }

    const void *second_end = std::memchr(dbi.data() + second_start, '\0',
                                         end - second_start);
    if (second_end == nullptr) {
      break;
    }

    offset = stacktrace_align4(
        static_cast<const std::uint8_t *>(second_end) - dbi.data() + 1);
  }

  return !modules.empty();
}

void stacktrace_pdb_parse_symbol_records(
    const std::vector<std::uint8_t> &stream,
    const std::vector<stacktrace_pe_section> &sections, size_t symbol_bytes,
    std::vector<stacktrace_pdb_symbol> &symbols) {
  if (symbol_bytes > stream.size()) {
    symbol_bytes = stream.size();
  }

  if (symbol_bytes < sizeof(std::uint32_t)) {
    return;
  }

  std::uint32_t signature = 0;
  std::memcpy(&signature, stream.data(), sizeof(signature));
  if (signature != 4) {
    return;
  }

  size_t record_offset = sizeof(signature);
  while (record_offset + sizeof(std::uint16_t) * 2 <= symbol_bytes) {
    std::uint16_t record_length = 0;
    std::uint16_t record_kind = 0;
    std::memcpy(&record_length, stream.data() + record_offset,
                sizeof(record_length));
    std::memcpy(&record_kind, stream.data() + record_offset + 2,
                sizeof(record_kind));

    const size_t record_start = record_offset + sizeof(std::uint16_t) * 2;
    if (record_length < sizeof(record_kind) || record_start > symbol_bytes ||
        record_length - sizeof(record_kind) > symbol_bytes - record_start) {
      break;
    }

    const size_t record_data_size = record_length - sizeof(record_kind);
    if ((record_kind == 0x1110 || record_kind == 0x1111) &&
        record_data_size > 35) {
      std::uint32_t procedure_length = 0;
      std::uint32_t code_offset = 0;
      std::uint16_t segment = 0;
      const auto *record = stream.data() + record_start;
      std::memcpy(&procedure_length, record + 12, sizeof(procedure_length));
      std::memcpy(&code_offset, record + 28, sizeof(code_offset));
      std::memcpy(&segment, record + 32, sizeof(segment));

      if (segment != 0 && segment <= sections.size()) {
        const char *name = reinterpret_cast<const char *>(record + 35);
        const size_t name_max = record_data_size - 35;
        const size_t name_size = stacktrace_bounded_strlen(name, name_max);
        if (name_size != 0 && name_size != name_max) {
          const auto rva = sections[segment - 1].rva + code_offset;
          symbols.push_back(
              {rva, procedure_length, std::string(name, name_size)});
        }
      }
    }

    record_offset = record_start + (record_length - sizeof(record_kind));
    record_offset = stacktrace_align4(record_offset);
  }
}

const std::string *stacktrace_pdb_find_checksum_file(
    const std::vector<stacktrace_pdb_file_checksum> &files,
    std::uint32_t checksum_offset) {
  for (const auto &file : files) {
    if (file.checksum_offset == checksum_offset) {
      return &file.file;
    }
  }
  return nullptr;
}

void stacktrace_pdb_parse_file_checksums(
    const std::uint8_t *payload, size_t payload_size,
    const std::vector<std::uint8_t> &names,
    std::vector<stacktrace_pdb_file_checksum> &files) {
  size_t offset = 0;
  while (offset + 6 <= payload_size) {
    const auto checksum_offset = static_cast<std::uint32_t>(offset);
    std::uint32_t name_offset = 0;
    std::memcpy(&name_offset, payload + offset, sizeof(name_offset));
    const std::uint8_t checksum_size = payload[offset + 4];

    const size_t entry_size = stacktrace_align4(6 + checksum_size);
    if (entry_size == 0 || entry_size > payload_size - offset) {
      break;
    }

    std::string file;
    if (stacktrace_pdb_read_cstr(names, name_offset, file) && !file.empty()) {
      files.push_back({checksum_offset, std::move(file)});
    }

    offset += entry_size;
  }
}

void stacktrace_pdb_parse_line_subsection(
    const std::uint8_t *payload, size_t payload_size,
    const std::vector<stacktrace_pe_section> &sections,
    const std::vector<stacktrace_pdb_file_checksum> &files,
    std::vector<stacktrace_pdb_line> &lines) {
  if (payload_size < 12) {
    return;
  }

  std::uint32_t code_offset = 0;
  std::uint16_t segment = 0;
  std::uint16_t flags = 0;
  std::uint32_t code_size = 0;
  std::memcpy(&code_offset, payload, sizeof(code_offset));
  std::memcpy(&segment, payload + 4, sizeof(segment));
  std::memcpy(&flags, payload + 6, sizeof(flags));
  std::memcpy(&code_size, payload + 8, sizeof(code_size));

  if (segment == 0 || segment > sections.size() || code_size == 0) {
    return;
  }

  const auto base_rva = sections[segment - 1].rva + code_offset;
  size_t block_offset = 12;
  while (block_offset + 12 <= payload_size) {
    std::uint32_t checksum_offset = 0;
    std::uint32_t line_count = 0;
    std::uint32_t block_size = 0;
    std::memcpy(&checksum_offset, payload + block_offset,
                sizeof(checksum_offset));
    std::memcpy(&line_count, payload + block_offset + 4,
                sizeof(line_count));
    std::memcpy(&block_size, payload + block_offset + 8,
                sizeof(block_size));

    if (block_size < 12 || block_size > payload_size - block_offset) {
      break;
    }

    const auto *file = stacktrace_pdb_find_checksum_file(files, checksum_offset);
    const size_t entries_offset = block_offset + 12;
    const size_t entries_bytes = static_cast<size_t>(line_count) * 8;
    if (entries_bytes <= block_size - 12 && file != nullptr &&
        !file->empty()) {
      for (std::uint32_t index = 0; index != line_count; ++index) {
        std::uint32_t line_offset = 0;
        std::uint32_t line_flags = 0;
        std::memcpy(&line_offset, payload + entries_offset + index * 8,
                    sizeof(line_offset));
        std::memcpy(&line_flags, payload + entries_offset + index * 8 + 4,
                    sizeof(line_flags));

        const auto line = line_flags & 0x00ffffffu;
        if (line == 0 || line_offset >= code_size) {
          continue;
        }

        std::uint32_t next_offset = code_size;
        if (index + 1 != line_count) {
          std::memcpy(&next_offset,
                      payload + entries_offset + (index + 1) * 8,
                      sizeof(next_offset));
          if (next_offset <= line_offset || next_offset > code_size) {
            next_offset = line_offset + 1;
          }
        }

        lines.push_back(
            {base_rva + line_offset, base_rva + next_offset, line, *file});
      }
    }

    block_offset += block_size;
    (void) flags;
  }
}

void stacktrace_pdb_parse_c13(
    const std::vector<std::uint8_t> &stream,
    const stacktrace_pdb_module_info &module,
    const std::vector<stacktrace_pe_section> &sections,
    const std::vector<std::uint8_t> &names,
    std::vector<stacktrace_pdb_line> &lines) {
  if (module.c13_bytes == 0 || names.empty()) {
    return;
  }

  const size_t c13_start =
      static_cast<size_t>(module.symbol_bytes) + module.c11_bytes;
  if (c13_start >= stream.size()) {
    return;
  }

  const size_t c13_end =
      (std::min)(stream.size(), c13_start + static_cast<size_t>(module.c13_bytes));

  std::vector<stacktrace_pdb_file_checksum> files;
  for (size_t offset = c13_start; offset + 8 <= c13_end;) {
    std::uint32_t kind = 0;
    std::uint32_t length = 0;
    std::memcpy(&kind, stream.data() + offset, sizeof(kind));
    std::memcpy(&length, stream.data() + offset + 4, sizeof(length));

    const size_t payload_offset = offset + 8;
    if (length > c13_end - payload_offset) {
      break;
    }

    if (kind == 0xf4) {
      stacktrace_pdb_parse_file_checksums(stream.data() + payload_offset,
                                          length, names, files);
    }

    offset = payload_offset + stacktrace_align4(length);
  }

  if (files.empty()) {
    return;
  }

  for (size_t offset = c13_start; offset + 8 <= c13_end;) {
    std::uint32_t kind = 0;
    std::uint32_t length = 0;
    std::memcpy(&kind, stream.data() + offset, sizeof(kind));
    std::memcpy(&length, stream.data() + offset + 4, sizeof(length));

    const size_t payload_offset = offset + 8;
    if (length > c13_end - payload_offset) {
      break;
    }

    if (kind == 0xf2) {
      stacktrace_pdb_parse_line_subsection(stream.data() + payload_offset,
                                           length, sections, files, lines);
    }

    offset = payload_offset + stacktrace_align4(length);
  }
}

// Minimal PDB/MSF reader for std::stacktrace diagnostics. It validates the
// RSDS GUID/age and reads CodeView procedure/line records from module streams.
bool stacktrace_pdb_parse_debug_info(
    const std::vector<std::uint8_t> &pdb,
    const stacktrace_pdb_identity &expected_identity,
    const std::vector<stacktrace_pe_section> &sections,
    std::vector<stacktrace_pdb_symbol> &symbols,
    std::vector<stacktrace_pdb_line> &lines) {
  symbols.clear();
  lines.clear();

  std::uint32_t block_size = 0;
  std::vector<stacktrace_pdb_stream_ref> streams;
  if (!stacktrace_pdb_read_stream_directory(pdb, block_size, streams) ||
      !stacktrace_pdb_validate_identity(pdb, block_size, streams,
                                        expected_identity)) {
    return false;
  }

  std::vector<std::uint8_t> names;
  stacktrace_pdb_read_names(pdb, block_size, streams, names);

  std::vector<std::uint8_t> dbi;
  std::vector<stacktrace_pdb_module_info> modules;
  if (!stacktrace_pdb_copy_stream(pdb, block_size, streams, 3, dbi) ||
      !stacktrace_pdb_read_modules(dbi, modules)) {
    return false;
  }

  for (const auto &module : modules) {
    std::vector<std::uint8_t> stream;
    if (!stacktrace_pdb_copy_stream(pdb, block_size, streams,
                                    module.stream_index, stream)) {
      continue;
    }

    stacktrace_pdb_parse_symbol_records(stream, sections, module.symbol_bytes,
                                        symbols);
    stacktrace_pdb_parse_c13(stream, module, sections, names, lines);
  }

  std::sort(symbols.begin(), symbols.end(),
            [](const auto &left, const auto &right) {
              return left.rva < right.rva;
            });
  std::sort(lines.begin(), lines.end(), [](const auto &left, const auto &right) {
    return left.rva < right.rva;
  });

  return !symbols.empty() || !lines.empty();
}

std::vector<std::string> stacktrace_pdb_candidates(
    const AUX_MODULE_EXTENDED_INFO &module, const std::string &rsds_name) {
  std::vector<std::string> candidates;

  size_t module_path_size = 0;
  const char *module_path_data = stacktrace_module_path(module, &module_path_size);
  if (module_path_data == nullptr) {
    return candidates;
  }

  const std::string module_path(module_path_data, module_path_size);
  const std::string module_dir = stacktrace_path_directory(module_path);
  const std::string rsds_basename = stacktrace_path_basename(rsds_name);

  const auto add_candidate = [&candidates](const std::string &candidate) {
    if (candidate.empty()) {
      return;
    }
    if (std::find(candidates.begin(), candidates.end(), candidate) ==
        candidates.end()) {
      candidates.push_back(candidate);
    }
  };

  if (!rsds_basename.empty() && !module_dir.empty()) {
    add_candidate(module_dir + rsds_basename);
  }

  if (stacktrace_is_absolute_dos_path(rsds_name) ||
      stacktrace_starts_with(rsds_name, "\\??\\") ||
      stacktrace_starts_with(rsds_name, "\\SystemRoot\\")) {
    add_candidate(rsds_name);
  }

  add_candidate(stacktrace_replace_extension(module_path, ".pdb"));
  return candidates;
}

bool stacktrace_load_pdb_symbols(const AUX_MODULE_EXTENDED_INFO &module,
                                 stacktrace_pdb_cache &cache) {
  cache.attempted = true;
  cache.loaded = false;
  cache.symbols.clear();
  cache.lines.clear();

  if (KeGetCurrentIrql() != PASSIVE_LEVEL) {
    return false;
  }

  const void *image_base = module.BasicInfo.ImageBase;
  std::string rsds_name;
  stacktrace_pdb_identity identity{};
  std::vector<stacktrace_pe_section> sections;
  if (!stacktrace_get_rsds_pdb_name(image_base, rsds_name, identity) ||
      !stacktrace_get_pe_sections(image_base, sections)) {
    return false;
  }

  for (const auto &candidate : stacktrace_pdb_candidates(module, rsds_name)) {
    std::vector<std::uint8_t> pdb;
    if (!stacktrace_read_file(candidate, pdb)) {
      continue;
    }

    if (stacktrace_pdb_parse_debug_info(pdb, identity, sections, cache.symbols,
                                        cache.lines)) {
      cache.loaded = true;
      return true;
    }
  }

  return false;
}

stacktrace_pdb_cache *stacktrace_get_pdb_cache(
    stacktrace_format_context &context, const AUX_MODULE_EXTENDED_INFO &module) {
  const auto module_base =
      reinterpret_cast<std::uintptr_t>(module.BasicInfo.ImageBase);

  for (auto &cache : context.pdb_caches) {
    if (cache.module_base == module_base) {
      return &cache;
    }
  }

  stacktrace_pdb_cache cache{};
  cache.module_base = module_base;
  context.pdb_caches.push_back(std::move(cache));
  return &context.pdb_caches.back();
}

const stacktrace_pdb_symbol *stacktrace_find_pdb_symbol(
    const stacktrace_pdb_cache &cache, std::uint32_t rva) {
  const stacktrace_pdb_symbol *best = nullptr;
  for (const auto &symbol : cache.symbols) {
    if (rva < symbol.rva || rva >= symbol.rva + symbol.length) {
      continue;
    }

    if (best == nullptr || symbol.rva > best->rva) {
      best = &symbol;
    }
  }
  return best;
}

const stacktrace_pdb_line *stacktrace_find_pdb_line(
    const stacktrace_pdb_cache &cache, std::uint32_t rva) {
  const stacktrace_pdb_line *best = nullptr;
  for (const auto &line : cache.lines) {
    if (rva < line.rva || rva >= line.end_rva) {
      continue;
    }

    if (best == nullptr || line.rva > best->rva) {
      best = &line;
    }
  }
  return best;
}

bool stacktrace_format_module_symbol(
    const void *address, const AUX_MODULE_EXTENDED_INFO &module,
    stacktrace_format_context &context, char *buffer, size_t buffer_size) {
  size_t name_size = 0;
  const char *name = stacktrace_module_name(module, &name_size);
  if (name == nullptr) {
    return false;
  }

  const auto base = reinterpret_cast<std::uintptr_t>(module.BasicInfo.ImageBase);
  const auto address_value = reinterpret_cast<std::uintptr_t>(address);
  const auto offset = static_cast<unsigned long long>(address_value - base);

  stacktrace_pdb_cache *cache = stacktrace_get_pdb_cache(context, module);
  if (cache != nullptr && !cache->attempted) {
    stacktrace_load_pdb_symbols(module, *cache);
  }

  if (cache != nullptr && cache->loaded) {
    const auto *symbol =
        stacktrace_find_pdb_symbol(*cache, static_cast<std::uint32_t>(offset));
    if (symbol != nullptr) {
      const auto symbol_offset =
          static_cast<unsigned long long>(offset - symbol->rva);
      const int count =
          std::snprintf(buffer, buffer_size, "%.*s!%s+0x%llX",
                        static_cast<int>(name_size), name,
                        symbol->name.c_str(), symbol_offset);
      return count > 0 && static_cast<size_t>(count) < buffer_size;
    }
  }

  const int count = std::snprintf(buffer, buffer_size, "%.*s+0x%llX",
                                  static_cast<int>(name_size), name, offset);
  return count > 0 && static_cast<size_t>(count) < buffer_size;
}

bool stacktrace_format_module_offset(
    const void *address, stacktrace_format_context &context, char *buffer,
    size_t buffer_size) {
  const auto address_value = reinterpret_cast<std::uintptr_t>(address);

  for (const auto &module : context.modules) {
    const auto base = reinterpret_cast<std::uintptr_t>(module.BasicInfo.ImageBase);
    const auto end = base + module.ImageSize;
    if (address_value < base || address_value >= end) {
      continue;
    }

    return stacktrace_format_module_symbol(address, module, context, buffer,
                                           buffer_size);
  }

  return false;
}

const stacktrace_pdb_line *stacktrace_source_line_entry(
    const void *address, stacktrace_format_context &context) {
  const auto address_value = reinterpret_cast<std::uintptr_t>(address);

  for (const auto &module : context.modules) {
    const auto base = reinterpret_cast<std::uintptr_t>(module.BasicInfo.ImageBase);
    const auto end = base + module.ImageSize;
    if (address_value < base || address_value >= end) {
      continue;
    }

    stacktrace_pdb_cache *cache = stacktrace_get_pdb_cache(context, module);
    if (cache != nullptr && !cache->attempted) {
      stacktrace_load_pdb_symbols(module, *cache);
    }

    if (cache == nullptr || !cache->loaded) {
      return nullptr;
    }

    return stacktrace_find_pdb_line(
        *cache, static_cast<std::uint32_t>(address_value - base));
  }

  return nullptr;
}

void stacktrace_format_address(const void *address, char *buffer,
                               size_t buffer_size,
                               stacktrace_format_context *context) {
  if (context != nullptr && context->have_modules &&
      stacktrace_format_module_offset(address, *context, buffer, buffer_size)) {
    return;
  }

  const int count = std::snprintf(buffer, buffer_size, "%p", address);
  if (count <= 0 || static_cast<size_t>(count) >= buffer_size) {
    if (buffer_size != 0) {
      buffer[0] = '\0';
    }
  }
}

void stacktrace_fill_address(const void *address, void *string,
                             _Stacktrace_string_fill fill) {
  stacktrace_format_context context;
  context.have_modules = stacktrace_query_modules(context.modules);

  char buffer[stacktrace_address_text_size]{};
  stacktrace_format_address(address, buffer, sizeof(buffer), &context);
  const int count = static_cast<int>(std::strlen(buffer));
  if (count > 0) {
    stacktrace_fill_text(string, fill, buffer, static_cast<size_t>(count));
  }
}

void stacktrace_fill_source_file(const void *address, void *string,
                                 _Stacktrace_string_fill fill) {
  stacktrace_format_context context;
  context.have_modules = stacktrace_query_modules(context.modules);

  if (context.have_modules) {
    if (const auto *line = stacktrace_source_line_entry(address, context)) {
      if (!line->file.empty()) {
        stacktrace_fill_text(string, fill, line->file.data(),
                             line->file.size());
        return;
      }
    }
  }

  stacktrace_fill_cstr(string, fill, "");
}

unsigned int stacktrace_source_line_number(const void *address) {
  stacktrace_format_context context;
  context.have_modules = stacktrace_query_modules(context.modules);

  if (context.have_modules) {
    if (const auto *line = stacktrace_source_line_entry(address, context)) {
      return line->line;
    }
  }

  return 0;
}
} // namespace

extern "C" {
unsigned short __stdcall
__std_stacktrace_capture(unsigned long frames_to_skip,
                         unsigned long frames_to_capture, void **back_trace,
                         unsigned long *back_trace_hash) noexcept {
  return RtlCaptureStackBackTrace(frames_to_skip + 1, frames_to_capture,
                                  back_trace, back_trace_hash);
}

void __stdcall
__std_stacktrace_address_to_string(const void *address, void *string,
                                   _Stacktrace_string_fill fill) {
  stacktrace_fill_address(address, string, fill);
}

void __stdcall __std_stacktrace_description(const void *address, void *string,
                                            _Stacktrace_string_fill fill) {
  stacktrace_fill_address(address, string, fill);
}

void __stdcall __std_stacktrace_source_file(const void *address, void *string,
                                            _Stacktrace_string_fill fill) {
  stacktrace_fill_source_file(address, string, fill);
}

unsigned int __stdcall __std_stacktrace_source_line(
    const void *address) noexcept {
  return stacktrace_source_line_number(address);
}

void __stdcall __std_stacktrace_to_string(const void *const *addresses,
                                          size_t size, void *string,
                                          _Stacktrace_string_fill fill) {
  if (fill == nullptr) {
    return;
  }

  stacktrace_format_context context;
  context.have_modules = stacktrace_query_modules(context.modules);
  std::string text;

  for (size_t index = 0; index != size; ++index) {
    char address_text[stacktrace_address_text_size]{};
    stacktrace_format_address(addresses[index], address_text,
                              sizeof(address_text), &context);

    if (address_text[0] == '\0') {
      break;
    }

    if (!text.empty()) {
      text.push_back('\n');
    }

    text.append(std::to_string(index));
    text.append("> ");
    text.append(address_text);
  }

  stacktrace_fill_text(string, fill, text.data(), text.size());
}
} // extern "C"
