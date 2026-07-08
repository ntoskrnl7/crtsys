#include "undname.h"

#include <stdlib.h>
#include <string.h>

struct crtsys_type_component {
  const char* first;
  size_t length;
};

static bool crtsys_parse_scoped_name(const char* name,
                                     crtsys_type_component* components,
                                     size_t capacity,
                                     size_t* count) {
  *count = 0;
  for (const char* it = name;;) {
    if (*it == '\0' || *it == '?' || *it == '$' || *count == capacity) {
      return false;
    }

    const char* const first = it;
    while (*it != '@') {
      if (*it == '\0' || *it == '?' || *it == '$') {
        return false;
      }
      ++it;
    }

    if (it == first) {
      return false;
    }

    components[*count] = {first, static_cast<size_t>(it - first)};
    ++*count;
    if (it[0] == '@' && it[1] == '@') {
      return true;
    }
    ++it;
  }
}

static char* crtsys_copy_result(char* output,
                                int output_count,
                                void* (__cdecl* alloc)(size_t),
                                const char* text,
                                size_t text_length) {
  const size_t required = text_length + 1;
  char* result = output;

  if (result == nullptr) {
    if (alloc == nullptr) {
      return nullptr;
    }
    result = static_cast<char*>(alloc(required));
    if (result == nullptr) {
      return nullptr;
    }
  } else if (output_count <= 0) {
    return nullptr;
  } else if (required > static_cast<size_t>(output_count)) {
    text_length = static_cast<size_t>(output_count - 1);
  }

  if (text_length != 0) {
    memcpy(result, text, text_length);
  }
  result[text_length] = '\0';
  return result;
}

static char* crtsys_copy_scoped_type(char* output,
                                     int output_count,
                                     void* (__cdecl* alloc)(size_t),
                                     const char* name,
                                     const char* kind) {
  crtsys_type_component components[32]{};
  size_t component_count = 0;
  if (!crtsys_parse_scoped_name(name, components, 32, &component_count)) {
    return nullptr;
  }

  size_t length = strlen(kind);
  for (size_t i = component_count; i != 0; --i) {
    length += components[i - 1].length;
    if (i != 1) {
      length += 2;
    }
  }

  char* result = output;
  if (result == nullptr) {
    if (alloc == nullptr) {
      return nullptr;
    }
    result = static_cast<char*>(alloc(length + 1));
    if (result == nullptr) {
      return nullptr;
    }
  } else if (output_count <= 0) {
    return nullptr;
  }

  const size_t writable =
      output_count > 0 ? static_cast<size_t>(output_count - 1) : length;
  size_t written = 0;
  auto append = [&](const char* text, size_t count) {
    const size_t available = written < writable ? writable - written : 0;
    const size_t to_copy = count < available ? count : available;
    if (to_copy != 0) {
      memcpy(result + written, text, to_copy);
    }
    written += count;
  };

  append(kind, strlen(kind));
  for (size_t i = component_count; i != 0; --i) {
    append(components[i - 1].first, components[i - 1].length);
    if (i != 1) {
      append("::", 2);
    }
  }

  const size_t terminator = written < writable ? written : writable;
  result[terminator] = '\0';
  return result;
}

extern "C" char* __cdecl __unDName(char* outputString,
                                    const char* name,
                                    int maxStringLength,
                                    void* (__cdecl* pAlloc)(size_t),
                                    void (__cdecl* pFree)(void*),
                                    unsigned short disableFlags) {
  (void)pFree;
  (void)disableFlags;

  if (name == nullptr) {
    return nullptr;
  }

  if (strcmp(name, "_N") == 0) {
    return crtsys_copy_result(outputString, maxStringLength, pAlloc, "bool", 4);
  }
  if (strcmp(name, "D") == 0) {
    return crtsys_copy_result(outputString, maxStringLength, pAlloc, "char", 4);
  }
  if (strcmp(name, "C") == 0) {
    return crtsys_copy_result(outputString, maxStringLength, pAlloc,
                              "signed char", 11);
  }
  if (strcmp(name, "E") == 0) {
    return crtsys_copy_result(outputString, maxStringLength, pAlloc,
                              "unsigned char", 13);
  }
  if (strcmp(name, "F") == 0) {
    return crtsys_copy_result(outputString, maxStringLength, pAlloc, "short",
                              5);
  }
  if (strcmp(name, "G") == 0) {
    return crtsys_copy_result(outputString, maxStringLength, pAlloc,
                              "unsigned short", 14);
  }
  if (strcmp(name, "H") == 0) {
    return crtsys_copy_result(outputString, maxStringLength, pAlloc, "int", 3);
  }
  if (strcmp(name, "I") == 0) {
    return crtsys_copy_result(outputString, maxStringLength, pAlloc,
                              "unsigned int", 12);
  }
  if (strcmp(name, "J") == 0) {
    return crtsys_copy_result(outputString, maxStringLength, pAlloc, "long",
                              4);
  }
  if (strcmp(name, "K") == 0) {
    return crtsys_copy_result(outputString, maxStringLength, pAlloc,
                              "unsigned long", 13);
  }
  if (strcmp(name, "_J") == 0) {
    return crtsys_copy_result(outputString, maxStringLength, pAlloc, "__int64",
                              7);
  }
  if (strcmp(name, "_K") == 0) {
    return crtsys_copy_result(outputString, maxStringLength, pAlloc,
                              "unsigned __int64", 16);
  }
  if (strcmp(name, "M") == 0) {
    return crtsys_copy_result(outputString, maxStringLength, pAlloc, "float",
                              5);
  }
  if (strcmp(name, "N") == 0) {
    return crtsys_copy_result(outputString, maxStringLength, pAlloc, "double",
                              6);
  }
  if (strcmp(name, "O") == 0) {
    return crtsys_copy_result(outputString, maxStringLength, pAlloc,
                              "long double", 11);
  }
  if (strcmp(name, "X") == 0) {
    return crtsys_copy_result(outputString, maxStringLength, pAlloc, "void", 4);
  }

  if (strncmp(name, "?AV", 3) == 0) {
    if (char* result = crtsys_copy_scoped_type(outputString, maxStringLength,
                                               pAlloc, name + 3, "class ")) {
      return result;
    }
  }
  if (strncmp(name, "?AU", 3) == 0) {
    if (char* result = crtsys_copy_scoped_type(outputString, maxStringLength,
                                               pAlloc, name + 3, "struct ")) {
      return result;
    }
  }
  if (strncmp(name, "?AT", 3) == 0) {
    if (char* result = crtsys_copy_scoped_type(outputString, maxStringLength,
                                               pAlloc, name + 3, "union ")) {
      return result;
    }
  }
  if (strncmp(name, "?AW4", 4) == 0) {
    if (char* result = crtsys_copy_scoped_type(outputString, maxStringLength,
                                               pAlloc, name + 4, "enum ")) {
      return result;
    }
  }

  return crtsys_copy_result(outputString, maxStringLength, pAlloc, name,
                            strlen(name));
}
