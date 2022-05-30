#pragma once

//
// Windows SDK 10.0.17763.0, Windows Driver Kit 10.0.22000.0를 사용하여 빌드 하는 경우
// Windows SDK 10.0.17763.0의 ntdef.h
//
// 이 프로젝트는 User Mode의 헤더를 우선 참조하기 위해서 
// $(VC_IncludePath);$(WindowsSDK_IncludePath)가 WDK의 헤더보다 먼저 참조하도록 설정하기때문에
// Include\10.0.22000.0\shared보다 Include\10.0.17763.0\shared를 먼저 참조합니다.
//
// 이때 아래 문제가 발생하였습니다.
//    1. Include\10.0.17763.0\shared\sdkddkver.h에 NTDDI_WIN10_FE, NTDDI_WIN10_CO가 정의되있지 않아서
//       Include\10.0.22000.0\km\wdm.h를 사용한 소스를 빌드할때 NTDDI_WIN10_FE, NTDDI_WIN10_CO아 undefined되어있는것 때문에 빌드 오류가 발생합니다.
//    2. Include\10.0.17763.0\shared\ntdef.h에 PUTF8_STRING, DECLSPEC_RESTRICT, ENCLAVE_SHORT_ID_LENGTH가 정의되어있지 않아서
//       Include\10.0.22000.0\km\wdm.h를 사용한 소스를 빌드할때 PUTF8_STRING, DECLSPEC_RESTRICT, ENCLAVE_SHORT_ID_LENGTH 관련 부분에서 빌드 오류가 발생합니다.
// 
// 그래서 위 부분을 Include\10.0.22000.0\shared\sdkddkver.h와 ntdef.h의 해당 부분을 복사하여, 이곳에 정의하였습니다.
//
// 추후 다른 부분에서 문제가 발생한다면, 여기에 정의하시기 바랍니다.
// 
// 그리고 되도록 SDK와 WDK의 버전이 같은 환경에서 빌드하는것을 권장합니다.
//

//
// Include\10.0.17763.0\shared\ntdef.h
//
#if !defined(_68K_) && !defined(_MPPC_) && !defined(_X86_) && !defined(_IA64_) && !defined(_AMD64_) && !defined(_ARM_) && !defined(_ARM64_) && defined(_M_IX86)
#define _X86_
#if !defined(_CHPE_X86_ARM64_) && defined(_M_HYBRID)
#define _CHPE_X86_ARM64_
#endif
#endif

#if !defined(_68K_) && !defined(_MPPC_) && !defined(_X86_) && !defined(_IA64_) && !defined(_AMD64_) && !defined(_ARM_) && !defined(_ARM64_) && defined(_M_AMD64)
#define _AMD64_
#endif

#if !defined(_68K_) && !defined(_MPPC_) && !defined(_X86_) && !defined(_IA64_) && !defined(_AMD64_) && !defined(_ARM_) && !defined(_ARM64_) && defined(_M_ARM)
#define _ARM_
#endif

#if !defined(_68K_) && !defined(_MPPC_) && !defined(_X86_) && !defined(_IA64_) && !defined(_AMD64_) && !defined(_ARM_) && !defined(_ARM64_) && defined(_M_ARM64)
#define _ARM64_
#endif

#if !defined(_68K_) && !defined(_MPPC_) && !defined(_X86_) && !defined(_IA64_) && !defined(_AMD64_) && !defined(_ARM_) && !defined(_ARM64_) && defined(_M_M68K)
#define _68K_
#endif

#if !defined(_68K_) && !defined(_MPPC_) && !defined(_X86_) && !defined(_IA64_) && !defined(_AMD64_) && !defined(_ARM_) && !defined(_ARM64_) && defined(_M_MPPC)
#define _MPPC_
#endif

#if !defined(_68K_) && !defined(_MPPC_) && !defined(_X86_) && !defined(_M_IX86) && !defined(_AMD64_) && !defined(_ARM_) && !defined(_ARM64_) && defined(_M_IA64)
#if !defined(_IA64_)
#define _IA64_
#endif /* !_IA64_ */
#endif

#include <ntdef.h>
typedef PSTRING PUTF8_STRING;

#ifndef DECLSPEC_RESTRICT
#if (_MSC_VER >= 1915) && !defined(MIDL_PASS)
#define DECLSPEC_RESTRICT   __declspec(restrict)
#else
#define DECLSPEC_RESTRICT
#endif
#endif

//
// Enclave ID definitions
//
#ifndef ENCLAVE_SHORT_ID_LENGTH
#define ENCLAVE_SHORT_ID_LENGTH             16
#endif
#ifndef ENCLAVE_LONG_ID_LENGTH
#define ENCLAVE_LONG_ID_LENGTH              32
#endif

//
// Include\10.0.17763.0\shared\sdkddkver.h
//
#ifndef NTDDI_WIN10_FE
#define NTDDI_WIN10_FE                      0x0A00000A
#endif
#ifndef NTDDI_WIN10_CO
#define NTDDI_WIN10_CO                      0x0A00000B
#endif
