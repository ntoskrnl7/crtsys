// clang-format off

#pragma once

// sdkddkver.h가 ntddk.h(ntdef.h)보다 포함되는 경우가 발생하여,
// DECLSPEC_DEPRECATED_DDK, DECLSPEC_DEPRECATED_DDK_WINXP 등이 정의되지 않아서 문제가 발생하였습니다.
// 그러서 ntdef.h가 미리 

#include <ntdef.h>