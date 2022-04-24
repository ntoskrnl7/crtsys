#pragma once

#include <internal_shared.h>
#include <ehhooks.h>

using namespace FH4;

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

#if !defined(RENAME_BASE_PTD)
#if defined _VCRT_SAT_1
#define RENAME_BASE_PTD(x) x##base
#else
#define RENAME_BASE_PTD(x) x
#endif
#endif
