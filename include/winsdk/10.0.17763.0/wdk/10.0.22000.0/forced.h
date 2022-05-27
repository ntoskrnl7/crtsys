#pragma once

#include <ntdef.h>
typedef PSTRING PUTF8_STRING;

#ifndef DECLSPEC_RESTRICT
#if (_MSC_VER >= 1915) && !defined(MIDL_PASS)
#define DECLSPEC_RESTRICT   __declspec(restrict)
#else
#define DECLSPEC_RESTRICT
#endif
#endif

#ifndef NTDDI_WIN10_FE
#define NTDDI_WIN10_FE                      0x0A00000A
#endif
#ifndef NTDDI_WIN10_FE
#define NTDDI_WIN10_CO                      0x0A00000B
#endif
#ifndef ENCLAVE_SHORT_ID_LENGTH
#define ENCLAVE_SHORT_ID_LENGTH             16
#endif
#ifndef ENCLAVE_LONG_ID_LENGTH
#define ENCLAVE_LONG_ID_LENGTH              32
#endif