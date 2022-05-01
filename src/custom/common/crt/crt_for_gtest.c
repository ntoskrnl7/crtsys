#include <stdio.h>
#include <sys/stat.h>
#include <time.h>
#include <wdm.h>

#pragma warning(disable : 4100)

_ACRTIMP
int __cdecl creat(_In_z_ char const *_FileName, _In_ int _PermissionMode)
{
    KdBreakPoint();
    return 0;
}

_Check_return_ _ACRTIMP int __cdecl _mkdir(_In_z_ char const *_Path)
{
    KdBreakPoint();
    return 0;
}

_Success_(return != 0) _Check_return_ _Ret_maybenull_z_ _ACRTIMP _CRTALLOCATOR
    char *__cdecl _getcwd(_Out_writes_opt_z_(_SizeInBytes) char *_DstBuf, _In_ int _SizeInBytes)
{
    // untested :-(
    RtlCopyMemory(_DstBuf, "%SystemRoot%", sizeof("%SystemRoot%"));
    return _DstBuf;
}
_Check_return_ _ACRTIMP FILE *__cdecl fdopen(_In_ int _FileHandle, _In_z_ char const *_Mode)
{

    KdBreakPoint();
    return NULL;
}

_Check_return_ _ACRTIMP int __cdecl _fileno(_In_ FILE *_Stream)
{
    KdBreakPoint();
    return 0;
}

_Check_return_opt_ _CRT_NONSTDC_DEPRECATE(_close)
_ACRTIMP
int __cdecl close(_In_ int _FileHandle)
{
    KdBreakPoint();
    return 0;
}

_Check_return_ _CRT_NONSTDC_DEPRECATE(_write)
_ACRTIMP
int __cdecl write(_In_ int _FileHandle, _In_reads_bytes_(_MaxCharCount) void const *_Buf,
                  _In_ unsigned int _MaxCharCount)
{
    KdBreakPoint();
    return 0;
}

_Check_return_wat_ _ACRTIMP errno_t __cdecl _localtime64_s(_Out_ struct tm *_Tm, _In_ __time64_t const *_Time)
{
    KdBreakPoint();
    return 0;
}

_Check_return_ _CRT_NONSTDC_DEPRECATE(_dup)
_ACRTIMP
int __cdecl dup(_In_ int _FileHandle)
{
    KdBreakPoint();
    return 0;
}

_Check_return_ _CRT_NONSTDC_DEPRECATE(_dup2)
_ACRTIMP
int __cdecl dup2(_In_ int _FileHandleSrc, _In_ int _FileHandleDst)
{
    KdBreakPoint();
    return 0;
}

_Check_return_ _ACRTIMP int __cdecl _isatty(_In_ int _FileHandle)
{
    KdBreakPoint();
    return 0;
}

_ACRTIMP int __cdecl _stat64i32(_In_z_ char const *_FileName, _Out_ struct _stat64i32 *_Stat)
{

    KdBreakPoint();
    return 0;
}
_ACRTIMP int __cdecl _open_osfhandle(_In_ intptr_t _OSFileHandle, _In_ int _Flags)
{
    KdBreakPoint();
    return 0;
}

#if UCXXRT
_ACRTIMP __declspec(noreturn) void __cdecl exit(_In_ int _Code)
{
}
_ACRTIMP __declspec(noreturn) void __cdecl _exit(_In_ int _Code)
{
}
#endif

#if _MSC_VER >= 1920
_Check_return_ _ACRTIMP short __cdecl _dtest(_In_ double *_Px);
#pragma function(_dtest)
#endif
_Check_return_ _ACRTIMP short __cdecl _dtest(_In_ double *_Px)
{
    KdBreakPoint();
    return 0;
}

#if _MSC_VER >= 1920
_Check_return_ _ACRTIMP short __cdecl _dclass(_In_ double _X);
#pragma function(_dclass)
#endif
_Check_return_ _ACRTIMP short __cdecl _dclass(_In_ double _X)
{
    return 0;
}

int __do_unsigned_char_lconv_initialization = 255;

_Check_return_ _ACRTIMP double __cdecl nextafter(_In_ double _X, _In_ double _Y)
{
    KdBreakPoint();
    return 0;
}

_ACRTIMP unsigned int __cdecl _set_abort_behavior(_In_ unsigned int _Flags, _In_ unsigned int _Mask)
{
    // untested :-(
    return 0;
}

_Check_return_opt_ _ACRTIMP int __cdecl _set_error_mode(_In_ int _Mode)
{
    // untested :-(
    return 0;
}
_Check_return_ _ACRTIMP unsigned long long __cdecl strtoull(_In_z_ char const *_String,
                                                            _Out_opt_ _Deref_post_z_ char **_EndPtr, _In_ int _Radix)
{
    KdBreakPoint();
    return 0;
}

_Check_return_ _DCRTIMP char *__cdecl getenv(_In_z_ char const *_VarName)
{
    // untested :-(
    return NULL;
}

_Ret_z_ _Check_return_ _ACRTIMP char *__cdecl strerror(_In_ int _ErrorMessage)
{
    KdBreakPoint();
    return NULL;
}

__declspec(noalias) void __cdecl __std_reverse_trivially_swappable_4(void *_First, void *_Last)
{
}

_Check_return_ _ACRTIMP FILE *__cdecl _wfopen(_In_z_ wchar_t const *_FileName, _In_z_ wchar_t const *_Mode)
{
    KdBreakPoint();
    return NULL;
}

_Success_(return != -1) _Check_return_ _ACRTIMP long __cdecl ftell(_Inout_ FILE *_Stream)
{
    KdBreakPoint();
    return 0;
}
_ACRTIMP int __cdecl remove(_In_z_ char const *_FileName)
{
    KdBreakPoint();
    return 0;
}