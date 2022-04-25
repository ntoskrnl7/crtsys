#include <vcruntime_internal.h>

extern "C" void __cdecl _initterm(_PVFV* const first, _PVFV* const last)
{
    for (_PVFV* it = first; it != last; ++it)
    {
        if (*it == nullptr)
            continue;

        (**it)();
    }
}

extern "C" int __cdecl _initterm_e(_PIFV* const first, _PIFV* const last)
{
    for (_PIFV* it = first; it != last; ++it)
    {
        if (*it == nullptr)
            continue;

        int const result = (**it)();
        if (result != 0)
            return result;
    }

    return 0;
}
