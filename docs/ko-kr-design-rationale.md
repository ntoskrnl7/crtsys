# 설계 근거와 운영 경계

[한국어 문서로 돌아가기](./ko-kr.md)

이 문서는 `crtsys`가 어떤 프로젝트인지, 어떤 실행 문맥을 기준으로
설계되었는지, 그리고 어디부터는 일반 WDK 규칙을 그대로 의식해야 하는지를
정리합니다.

## 위치

`crtsys`는 Windows 드라이버를 위한 커널 모드 C++ 런타임 기반 계층입니다.
MSVC C++ runtime, CRT, STL, helper library 지원을 LDK 기반
Windows/NTDLL-compatible API 및 ICU ABI layer와 결합하고, 그 결과로 얻은
driver-tested surface와 execution-context contract를 문서화합니다.

실제 목표는 다음과 같습니다.

```text
MSVC C++ / CRT / STL driver path에 필요한 런타임 기반을 제공하고,
부족한 의존성을 커널 primitive 위에 매핑하며,
coverage surface를 명시적으로 문서화하고 테스트한다.
```

이 구분은 중요합니다. `crtsys`는 알려진 driver/runtime path를 위한 runtime
substrate 및 Windows API compatibility layer이지, 임의의 user-mode 가정을
커널 모드로 그대로 가져와도 된다는 허가가 아닙니다. driver는 여전히 IRQL,
stack, pool allocation, pageable code, unload safety, verifier, HVCI,
target OS validation 같은 WDK 규칙을 따라야 합니다.

## 왜 필요한가

커널 모드에서도 C++ 언어 자체는 사용할 수 있지만, 일반 MSVC 런타임 환경은
대부분 없습니다. 실제 드라이버에서 현대적인 C++을 쓰려면 다음 지원이
필요해집니다.

- static/dynamic initialization
- 선택된 exception runtime 경로
- STL 코드가 요구하는 일부 CRT 함수
- 통제된 문맥에서 쓰는 STL synchronization/threading 경로
- stream 및 diagnostic 지원
- driver object와 lock을 감싸는 RAII wrapper

공통 런타임 계층이 없으면 각 드라이버 프로젝트가 같은 저수준 runtime
adaptation 작업을 반복해서 만들어야 합니다. `crtsys`는 그 작업을 한곳에
모으고 테스트와 연결합니다.

## 계층 구조

driver source에서 kernel primitive까지 내려가는 의도한 구조는 다음과
같습니다.

```text
Driver source (.sys)
        |
        v
MSVC C++ runtime / CRT / STL API + NTL helper
        |
        v
crtsys compatibility layer
  - 선택된 Microsoft CRT/STL/VCRT/UCRT source path
  - kernel-mode runtime adapter 및 ABI helper
  - driver-run coverage 및 IRQL contract
        |
        v
LDK Windows/NTDLL-compatible API + ICU ABI substrate
        |
        v
WDK / NT kernel primitive
```

이 모델에서 `crtsys`는 MSVC runtime/STL integration, kernel-mode runtime
adapter, 테스트된 C++ surface를 담당합니다. `Ldk`는 알려진 runtime
dependency를 위한 kernel-backed Windows/NTDLL-compatible API 및 ICU ABI
substrate로 사용됩니다. 임의의 user-mode module을 커널 공간에서 실행하는
수단으로 설명하지 않습니다.

## 실행 모델

`crtsys`는 주로 드라이버 제어 경로를 대상으로 설계되었습니다.

- driver initialization / unload
- device creation / teardown
- IOCTL 및 RPC request handling
- worker thread coordination
- ownership cleanup 및 error handling
- diagnostic 및 test code

이런 경로는 보통 `PASSIVE_LEVEL`에서 실행되며, underlying WDK API가 허용하는
일부 경우에만 `APC_LEVEL`까지 수용합니다. 이것이 기본 설계 가정입니다.

어떤 기능이나 API가 더 넓은 계약을 명시하지 않는다면 기본값은
`PASSIVE_LEVEL`입니다. C++ abstraction은 allocation, wait, throw, unwind,
blocking lock, pageable code, runtime state 의존성을 숨길 수 있습니다.
컴파일된다는 사실만으로 elevated IRQL에서 안전해지는 것은 아닙니다.

DPC, ISR, paging I/O, spin lock을 잡은 상태, 기타 elevated-IRQL hot path에서
runtime-backed helper를 사용하려면 그 API가 해당 문맥을 명시적으로 문서화하고
테스트해야 합니다.

### 런타임 종료 순서

CRT, STL, LDK 또는 NTL 상태를 소유한 객체는
`CrtSysUninitializeRuntime`이 runtime substrate를 종료하기 전에 파괴해야
합니다. 이 규칙은 정상 unload뿐 아니라 초기화 실패 경로에도 적용됩니다.
특히 `ntl::main`이 실패를 반환하면 NTL entry path는 runtime을 종료하기 전에
`ntl::driver` 상태를 먼저 해제합니다. 이 순서가 뒤바뀌면 C++ container
소멸자가 이미 종료된 LDK heap에 접근할 수 있습니다.

기본적으로 꺼져 있는 `CRTSYS_ENABLE_FAILURE_HARNESS_SELF_TEST` 회귀 경로는
의도적으로 드라이버 초기화를 실패시킵니다. 기대 결과는 모든 C++ 객체를
파괴한 뒤 bugcheck 없이 드라이버 시작이 실패하는 것입니다.

## 동기화 철학

NTL은 주된 드라이버 제어 경로에서 ownership clarity와
blocking/resource-style coordination을 우선합니다. 그래서 `ERESOURCE` 기반
helper가 중심입니다. `PASSIVE_LEVEL` / 일부 `APC_LEVEL` 모델에서는 더 긴
수명의 driver state를 조율하고, underlying WDK contract가 허용하는 경우
blocking을 사용할 수 있기 때문입니다.

Spin lock도 필요합니다. 하지만 runtime-backed C++ 모델의 기본 동기화
abstraction은 아닙니다. Spin lock은 짧고, nonblocking이며, resident이고,
elevated-IRQL-safe인 영역에 써야 합니다. allocation, wait, exception unwind,
stream formatting, pageable code, 임의의 STL/runtime 호출을 보호하는 용도로
쓰면 안 됩니다.

## 기능 사용 가이드

| 영역 | 권장 사용 | 피해야 할 사용 |
| --- | --- | --- |
| RAII wrapper | driver control path의 ownership/cleanup | WDK lifetime rule을 완전히 숨기는 설계 |
| `ntl::status` | 명확한 `NTSTATUS` 흐름 | exception policy 대체물로 취급 |
| `ntl::driver` / `ntl::device` | 초기화, teardown, IOCTL 등록 | lifetime을 모른 채 hot path에 밀어 넣기 |
| `ntl::resource` | 유효한 IRQL에서 blocking state coordination | DPC, ISR, spin-lock-held path |
| `ntl::spin_lock` | 짧은 nonblocking critical region | wait/allocation/unwind 가능 코드 보호 |
| STL container | allocation이 유효한 management path | DPC, ISR, paging I/O, allocation-sensitive path |
| STL threading primitive | 통제된 worker-thread path | unload 이후 살아남을 수 있는 fire-and-forget work |
| Exception | 좁고 테스트된 control path | hot path, elevated IRQL, WDK callback boundary |
| iostream | diagnostic 및 test | production hot path와 stack-sensitive path |
| `ntl::expand_stack` | 깊은 stack 사용이 예상되고 테스트된 경로 | 얕은 call graph 설계를 대체하는 용도 |

## Exception 정책

`crtsys`는 선택된 C++ exception 시나리오를 지원합니다. 이 지원은 커널
드라이버에서 통제된 C++ runtime path를 가능하게 하기 위한 것이지,
driver hot path를 user-mode exception 스타일로 작성하라는 권장이 아닙니다.

권장 경계는 다음과 같습니다.

- hot path, paging I/O, DPC, ISR, spin-lock-held region에서는 exception을
  피합니다.
- 명시적으로 테스트하고 문서화하지 않았다면 exception이 WDK callback
  boundary를 넘어가게 하지 않습니다.
- exception-heavy path는 stack-sensitive path로 취급합니다.
- unwinding 중 실행되는 destructor는 kernel-safe해야 합니다.

## Stack 정책

커널 스택은 작고, C++ runtime 계층은 stack pressure를 늘릴 수 있습니다.
dispatch path는 얕게 유지하고, 큰 local object와 recursive design을 피하며,
production hot path에서는 stream formatting을 피하는 것이 좋습니다.

`ntl::expand_stack`은 stack 사용이 깊어지는 경로를 알고 있고 테스트한 경우에
사용하세요. stack expansion은 통제된 도구이지, stack discipline을 무시해도
된다는 허가가 아닙니다.

## Memory와 HVCI 정책

최종 driver binary는 여전히 driver binary로 검증해야 합니다. Driver
Verifier, Code Integrity check, 배포 요구사항에 따른 Memory Integrity,
관련 HLK 또는 내부 검증 suite로 테스트해야 합니다.

`crtsys`의 안전한 배포 모델은 static runtime compatibility layer입니다.
writable/executable mapping을 만들거나, executable memory를 patch하거나,
임의의 executable image를 load하거나, data file을 executable code처럼
취급하는 설계는 이 모델 밖입니다.

## Non-goals

`crtsys`가 목표로 하지 않는 것은 다음과 같습니다.

- full user-mode CRT compatibility
- full Win32 또는 NTDLL compatibility
- arbitrary user-mode DLL execution in kernel mode
- 모든 STL 기능이 모든 driver context에서 안전하다는 보장
- WDK object ownership, IRQL, pageable-code, pool-allocation rule을 숨기기
- Driver Verifier, HLK, VM test, crash dump analysis 대체
- 테스트하지 않은 Visual Studio, SDK, WDK, Windows, architecture 조합 보장

## 문서화 규칙

`crtsys` 또는 NTL API의 IRQL contract가 문서화되어 있지 않다면 사용자는
`PASSIVE_LEVEL`로 가정해야 합니다.

새 API를 문서화할 때는 다음처럼 명시적인 계약을 쓰는 것이 좋습니다.

- `PASSIVE_LEVEL only`
- `<= APC_LEVEL`
- `<= DISPATCH_LEVEL`
- `DPC-level only`
- `test/diagnostic only`

allocation, blocking, pageable code, exception, stack expansion, runtime
adapter path가 관련되어 있다면 왜 그런 계약이 필요한지도 API 문서에 함께
적어야 합니다.
