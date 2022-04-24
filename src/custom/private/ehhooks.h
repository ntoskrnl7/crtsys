#pragma once

#include <vcruntime_internal.h>
#include <trnsctrl.h>
#include <ehdata4.h>
#include <ehdata4_export.h>
#include <ehassert.h>

#define __pSETranslator   (_se_translator_function&)(__vcrt_getptd()->_translator)
