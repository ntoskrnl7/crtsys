#include <malloc.h>
#include <stdarg.h>
#include <stdio.h>

// _vscprintf 사용 시, libcntpr.lib(output.obj)에서 __lookuptable_s를 참조하게 되어 __lookuptable_s가 필요합니다.
const unsigned char __lookuptable_s[] = {
    /* ' ' */ 0x06,
    /* '!' */ 0x80,
    /* '"' */ 0x80,
    /* '#' */ 0x86,
    /* '$' */ 0x80,
    /* '%' */ 0x81,
    /* '&' */ 0x80,
    /* ''' */ 0x00,
    /* '(' */ 0x00,
    /* ')' */ 0x10,
    /* '*' */ 0x03,
    /* '+' */ 0x86,
    /* ',' */ 0x80,
    /* '-' */ 0x86,
    /* '.' */ 0x82,
    /* '/' */ 0x80,
    /* '0' */ 0x14,
    /* '1' */ 0x05,
    /* '2' */ 0x05,
    /* '3' */ 0x45,
    /* '4' */ 0x45,
    /* '5' */ 0x45,
    /* '6' */ 0x85,
    /* '7' */ 0x85,
    /* '8' */ 0x85,
    /* '9' */ 0x05,
    /* ':' */ 0x00,
    /* ';' */ 0x00,
    /* '<' */ 0x30,
    /* '=' */ 0x30,
    /* '>' */ 0x80,
    /* '?' */ 0x50,
    /* '@' */ 0x80,
    /* 'A' */ 0x80, // Disable %A format
    /* 'B' */ 0x00,
    /* 'C' */ 0x08,
    /* 'D' */ 0x00,
    /* 'E' */ 0x28,
    /* 'F' */ 0x27,
    /* 'G' */ 0x38,
    /* 'H' */ 0x50,
    /* 'I' */ 0x57,
    /* 'J' */ 0x80,
    /* 'K' */ 0x00,
    /* 'L' */ 0x07,
    /* 'M' */ 0x00,
    /* 'N' */ 0x37,
    /* 'O' */ 0x30,
    /* 'P' */ 0x30,
    /* 'Q' */ 0x50,
    /* 'R' */ 0x50,
    /* 'S' */ 0x88,
    /* 'T' */ 0x00,
    /* 'U' */ 0x00,
    /* 'V' */ 0x00,
    /* 'W' */ 0x20,
    /* 'X' */ 0x28,
    /* 'Y' */ 0x80,
    /* 'Z' */ 0x88,
    /* '[' */ 0x80,
    /* '\' */ 0x80,
    /* ']' */ 0x00,
    /* '^' */ 0x00,
    /* '_' */ 0x00,
    /* '`' */ 0x60,
    /* 'a' */ 0x60, // Disable %a format
    /* 'b' */ 0x60,
    /* 'c' */ 0x68,
    /* 'd' */ 0x68,
    /* 'e' */ 0x68,
    /* 'f' */ 0x08,
    /* 'g' */ 0x08,
    /* 'h' */ 0x07,
    /* 'i' */ 0x78,
    /* 'j' */ 0x70,
    /* 'k' */ 0x70,
    /* 'l' */ 0x77,
    /* 'm' */ 0x70,
    /* 'n' */ 0x70,
    /* 'o' */ 0x08,
    /* 'p' */ 0x08,
    /* 'q' */ 0x00,
    /* 'r' */ 0x00,
    /* 's' */ 0x08,
    /* 't' */ 0x00,
    /* 'u' */ 0x08,
    /* 'v' */ 0x00,
    /* 'w' */ 0x07,
    /* 'x' */ 0x08};

_Check_return_opt_ _CRT_STDIO_INLINE int __CRTDECL vfprintf(_Inout_ FILE *const _Stream,
                                                            _In_z_ _Printf_format_string_ char const *const _Format,
                                                            va_list _ArgList)
{
    int size = _vscprintf(_Format, _ArgList) + 1;
    if (size == 0)
        return 0;

    char *buffer = (char *)malloc(size);
    if (!buffer)
        return 0;

    size = vsprintf_s(buffer, size, _Format, _ArgList);

    if (fwrite(buffer, size, 1, _Stream) < (size_t)size)
        size = -1;

    free(buffer);

    return size;
}

_Check_return_opt_ _CRT_STDIO_INLINE int __CRTDECL fprintf(_Inout_ FILE *const _Stream,
                                                           _In_z_ _Printf_format_string_ char const *const _Format, ...)
{
    va_list args;
    va_start(args, _Format);
    return vfprintf(_Stream, _Format, args);
}

_Check_return_opt_ _CRT_STDIO_INLINE int __CRTDECL printf(_In_z_ _Printf_format_string_ char const *const _Format, ...)
{
    va_list args;
    va_start(args, _Format);
    return vfprintf(stdout, _Format, args);
}

_Check_return_opt_ _ACRTIMP int __cdecl puts(_In_z_ char const *_Buffer)
{
    return printf(_Buffer);
}