#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <iostream>
#include <stdexcept>

namespace crt_environment_semantic_test {
namespace {
struct variable_names {
  char missing[80]{};
  char mutation[80]{};
  wchar_t wide_missing[80]{};
  wchar_t wide_mutation[80]{};
};

void expect(bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

variable_names make_variable_names() {
  variable_names names{};
  const auto seed =
      static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(&names));

  // Kernel driver tests can be loaded repeatedly into the same pseudo-process
  // environment. Keep the MS UCRT semantics under test identical, but use fresh
  // names so the "missing variable" cases are not affected by a prior failed
  // load that left process-environment state behind.
  std::snprintf(names.missing, sizeof(names.missing),
                "CRTSYS_CRT_ENV_MISSING_%llX", seed);
  std::snprintf(names.mutation, sizeof(names.mutation),
                "CRTSYS_CRT_ENV_MUTATION_%llX", seed);
  std::swprintf(names.wide_missing,
                sizeof(names.wide_missing) / sizeof(names.wide_missing[0]),
                L"CRTSYS_CRT_WENV_MISSING_%llX", seed);
  std::swprintf(names.wide_mutation,
                sizeof(names.wide_mutation) / sizeof(names.wide_mutation[0]),
                L"CRTSYS_CRT_WENV_MUTATION_%llX", seed);
  return names;
}

const char *read_environment(const char *name) {
#pragma warning(push)
#pragma warning(disable : 4996)
  // getenv is the standard CRT API that user code sees. MSVC warns in favor of
  // getenv_s, but this semantic test intentionally covers both paths.
  const char *value = std::getenv(name);
#pragma warning(pop)
  return value;
}

wchar_t *read_environment(const wchar_t *name) {
#pragma warning(push)
#pragma warning(disable : 4996)
  // _wgetenv is the wide CRT API that user code sees. MSVC warns in favor of
  // _wgetenv_s, but this semantic test intentionally covers both paths.
  wchar_t *value = _wgetenv(name);
#pragma warning(pop)
  return value;
}

void expect_missing(const char *name, const char *message) {
  const char *value = read_environment(name);
  if (value != nullptr) {
    std::cout << message << ": getenv returned '" << value << "'\n";
    throw std::runtime_error(message);
  }
}

void expect_missing(const wchar_t *name, const char *message) {
  const wchar_t *value = read_environment(name);
  if (value != nullptr) {
    std::wcout << message << L": _wgetenv returned '" << value << L"'\n";
    throw std::runtime_error(message);
  }
}

void verify_narrow_environment(const char *missing_variable_name,
                               const char *mutation_variable_name) {
  expect(_putenv_s(mutation_variable_name, "") == 0,
         "_putenv_s missing delete failed");
  expect_missing(mutation_variable_name,
                 "_putenv_s missing delete created the variable");

  expect(_putenv_s(mutation_variable_name, "alpha") == 0,
         "_putenv_s alpha failed");
  const char *raw_value = read_environment(mutation_variable_name);
  expect(raw_value != nullptr,
         "_putenv_s alpha was not visible through getenv");
  expect(std::strcmp(raw_value, "alpha") == 0,
         "_putenv_s alpha returned unexpected value");

  size_t required = 0;
  expect(getenv_s(&required, nullptr, 0, mutation_variable_name) == 0,
         "getenv_s size query failed");
  expect(required == std::strlen(raw_value) + 1,
         "getenv_s returned unexpected size");

  char buffer[260]{};
  expect(getenv_s(&required, buffer, sizeof(buffer), mutation_variable_name) ==
             0,
         "getenv_s buffer query failed");
  expect(std::strcmp(buffer, raw_value) == 0,
         "getenv_s returned unexpected value");

  char *duplicated = nullptr;
  size_t duplicated_length = 0;
  expect(_dupenv_s(&duplicated, &duplicated_length, mutation_variable_name) ==
             0,
         "_dupenv_s failed");
  expect(duplicated != nullptr, "_dupenv_s returned null");
  expect(duplicated_length == std::strlen(raw_value) + 1,
         "_dupenv_s returned unexpected size");
  expect(std::strcmp(duplicated, raw_value) == 0,
         "_dupenv_s returned unexpected value");
  std::free(duplicated);

  required = 1;
  expect(getenv_s(&required, nullptr, 0, missing_variable_name) == 0,
         "getenv_s missing variable query failed");
  expect(required == 0, "getenv_s missing variable returned a size");

  expect(_putenv_s(mutation_variable_name, "alpha-beta-gamma-long-value") == 0,
         "_putenv_s long update failed");
  expect(read_environment(mutation_variable_name) != nullptr,
         "_putenv_s long update was not visible through getenv");
  expect(std::strcmp(read_environment(mutation_variable_name),
                     "alpha-beta-gamma-long-value") == 0,
         "_putenv_s long update returned unexpected value");

  expect(_putenv_s(mutation_variable_name, "") == 0,
         "_putenv_s delete failed");
  expect_missing(mutation_variable_name,
                 "_putenv_s delete left the variable visible");
}

void verify_wide_environment(const wchar_t *wide_missing_variable_name,
                             const wchar_t *wide_mutation_variable_name) {
  expect(_wputenv_s(wide_mutation_variable_name, L"") == 0,
         "_wputenv_s missing delete failed");
  expect_missing(wide_mutation_variable_name,
                 "_wputenv_s missing delete created the variable");

  expect(_wputenv_s(wide_mutation_variable_name, L"wide") == 0,
         "_wputenv_s wide failed");
  const wchar_t *raw_value = read_environment(wide_mutation_variable_name);
  expect(raw_value != nullptr,
         "_wputenv_s wide was not visible through _wgetenv");
  expect(std::wcscmp(raw_value, L"wide") == 0,
         "_wputenv_s wide returned unexpected value");

  size_t required = 0;
  expect(_wgetenv_s(&required, nullptr, 0, wide_mutation_variable_name) == 0,
         "_wgetenv_s size query failed");
  expect(required == std::wcslen(raw_value) + 1,
         "_wgetenv_s returned unexpected size");

  wchar_t buffer[260]{};
  expect(_wgetenv_s(&required, buffer, sizeof(buffer) / sizeof(buffer[0]),
                    wide_mutation_variable_name) == 0,
         "_wgetenv_s buffer query failed");
  expect(std::wcscmp(buffer, raw_value) == 0,
         "_wgetenv_s returned unexpected value");

  wchar_t *duplicated = nullptr;
  size_t duplicated_length = 0;
  expect(_wdupenv_s(&duplicated, &duplicated_length,
                    wide_mutation_variable_name) == 0,
         "_wdupenv_s failed");
  expect(duplicated != nullptr, "_wdupenv_s returned null");
  expect(duplicated_length == std::wcslen(raw_value) + 1,
         "_wdupenv_s returned unexpected size");
  expect(std::wcscmp(duplicated, raw_value) == 0,
         "_wdupenv_s returned unexpected value");
  std::free(duplicated);

  required = 1;
  expect(_wgetenv_s(&required, nullptr, 0, wide_missing_variable_name) == 0,
         "_wgetenv_s missing variable query failed");
  expect(required == 0, "_wgetenv_s missing variable returned a size");

  expect(_wputenv_s(wide_mutation_variable_name,
                    L"wide-alpha-beta-gamma-long-value") == 0,
         "_wputenv_s long update failed");
  expect(read_environment(wide_mutation_variable_name) != nullptr,
         "_wputenv_s long update was not visible through _wgetenv");
  expect(std::wcscmp(read_environment(wide_mutation_variable_name),
                     L"wide-alpha-beta-gamma-long-value") == 0,
         "_wputenv_s long update returned unexpected value");

  expect(_wputenv_s(wide_mutation_variable_name, L"") == 0,
         "_wputenv_s delete failed");
  expect_missing(wide_mutation_variable_name,
                 "_wputenv_s delete left the variable visible");
}
} // namespace

void run() {
  const variable_names names = make_variable_names();
  verify_narrow_environment(names.missing, names.mutation);
  verify_wide_environment(names.wide_missing, names.wide_mutation);
  std::cout << "CRT environment semantic assertions passed\n";
}
} // namespace crt_environment_semantic_test
