// clang-format off

#include <ntifs.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

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

void stacktrace_fill_address(const void *address, void *string,
                             _Stacktrace_string_fill fill) {
  char buffer[2 + sizeof(void *) * 2 + 1]{};
  const int count = std::snprintf(buffer, sizeof(buffer), "%p", address);
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

  char buffer[512]{};
  size_t offset = 0;

  for (size_t index = 0; index != size; ++index) {
    const int count = std::snprintf(
        buffer + offset, sizeof(buffer) - offset, "%s%u> %p",
        index == 0 ? "" : "\n", static_cast<unsigned int>(index),
        addresses[index]);
    if (count <= 0) {
      break;
    }

    offset += static_cast<size_t>(count);
    if (offset >= sizeof(buffer)) {
      offset = sizeof(buffer) - 1;
      break;
    }
  }

  stacktrace_fill_text(string, fill, buffer, offset);
}
} // extern "C"
