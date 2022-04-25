#pragma once

#include <internal_shared.h>

#define EHTRACE_ENTER_FMT1(...)
#define EHTRACE_ENTER_FMT2(...)
#define EHTRACE_FMT1(...)
#define EHTRACE_FMT2(...)

#define EHTRACE_ENTER
#define EHTRACE_EXIT
#define EHTRACE_EXCEPT(x) x
#define EHTRACE_HANDLER_EXIT(x)
#define EHTRACE_SAVE_LEVEL
#define EHTRACE_RESTORE_LEVEL(x)

#define EHTRACE_RESET

#define _VCRT_VERIFY(x)

//
// frame.cpp
//
// intptr_t continuationIndex = reinterpret_cast<intptr_t>(continuationAddress);
//
// to
//
// uintptr_t continuationIndex = reinterpret_cast<uintptr_t>(continuationAddress);
//
#define intptr_t    uintptr_t

#if !defined(DASSERT)
#if !defined(_VCRT_BUILD) && defined(_DEBUG)
#include <assert.h>
#define DASSERT(x) assert(x)
#else
#define DASSERT(x)
#endif
#else
// This is an FE build; DASSERT is defined properly
#endif