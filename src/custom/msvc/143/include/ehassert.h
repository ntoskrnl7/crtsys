#pragma once

#include <windows.h>

#ifdef _DEBUG
int dprintf( char *, ... );
#define DASSERT(c)  ((c)?0: \
                      (DbgPrint("Runtime internal error (%s, line %d)", __FILE__, __LINE__),\
                      terminate()))

#define DEXPECT(c)	((c)?0: \
                      DbgPrint("Runtime internal error suspected (%s, line %d)", __FILE__, __LINE__))
#else
#define dprintf
#define DASSERT(c)  (c)
#define DEXPECT(c)  (c)
#endif

#define EHTRACE_RESET
#define EHTRACE_SAVE_LEVEL
#define EHTRACE_RESTORE_LEVEL(x)
#define EHTRACE_ENTER
#define EHTRACE_ENTER_MSG(x)
#define EHTRACE_ENTER_FMT1(x,y)
#define EHTRACE_ENTER_FMT2(x,y,z)
#define EHTRACE_MSG(x)
#define EHTRACE_FMT1(x,y)
#define EHTRACE_FMT2(x,y,z)
#define EHTRACE_EXCEPT(x)       (x)
#define EHTRACE_EXIT
#define EHTRACE_HANDLER_EXIT(x)

// ???
#define _VCRT_VERIFY(x)

// #pragma once

// #include <internal_shared.h>
// #include <ehhooks.h>

// #if !defined(RENAME_BASE_PTD)
// #if defined _VCRT_SAT_1
// #define RENAME_BASE_PTD(x) x##base
// #else
// #define RENAME_BASE_PTD(x) x
// #endif
// #endif
