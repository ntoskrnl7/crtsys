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
struct stacktrace_text {
  const char *data;
  size_t size;
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

bool stacktrace_format_module_offset(
    const void *address, const AUX_MODULE_EXTENDED_INFO *modules,
    size_t module_count, char *buffer, size_t buffer_size) {
  const auto address_value = reinterpret_cast<std::uintptr_t>(address);

  for (size_t index = 0; index != module_count; ++index) {
    const auto &module = modules[index];
    const auto base = reinterpret_cast<std::uintptr_t>(module.BasicInfo.ImageBase);
    const auto end = base + module.ImageSize;
    if (address_value < base || address_value >= end) {
      continue;
    }

    size_t name_size = 0;
    const char *name = stacktrace_module_name(module, &name_size);
    if (name == nullptr) {
      return false;
    }

    const auto offset = static_cast<unsigned long long>(address_value - base);
    const int count = std::snprintf(buffer, buffer_size, "%.*s+0x%llX",
                                    static_cast<int>(name_size), name, offset);
    return count > 0 && static_cast<size_t>(count) < buffer_size;
  }

  return false;
}

void stacktrace_format_address(const void *address, char *buffer,
                               size_t buffer_size,
                               const AUX_MODULE_EXTENDED_INFO *modules,
                               size_t module_count) {
  if (modules != nullptr &&
      stacktrace_format_module_offset(address, modules, module_count, buffer,
                                      buffer_size)) {
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
  std::vector<AUX_MODULE_EXTENDED_INFO> modules;
  const bool have_modules = stacktrace_query_modules(modules);

  char buffer[AUX_KLIB_MODULE_PATH_LEN + sizeof("+0x") +
              sizeof(void *) * 2 + 1]{};
  stacktrace_format_address(address, buffer, sizeof(buffer),
                            have_modules ? modules.data() : nullptr,
                            have_modules ? modules.size() : 0);
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

  std::vector<AUX_MODULE_EXTENDED_INFO> modules;
  const bool have_modules = stacktrace_query_modules(modules);
  std::string text;

  for (size_t index = 0; index != size; ++index) {
    char address_text[AUX_KLIB_MODULE_PATH_LEN + sizeof("+0x") +
                      sizeof(void *) * 2 + 1]{};
    stacktrace_format_address(addresses[index], address_text,
                              sizeof(address_text),
                              have_modules ? modules.data() : nullptr,
                              have_modules ? modules.size() : 0);

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
