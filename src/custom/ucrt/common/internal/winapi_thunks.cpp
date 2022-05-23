#include <windows.h>

extern "C" BOOL WINAPI __acrt_InitializeCriticalSectionEx(LPCRITICAL_SECTION const critical_section,
                                                          DWORD const spin_count, DWORD const flags)
{
    flags;
    return InitializeCriticalSectionAndSpinCount(critical_section, spin_count);
}

extern "C" DWORD WINAPI __acrt_FlsAlloc(PFLS_CALLBACK_FUNCTION const callback)
{
    return FlsAlloc(callback);
}

extern "C" BOOL WINAPI __acrt_FlsFree(DWORD const fls_index)
{
    return FlsFree(fls_index);
}

extern "C" PVOID WINAPI __acrt_FlsGetValue(DWORD const fls_index)
{
    return FlsGetValue(fls_index);
}

extern "C" BOOL WINAPI __acrt_FlsSetValue(DWORD const fls_index, PVOID const fls_data)
{
    return FlsSetValue(fls_index, fls_data);
}

extern "C" BOOL WINAPI __acrt_AreFileApisANSI()
{
    return AreFileApisANSI();
}

extern "C"
_Success_(return != 0)
int __cdecl __acrt_MultiByteToWideChar(
    _In_                           UINT    _CodePage,
    _In_                           DWORD   _DWFlags,
    _In_                           LPCSTR  _LpMultiByteStr,
    _In_                           int     _CbMultiByte,
    _Out_writes_opt_(_CchWideChar) LPWSTR  _LpWideCharStr,
    _In_                           int     _CchWideChar
    )
{
    return MultiByteToWideChar(_CodePage, _DWFlags, _LpMultiByteStr, _CbMultiByte, _LpWideCharStr, _CchWideChar);
}

extern "C"
_Success_(return != 0)
int __cdecl __acrt_WideCharToMultiByte(
    _In_                           UINT    _CodePage,
    _In_                           DWORD   _DWFlags,
    _In_                           LPCWSTR _LpWideCharStr,
    _In_                           int     _CchWideChar,
    _Out_writes_opt_(_CbMultiByte) LPSTR   _LpMultiByteStr,
    _In_                           int     _CbMultiByte,
    _In_opt_                       LPCSTR  _LpDefaultChar,
    _Out_opt_                      LPBOOL  _LpUsedDefaultChar
    )
{
    return WideCharToMultiByte(_CodePage, _DWFlags, _LpWideCharStr, _CchWideChar, _LpMultiByteStr, _CbMultiByte, _LpDefaultChar, _LpUsedDefaultChar);
}

extern "C" int WINAPI __acrt_GetUserDefaultLocaleName(
    LPWSTR const locale_name,
    int    const locale_name_count
    )
{
    return GetUserDefaultLocaleName(locale_name, locale_name_count);
}


extern "C" int WINAPI __acrt_LCIDToLocaleName(
    LCID   const locale,
    LPWSTR const name,
    int    const name_count,
    DWORD  const flags
    )
{
    return LCIDToLocaleName(locale, name, name_count, flags);
}

extern "C" LCID WINAPI __acrt_LocaleNameToLCID(
    LPCWSTR const name,
    DWORD   const flags
    )
{
    return LocaleNameToLCID(name, flags);
}

extern "C" BOOL WINAPI __acrt_IsValidLocaleName(LPCWSTR const locale_name)
{
    return IsValidLocale(__acrt_LocaleNameToLCID(locale_name, 0), LCID_INSTALLED);
}



#define _CORECRT_BUILD
#define _CRT_GLOBAL_STATE_ISOLATION
#define _CRT_DECLARE_GLOBAL_VARIABLES_DIRECTLY
#include <corecrt_internal.h>

#pragma warning(disable : 4100)

extern "C" HRESULT WINAPI __acrt_RoInitialize(RO_INIT_TYPE const init_type)
{
    return S_OK;
}

extern "C" void WINAPI __acrt_RoUninitialize()
{
}


//
//
//

extern "C" void __cdecl __acrt_eagerly_load_locale_apis()
{
}

extern "C"
begin_thread_init_policy __cdecl __acrt_get_begin_thread_init_policy()
{
    return begin_thread_init_policy_none;
}

extern "C"
process_end_policy __cdecl __acrt_get_process_end_policy(void)
{
    return process_end_policy_exit_process;
}


extern "C" BOOL WINAPI __acrt_EnumSystemLocalesEx(
    LOCALE_ENUMPROCEX const enum_proc,
    DWORD             const flags,
    LPARAM            const param,
    LPVOID            const reserved
    )
{
    return EnumSystemLocalesEx(enum_proc, flags, param, reserved);
}




extern "C" int WINAPI __acrt_GetDateFormatEx(
    LPCWSTR           const locale_name,
    DWORD             const flags,
    SYSTEMTIME CONST* const date,
    LPCWSTR           const format,
    LPWSTR            const buffer,
    int               const buffer_count,
    LPCWSTR           const calendar
    )
{
    return GetDateFormatW(__acrt_LocaleNameToLCID(locale_name, 0), flags, date, format, buffer, buffer_count);
}

extern "C" int WINAPI __acrt_GetTimeFormatEx(
    LPCWSTR           const locale_name,
    DWORD             const flags,
    SYSTEMTIME CONST* const time,
    LPCWSTR           const format,
    LPWSTR            const buffer,
    int               const buffer_count
    )
{
    return GetTimeFormatW(__acrt_LocaleNameToLCID(locale_name, 0), flags, time, format, buffer, buffer_count);
}

extern "C" int WINAPI __acrt_GetLocaleInfoEx(
    LPCWSTR const locale_name,
    LCTYPE  const lc_type,
    LPWSTR  const data,
    int     const data_count
    )
{
    return GetLocaleInfoW(__acrt_LocaleNameToLCID(locale_name, 0), lc_type, data, data_count);
}

extern "C"
int __cdecl __acrt_GetLocaleInfoA(_locale_t const locale, int const lc_type, wchar_t const *const locale_name,
                                  LCTYPE const locale_type, void *const void_result)
{
    return 0;
}

extern "C" int __cdecl __acrt_LCMapStringA(_locale_t const plocinfo, PCWSTR const LocaleName, DWORD const dwMapFlags,
                                           PCCH const lpSrcStr, int const cchSrc, PCH const lpDestStr,
                                           int const cchDest, int const code_page, BOOL const bError)
{
    return 0;
}

extern "C" bool __cdecl __acrt_can_use_vista_locale_apis()
{
    return true;
}

extern "C" bool __cdecl __acrt_initialize_winapi_thunks()
{
    return true;
}
extern "C" bool __cdecl __acrt_uninitialize_winapi_thunks(bool const terminating)
{
    return true;
}
bool __cdecl __acrt_initialize_command_line()
{
    return true;
}
bool __cdecl __acrt_uninitialize_command_line(bool const terminating)
{
    return true;
}
void __cdecl __acrt_initialize_user_matherr(void* encoded_null)
{
}

extern "C" {
    char**    __argv   = nullptr; // The arguments as narrow strings
    wchar_t** __wargv  = nullptr; // The arguments as wide strings
}

//
// :-(
//
extern "C" errno_t __cdecl _configure_narrow_argv(_crt_argv_mode const mode)
{
    return 0;
}

extern "C" int __cdecl __acrt_show_wide_message_box(
    wchar_t const* const text,
    wchar_t const* const caption,
    unsigned       const type
    )
{
    return 0;
}

#include <float.h>

#ifdef _M_IX86
extern "C" unsigned int __cdecl __get_fpsr_sse2()
{
    return 0;
}

extern "C" void __cdecl __set_fpsr_sse2(unsigned int)
{
}

extern "C" void __cdecl _setdefaultprecision()
{
    _controlfp_s(NULL, _PC_53, _MCW_PC);
}
#endif