//
// MSVC\14.33.31629\include\eh.h
// _VCRT_COMPILER_PREPROCESSOR가 1이면서 _VCRT_BUILD가 정의되어있어야 unexpected 함수가 정의됩니다.
//
#define _VCRT_COMPILER_PREPROCESSOR 1
#define _VCRT_BUILD

#include <eh.h>
#include <ehassert.h> 
#include <ehdata.h>
#include <ehdata4.h>
#include <ehhooks.h>
#include <trnsctrl.h>
#include <vcruntime_exception.h>
#include <vcruntime_typeinfo.h>

#include <Unknwn.h>
#include <Windows.h>
#include "ehhelpers.h"

#if defined(_M_ARM64EC)
#include <vcwininternls.h> //PDISPATCHER_CONTEXT_ARM64EC
#include <winternl.h>
#endif

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

#include <frame.cpp>