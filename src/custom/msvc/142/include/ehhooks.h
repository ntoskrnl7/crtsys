#pragma once

#include <vcruntime_internal.h>

#define __pSETranslator   (_se_translator_function&)(__vcrt_getptd()->_translator)
