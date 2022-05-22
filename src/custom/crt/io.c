#include <stdio.h>

int __cdecl _creat(char const *const path, int const pmode);
_ACRTIMP int __cdecl creat(_In_z_ char const *_FileName, _In_ int _PermissionMode)
{
    return _creat(_FileName, _PermissionMode);
}

FILE *__cdecl _fdopen(int const fh, char const *const mode);
_Check_return_ _ACRTIMP FILE *__cdecl fdopen(_In_ int _FileHandle, _In_z_ char const *_Mode)
{
    return _fdopen(_FileHandle, _Mode);
}

int __cdecl _read(int const fh, void *const buffer, unsigned const buffer_size);
_Check_return_ _CRT_NONSTDC_DEPRECATE(_read) _ACRTIMP
    int __cdecl read(int const fh, void *const buffer, unsigned const buffer_size)
{
    return _read(fh, buffer, buffer_size);
}

int __cdecl _write(int const fh, void const *const buffer, unsigned const size);
_Check_return_ _CRT_NONSTDC_DEPRECATE(_write) _ACRTIMP
    int __cdecl write(_In_ int _FileHandle, _In_reads_bytes_(_MaxCharCount) void const *_Buf,
                      _In_ unsigned int _MaxCharCount)
{
    return _write(_FileHandle, _Buf, _MaxCharCount);
}

int __cdecl _close(int const fh);
_Check_return_opt_ _CRT_NONSTDC_DEPRECATE(_close) _ACRTIMP int __cdecl close(_In_ int _FileHandle)
{
    return _close(_FileHandle);
}

int __cdecl _dup(int const fh);
_Check_return_ _CRT_NONSTDC_DEPRECATE(_dup) _ACRTIMP int __cdecl dup(_In_ int _FileHandle)
{
    return _dup(_FileHandle);
}

int __cdecl _dup2(int const source_fh, int const target_fh);
_Check_return_ _CRT_NONSTDC_DEPRECATE(_dup2) _ACRTIMP int __cdecl dup2(_In_ int _FileHandleSrc, _In_ int _FileHandleDst)
{
    return _dup2(_FileHandleSrc, _FileHandleDst);
}