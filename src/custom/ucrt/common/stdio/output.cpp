#include "../../../common/crt/crt_internal.h"
#include <stdlib.h>

const char __lookuptable[] = {
    0x06, 0x00, 0x00, 0x06, 0x00, 0x01, 0x00, 0x00, 0x10, 0x00, 0x03, 0x06, 0x00, 0x06, 0x02,
    0x10, 0x04, 0x45, 0x45, 0x45, 0x05, 0x05, 0x05, 0x05, 0x05, 0x35, 0x30, 0x00, 0x50, 0x00,
    0x00, 0x00, 0x00, 0x20, 0x28, 0x38, 0x50, 0x58, 0x07, 0x08, 0x00, 0x37, 0x30, 0x30, 0x57,
    0x50, 0x07, 0x00, 0x00, 0x20, 0x20, 0x08, 0x00, 0x00, 0x00, 0x00, 0x08, 0x60, 0x68, /* 'Z' (extension for NT
                                                                                           development) */
    0x60, 0x60, 0x60, 0x60, 0x00, 0x00, 0x70, 0x70, 0x78, 0x78, 0x78, 0x78, 0x08, 0x07, 0x08,
    0x00, 0x00, 0x07, 0x00, 0x08, 0x08, 0x08, 0x00, 0x00, 0x08, 0x00, 0x08, 0x00, 0x07, /* 'w' (extension for NT
                                                                                           development) */
    0x08};

#define find_char_class(c) ((c) < ' ' || (c) > 'x' ? CH_OTHER : (CHARTYPE)(__lookuptable[(c) - ' '] & 0xF))

#define find_next_state(class, state) (STATE)(__lookuptable[(class) * NUMSTATES + (state)] >> 4)

enum STATE
{
    ST_NORMAL,  /* normal state; outputting literal chars */
    ST_PERCENT, /* just read '%' */
    ST_FLAG,    /* just read flag character */
    ST_WIDTH,   /* just read width specifier */
    ST_DOT,     /* just read '.' */
    ST_PRECIS,  /* just read precision specifier */
    ST_SIZE,    /* just read size specifier */
    ST_TYPE     /* just read type specifier */
};
#define NUMSTATES (ST_TYPE + 1)

enum CHARTYPE
{
    CH_OTHER,   /* character with no special meaning */
    CH_PERCENT, /* '%' */
    CH_DOT,     /* '.' */
    CH_STAR,    /* '*' */
    CH_ZERO,    /* '0' */
    CH_DIGIT,   /* '1'..'9' */
    CH_FLAG,    /* ' ', '+', '-', '#' */
    CH_SIZE,    /* 'h', 'l', 'L', 'N', 'F', 'w' */
    CH_TYPE     /* type specifying character */
};

BOOLEAN
CrtSyspIsFloatingPointOnly(_In_z_ _Printf_format_string_ PCSTR Format)
{
    bool ret = false;
    char ch;
    enum CHARTYPE chclass;
    enum STATE state = ST_NORMAL;
    while ((ch = *Format++) != '\0')
    {
        chclass = find_char_class(ch);
        state = find_next_state(chclass, state);
        switch (state)
        {
        case ST_TYPE:
            switch (ch)
            {
            case 'E':
            case 'G':
            case 'e':
            case 'f':
            case 'g':
                ret = true;
                break;
            default:
                return false;
            }
            break;
        }
    }
    return ret;
}

#include <math.h>
#include <stdint.h>

EXTERN_C
_Success_(return >= 0) _Check_return_opt_
    int CrtSysVPrintf(_Out_writes_(_BufferCount) _Always_(_Post_z_) char *const _Buffer, _In_ size_t const _BufferCount,
                      _In_z_ _Printf_format_string_ char const *const _Format, va_list _ArgList)
{
    if (CrtSyspIsFloatingPointOnly(_Format))
    {
        char ch;
        enum CHARTYPE chclass;
        enum STATE state = ST_NORMAL;
        const char *format = _Format;
        int precision = -1;
        while ((ch = *format++) != '\0')
        {
            chclass = find_char_class(ch);
            state = find_next_state(chclass, state);
            switch (state)
            {
            case ST_PRECIS:
                if (ch == '*')
                {
                    precision = va_arg(_ArgList, int);
                    if (precision < 0)
                        precision = -1;
                }
                else
                {
                    precision = precision * 10 + (ch - '0');
                }
                break;
            case ST_TYPE:
                switch (ch)
                {
                case 'E':
                case 'G':
                case 'e':
                case 'f':
                case 'g': {
                    double val = va_arg(_ArgList, double);
                    XSTATE_SAVE state_save;
                    auto features = RtlGetEnabledExtendedFeatures(XSTATE_MASK_LEGACY);
                    if (!FlagOn(features, XSTATE_MASK_LEGACY))
                    {
                        KdBreakPoint();
                        return -1;
                    }
                    KeSaveExtendedProcessorState(XSTATE_MASK_LEGACY, &state_save);
                    __try
                    {
                        __try
                        {
                            int64_t int_part = (int64_t)val;
                            double flt_part = (double)(val - int_part);
                            if (precision < 0)
                                precision = 6;
                            if (_i64toa_s(int_part, _Buffer, _BufferCount, 10) != 0)
                                return -1;
                            size_t offset = strlen(_Buffer);
                            if (offset + precision > _BufferCount)
                                return -1;
                            _Buffer[offset] = '.';
                            if (_i64toa_s(_abs64((int64_t)(flt_part * pow(10, precision))), &_Buffer[offset + 1],
                                          _BufferCount, 10) != 0)
                                return -1;
                            return (int)strlen(_Buffer);
                        }
                        __except (EXCEPTION_EXECUTE_HANDLER)
                        {
                            ASSERT(GetExceptionCode() == EXCEPTION_FLT_INEXACT_RESULT);
                            KdBreakPoint();
                        }
                    }
                    __finally
                    {
                        KeRestoreExtendedProcessorState(&state_save);
                    }
                }
                }
                break;
            }
        }
        return 0;
    }
    return _vsnprintf_s(_Buffer, _BufferCount, _TRUNCATE, _Format, _ArgList);
}

EXTERN_C _Success_(return >= 0) _Check_return_opt_ _CRT_STDIO_INLINE int __CRTDECL
    vsprintf_s(_Out_writes_(_BufferCount) _Always_(_Post_z_) char *const _Buffer, _In_ size_t const _BufferCount,
               _In_z_ _Printf_format_string_ char const *const _Format, va_list _ArgList)
{
    int len = _vsnprintf_s(_Buffer, _BufferCount, _TRUNCATE, _Format, _ArgList);
    if (len != -1)
        return len;
    return CrtSysVPrintf(_Buffer, _BufferCount, _Format, _ArgList);
}

EXTERN_C _Success_(return >= 0) _Check_return_opt_ _CRT_STDIO_INLINE int __CRTDECL
    sprintf_s(_Out_writes_(_BufferCount) _Always_(_Post_z_) char *const _Buffer, _In_ size_t const _BufferCount,
              _In_z_ _Printf_format_string_ char const *const _Format, ...)
{
    va_list args;
    va_start(args, _Format);
    return vsprintf_s(_Buffer, _BufferCount, _Format, args);
}