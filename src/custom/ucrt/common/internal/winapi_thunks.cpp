#define WIN32_LEAN_AND_MEAN
#include <windows.h>

extern "C" bool __cdecl __acrt_uninitialize_winapi_thunks(bool const terminating)
{
    UNREFERENCED_PARAMETER(terminating);
    return true;
}

extern "C" BOOL WINAPI __acrt_InitializeCriticalSectionEx(LPCRITICAL_SECTION const critical_section,
                                                          DWORD const spin_count, DWORD const flags)
{
    UNREFERENCED_PARAMETER(flags);
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

extern "C" PVOID WINAPI __acrt_FlsGetValue2(DWORD const fls_index)
{
    return FlsGetValue(fls_index);
}

extern "C" bool __cdecl __acrt_tls2_supported()
{
    return false;
}

extern "C" BOOL WINAPI __acrt_FlsSetValue(DWORD const fls_index, PVOID const fls_data)
{
    return FlsSetValue(fls_index, fls_data);
}

extern "C" BOOL WINAPI __acrt_AreFileApisANSI()
{
    return AreFileApisANSI();
}

extern "C" void WINAPI __acrt_GetSystemTimePreciseAsFileTime(LPFILETIME const system_time_as_file_time)
{
    GetSystemTimePreciseAsFileTime(system_time_as_file_time);
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

extern "C" void __cdecl __acrt_eagerly_load_locale_apis()
{
}

extern "C"
begin_thread_init_policy __cdecl __acrt_get_begin_thread_init_policy()
{
    return begin_thread_init_policy_none;
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
    UNREFERENCED_PARAMETER(locale);

    if (void_result == nullptr)
    {
        return -1;
    }

    if (lc_type == LC_INT_TYPE)
    {
        DWORD value = 0;
        const LCTYPE base_type = locale_type & ~(LOCALE_RETURN_NUMBER | LOCALE_USE_CP_ACP | LOCALE_NOUSEROVERRIDE);
        const int actual = __acrt_GetLocaleInfoEx(
            locale_name,
            base_type | LOCALE_RETURN_NUMBER,
            reinterpret_cast<LPWSTR>(&value),
            sizeof(value) / sizeof(wchar_t));
        if (actual == 0)
        {
            return -1;
        }

        if (base_type == LOCALE_IDEFAULTANSICODEPAGE || base_type == LOCALE_IDEFAULTCODEPAGE)
        {
            *static_cast<int*>(void_result) = static_cast<int>(value);
        }
        else
        {
            *static_cast<unsigned char*>(void_result) = static_cast<unsigned char>(value);
        }
        return 0;
    }

    const int wide_count = __acrt_GetLocaleInfoEx(locale_name, locale_type, nullptr, 0);
    if (wide_count == 0)
    {
        return -1;
    }

    auto* const wide_result = static_cast<wchar_t*>(calloc(static_cast<size_t>(wide_count), sizeof(wchar_t)));
    if (wide_result == nullptr)
    {
        return -1;
    }

    if (__acrt_GetLocaleInfoEx(locale_name, locale_type, wide_result, wide_count) == 0)
    {
        free(wide_result);
        return -1;
    }

    if (lc_type == LC_WSTR_TYPE)
    {
        *static_cast<wchar_t**>(void_result) = wide_result;
        return 0;
    }

    if (lc_type == LC_STR_TYPE)
    {
        const int char_count = __acrt_WideCharToMultiByte(
            CP_UTF8, 0, wide_result, -1, nullptr, 0, nullptr, nullptr);
        if (char_count == 0)
        {
            free(wide_result);
            return -1;
        }

        auto* const char_result = static_cast<char*>(calloc(static_cast<size_t>(char_count), sizeof(char)));
        if (char_result == nullptr)
        {
            free(wide_result);
            return -1;
        }

        const int converted = __acrt_WideCharToMultiByte(
            CP_UTF8, 0, wide_result, -1, char_result, char_count, nullptr, nullptr);
        free(wide_result);
        if (converted == 0)
        {
            free(char_result);
            return -1;
        }

        *static_cast<char**>(void_result) = char_result;
        return 0;
    }

    free(wide_result);
    return -1;
}

extern "C" int __cdecl __acrt_LCMapStringA(_locale_t const plocinfo, PCWSTR const LocaleName, DWORD const dwMapFlags,
                                           PCCH const lpSrcStr, int const cchSrc, PCH const lpDestStr,
                                           int const cchDest, int const code_page, BOOL const bError)
{
    UNREFERENCED_PARAMETER(plocinfo);
    UNREFERENCED_PARAMETER(LocaleName);
    UNREFERENCED_PARAMETER(code_page);
    UNREFERENCED_PARAMETER(bError);

    if (lpSrcStr == nullptr || cchSrc < -1 || cchDest < 0)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }

    int count = cchSrc;
    if (count == -1)
    {
        count = static_cast<int>(strlen(lpSrcStr)) + 1;
    }

    if (lpDestStr == nullptr || cchDest == 0)
    {
        return count;
    }

    if (cchDest < count)
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }

    for (int index = 0; index < count; ++index)
    {
        unsigned char ch = static_cast<unsigned char>(lpSrcStr[index]);
        if ((dwMapFlags & LCMAP_LOWERCASE) && ch >= 'A' && ch <= 'Z')
        {
            ch = static_cast<unsigned char>(ch - 'A' + 'a');
        }
        else if ((dwMapFlags & LCMAP_UPPERCASE) && ch >= 'a' && ch <= 'z')
        {
            ch = static_cast<unsigned char>(ch - 'a' + 'A');
        }

        lpDestStr[index] = static_cast<char>(ch);
    }

    return count;
}

extern "C" int WINAPI __acrt_MessageBoxA(
    HWND   const hwnd,
    LPCSTR const text,
    LPCSTR const caption,
    UINT   const type
    )
{
    CRTSYS_DIAGNOSTIC_BREAK();
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return 0;
    // abort(); // No fallback; callers should check availablility before calling
}

extern "C" int WINAPI __acrt_MessageBoxW(
    HWND    const hwnd,
    LPCWSTR const text,
    LPCWSTR const caption,
    UINT    const type
    )
{
    CRTSYS_DIAGNOSTIC_BREAK();
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return 0;
    // abort(); // No fallback; callers should check availablility before calling
}

extern "C" BOOLEAN WINAPI __acrt_RtlGenRandom(
    PVOID const buffer,
    ULONG const buffer_count
    )
{
    return SystemFunction036(buffer, buffer_count);
}

extern "C" bool __cdecl __acrt_can_use_vista_locale_apis()
{
    return true;
}

extern "C" bool __cdecl __acrt_initialize_winapi_thunks()
{
    return true;
}

extern "C" HWND __cdecl __acrt_get_parent_window()
{
    return nullptr;
}

extern "C" bool __cdecl __acrt_is_interactive()
{
    return true;
}

extern "C" bool __cdecl __acrt_can_show_message_box()
{
    return false;
}

//
// unexported sources
//
void __cdecl __acrt_initialize_user_matherr(void* encoded_null)
{
}

extern "C" process_end_policy __cdecl __acrt_get_process_end_policy(void)
{
    return process_end_policy_terminate_process;
}

extern "C" developer_information_policy __cdecl __acrt_get_developer_information_policy(void)
{
    return developer_information_policy_none;
}

extern "C" windowing_model_policy __cdecl __acrt_get_windowing_model_policy(void)
{
    return windowing_model_policy_none;
}
