#include <vcruntime_internal.h>

extern "C" void __cdecl _invalid_parameter(
    wchar_t const* const expression,
    wchar_t const* const function_name,
    wchar_t const* const file_name,
    unsigned int   const line_number,
    uintptr_t      const reserved
    )
{
    expression;
    function_name;
    file_name;
    line_number;
    reserved;
}

extern "C" void __cdecl _invalid_parameter_noinfo()
{
    _invalid_parameter(nullptr, nullptr, nullptr, 0, 0);
}

extern "C" __declspec(noreturn) void __cdecl _invalid_parameter_noinfo_noreturn()
{
    _invalid_parameter(nullptr, nullptr, nullptr, 0, 0);
}
