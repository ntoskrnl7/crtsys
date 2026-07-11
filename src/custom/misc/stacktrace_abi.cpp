// clang-format off

#include <ntifs.h>
#include <aux_klib.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
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

struct stacktrace_pdb_identity {
  std::uint8_t guid[16];
  std::uint32_t age;
};

struct stacktrace_pdb_cache {
  std::uintptr_t module_base{};
  bool attempted{};
  bool loaded{};
  std::vector<stacktrace_pdb_symbol> symbols;
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

// Minimal PDB/MSF reader for std::stacktrace diagnostics. It validates the
// RSDS GUID/age and reads CodeView procedure records from module streams.
bool stacktrace_pdb_parse_symbols(
    const std::vector<std::uint8_t> &pdb,
    const stacktrace_pdb_identity &expected_identity,
    const std::vector<stacktrace_pe_section> &sections,
    std::vector<stacktrace_pdb_symbol> &symbols) {
  symbols.clear();

  static constexpr char msf70_magic[] =
      "Microsoft C/C++ MSF 7.00\r\n\x1a""DS\0\0\0";
  if (pdb.size() < 56 ||
      std::memcmp(pdb.data(), msf70_magic, sizeof(msf70_magic) - 1) != 0) {
    return false;
  }

  std::uint32_t block_size = 0;
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

  bool identity_matches = false;
  for (std::uint32_t stream_index = 0; stream_index != stream_count;
       ++stream_index) {
    const std::uint32_t stream_size = stream_sizes[stream_index];
    if (stream_size == 0 || stream_size == 0xffffffffu) {
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

    if (stream_index == 1) {
      std::uint8_t info[28]{};
      if (stream_size < sizeof(info) ||
          !stacktrace_msf_read_stream_bytes(pdb, block_size, blocks,
                                            stream_size, 0, info,
                                            sizeof(info))) {
        return false;
      }

      std::uint32_t age = 0;
      std::memcpy(&age, info + 8, sizeof(age));
      identity_matches =
          age == expected_identity.age &&
          std::memcmp(info + 12, expected_identity.guid,
                      sizeof(expected_identity.guid)) == 0;
      if (!identity_matches) {
        return false;
      }
      continue;
    }

    std::uint32_t signature = 0;
    if (stream_size < sizeof(signature) ||
        !stacktrace_msf_read_stream_bytes(pdb, block_size, blocks, stream_size,
                                          0, &signature, sizeof(signature)) ||
        signature != 4) {
      continue;
    }

    std::vector<std::uint8_t> stream;
    if (!stacktrace_msf_copy_stream(pdb, block_size, blocks, stream_size,
                                    stream)) {
      continue;
    }

    size_t record_offset = sizeof(signature);
    while (record_offset + sizeof(std::uint16_t) * 2 <= stream.size()) {
      std::uint16_t record_length = 0;
      std::uint16_t record_kind = 0;
      std::memcpy(&record_length, stream.data() + record_offset,
                  sizeof(record_length));
      std::memcpy(&record_kind, stream.data() + record_offset + 2,
                  sizeof(record_kind));

      const size_t record_start = record_offset + sizeof(std::uint16_t) * 2;
      if (record_length < sizeof(record_kind) ||
          record_start > stream.size() ||
          record_length - sizeof(record_kind) > stream.size() - record_start) {
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
      record_offset = (record_offset + 3) & ~static_cast<size_t>(3);
    }
  }

  return identity_matches && !symbols.empty();
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

    if (stacktrace_pdb_parse_symbols(pdb, identity, sections, cache.symbols)) {
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

void __stdcall __std_stacktrace_source_file(const void *, void *string,
                                            _Stacktrace_string_fill fill) {
  stacktrace_fill_cstr(string, fill, "");
}

unsigned int __stdcall __std_stacktrace_source_line(const void *) noexcept {
  return 0;
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
