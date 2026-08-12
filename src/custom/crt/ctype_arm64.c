// ARM64 WDK 10.0.26100 packages the narrow ctype functions in one
// libcntpr archive member. Some of those functions are also exported by the
// kernel import library, so requesting one of the missing functions would
// otherwise pull the whole member and create duplicate definitions.

#if defined(_M_ARM64)

#undef __isascii
#undef __toascii
#undef isalpha
#undef isalnum
#undef iscntrl
#undef isgraph
#undef ispunct
#undef __iscsymf
#undef __iscsym

static int __cdecl crtsys_is_ascii_alpha(int value)
{
    const unsigned int character = (unsigned int)value;
    return (character - (unsigned int)'A' <=
            (unsigned int)('Z' - 'A')) ||
           (character - (unsigned int)'a' <=
            (unsigned int)('z' - 'a'));
}

static int __cdecl crtsys_is_ascii_digit(int value)
{
    const unsigned int character = (unsigned int)value;
    return character - (unsigned int)'0' <=
           (unsigned int)('9' - '0');
}

int __cdecl __isascii(int value)
{
    return (unsigned int)value <= 0x7fU;
}

int __cdecl __toascii(int value)
{
    return value & 0x7f;
}

int __cdecl isalpha(int value)
{
    return crtsys_is_ascii_alpha(value);
}

int __cdecl isalnum(int value)
{
    return crtsys_is_ascii_alpha(value) ||
           crtsys_is_ascii_digit(value);
}

int __cdecl iscntrl(int value)
{
    const unsigned int character = (unsigned int)value;
    return character < 0x20U || character == 0x7fU;
}

int __cdecl isgraph(int value)
{
    const unsigned int character = (unsigned int)value;
    return character >= 0x21U && character <= 0x7eU;
}

int __cdecl ispunct(int value)
{
    return isgraph(value) && !isalnum(value);
}

int __cdecl __iscsymf(int value)
{
    return value == '_' || crtsys_is_ascii_alpha(value);
}

int __cdecl __iscsym(int value)
{
    return value == '_' || crtsys_is_ascii_alpha(value) ||
           crtsys_is_ascii_digit(value);
}

#endif
