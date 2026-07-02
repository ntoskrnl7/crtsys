#include <Windows.h>

#ifdef _M_ARM64

typedef PVOID(WINAPI* CrtSysLocateXStateFeatureFn)(PCONTEXT Context,
                                                   DWORD FeatureId,
                                                   PDWORD Length);

CrtSysLocateXStateFeatureFn __locateXStateFeature = NULL;
DWORD64 __arm64_xstate_features = 0;

#endif
