#include <minwindef.h>

#define FLG_APPLICATION_VERIFIER    0x0100

typedef struct _LDK_PEB {
	ULONG NtGlobalFlag;
} LDK_PEB, *PLDK_PEB;

