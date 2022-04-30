#include <vcruntime_internal.h>

#pragma warning(disable : 4100)

#include <wdm.h>

extern "C" __declspec(noreturn) void __cdecl _invoke_watson(
    wchar_t const *const expression, wchar_t const *const function_name,
    wchar_t const *const file_name, unsigned int const line_number,
    uintptr_t const reserved) {
  KdBreakPoint();
}

extern "C" void __cdecl _invalid_parameter(wchar_t const *const expression,
                                           wchar_t const *const function_name,
                                           wchar_t const *const file_name,
                                           unsigned int const line_number,
                                           uintptr_t const reserved) {
  KdBreakPoint();
}

extern "C" void __cdecl _invalid_parameter_noinfo() {
  _invalid_parameter(nullptr, nullptr, nullptr, 0, 0);
}

extern "C" __declspec(
    noreturn) void __cdecl _invalid_parameter_noinfo_noreturn() {
  _invalid_parameter(nullptr, nullptr, nullptr, 0, 0);
}
