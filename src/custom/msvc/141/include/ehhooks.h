#pragma once

#include <vcruntime_internal.h>
#include <trnsctrl.h>
#include <ehdata4.h>
#include <ehdata4_export.h>

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

#define __pSETranslator   (_se_translator_function&)(__vcrt_getptd()->_translator)
