# NTL 커널 네트워크 계약 테스트

[English](./README.md)

이 계약 테스트는 dual-runtime 경계를 코드 조각이 아니라 실제 드라이버와 앱으로
보여줍니다. 앱은 bounded HTTP/1, HTTP/2, HTTP/3, gRPC, WebSocket,
WebTransport, QPACK, TLS ClientHello 입력을 고정 크기 IOCTL 계약으로 보냅니다.
드라이버는 같은 `ntl::net` API로 이를 검증하고, caller-owned 출력 버퍼로
`ntl::net::borrowed_transform_pipeline`을 직접 실행하며 versioned explicit-offload
계약도 검증합니다.

IOCTL은 재현 가능한 계약 검증용 전송이며 실제 네트워크 예제가 아닙니다.
실제 네트워크 분류와 커널→사용자 정책 전달은
[`examples/wfp/user/tcp-content-filter`](../../../examples/wfp/user/tcp-content-filter/)와
[`examples/wfp/user/udp-content-filter`](../../../examples/wfp/user/udp-content-filter/)를
이어서 보십시오. 그 예제들은 WFP 수명, backpressure, timeout, fail-closed
처리를 포함합니다.

이 프로젝트를 `test/net` 아래에 두어 합성 IOCTL 전송을 실제 WFP 트래픽을
처리하는 예제로 오해하지 않게 했습니다.

## 커널 스택을 사용하지 않는 workspace

큰 parser scratch와 출력 레코드는 커널 스택에 두지 않습니다. 드라이버가
`ntl::net::kernel::workspace_pool`을 소유하고 각 IOCTL은 작은 move-only lease만
얻습니다.

```cpp
auto acquired = workspaces.try_acquire();
if (!acquired)
  return acquired.status();

auto workspace = std::move(*acquired);
auto status = inspect(input, workspace->reply, workspace->scratch);
```

workspace는 nonpaged lookaside list가 제공하고 매 lease마다 다시 생성되며,
lease가 끝나면 자동으로 반환됩니다. QPACK과 TLS는 같은 bounded scratch를
재사용합니다. 프로토콜 handler도 별도 함수로 나누어 Debug 빌드가 모든 분기의
로컬 변수를 하나의 커널 스택 프레임으로 합치지 못하게 했습니다. 앱이 보는
IOCTL 계약은 그대로이며 호출자가 native pool이나 stack expansion API를 직접
사용할 필요가 없습니다.

## 빌드와 실행

```bat
cmake -S test\net\kernel-contracts -B test\net\kernel-contracts\build_x64 -A x64
cmake --build test\net\kernel-contracts\build_x64 --config Debug
sc create CrtSysNtlNetKernelContracts binpath= "C:\path\to\crtsys_ntl_net_kernel_contract_driver.sys" type= kernel start= demand
sc start CrtSysNtlNetKernelContracts
test\net\kernel-contracts\build_x64\Debug\crtsys_ntl_net_kernel_contract_app.exe
sc stop CrtSysNtlNetKernelContracts
sc delete CrtSysNtlNetKernelContracts
```

정상 출력은 다음과 같습니다.

```text
kernel network core ok: HTTP/1=40 HTTP/2=3 HTTP/3=3 gRPC=3 WebSocket=2 WebTransport=2 QPACK=1 TLS=2 transformed=32 offload=1 executor=1
```

테스트 VM에서 반복 load·실행·unload와 System log의 crash 기록까지 검사하려면:

```powershell
.\Run-NtlNetKernelContracts.ps1 `
  -DriverPath .\build_x64\Debug\crtsys_ntl_net_kernel_contract_driver.sys `
  -AppPath .\build_x64\Debug\crtsys_ntl_net_kernel_contract_app.exe `
  -CrashPostcheckPath ..\..\common\Test-VmCrashPostcheck.ps1 `
  -AllowDisposableGuestMutation `
  -Cycles 20 -IterationsPerCycle 10 `
  -EvidencePath .\ntl-net-kernel-contract-evidence.json
```

저장소의 일반적인 test-signing 구성을 적용한 폐기 가능한 드라이버 테스트 VM에서만
실행하십시오. 명시적인 변경 허용 switch는
`C:\crtsys-disposable-test-guest.sentinel` 파일이 존재하고 내용이 정확히
`CRTSYS_DISPOSABLE_TEST_GUEST`일 때만 받아들입니다. 실행기는 이 sentinel을 만들지
않습니다. 이 스크립트는 재부팅하거나 Verifier 설정을 바꾸지 않습니다.
드라이버를 load하기 전에 현재 System event RecordId와 crash dump 지문을 기록하고,
그 뒤 새로 생긴 crash 증거만 실패로 판정합니다. guest용 패키지에는
`test/common/Test-VmCrashPostcheck.ps1`과
`test/wfp/runtime/common/DisposableGuestGuard.ps1`을 실행기 옆에 복사하십시오.
저장소에서 직접 실행할 때는 실행기가 두 공통 스크립트를 자동으로 찾습니다.
VM에서 `crtsys_ntl_net_kernel_contract_driver.sys`를 Driver Verifier 대상으로
설정하고 직접
재부팅한 뒤에는 `-RequireVerifierTarget`을 추가할 수 있습니다. 대상이 활성화되지
않았으면 suite는 드라이버를 load하기 전에 실패합니다. 이 드라이버를 대상으로
요구하지 않고 설정만 증거에 남기려면 `-CaptureVerifierSettings`를 사용하십시오.
Verifier 조회를 포함해 실행하는 모든 프로세스에는 제한 시간이 있으며 기본값은
30초입니다. 필요하면 `-OperationTimeoutSeconds`로 바꿀 수 있습니다.

suite 실행 중에는 현재 단계, cycle, iteration, 완료한 실행 수가
`ntl-net-kernel-contract-evidence.json.progress.json`에 계속 갱신됩니다. 최종 PASS/FAIL
증거는 별도 JSON으로 남으므로 guest 제어 timeout과 드라이버, 앱, service control,
Verifier timeout을 구분할 수 있습니다.

앱은 HTTP/1·HTTP/2·HTTP/3 framing, gRPC framing, WebSocket unmask,
WebTransport stream-prefix 파싱, static QPACK, TLS ClientHello의 SNI·ALPN 관찰,
직접 content transform, explicit-offload 요청·응답 검증까지 모든 bounded
driver handler를 실행합니다. 0이 아닌 종료 코드는 실패한 단계를 구분합니다.
마지막 요청은 `ntl::net::kernel::executor`에 작업을 post하고 완료를 기다립니다.
unload는 새 작업을 막은 뒤 수락된 executor 작업을 모두 drain합니다.

커널 공개 통합 헤더는 `<ntl/net/kernel/all>`입니다. 현재 계약은 공통 parsing·정책
코어, 직접 커널 실행, explicit-offload, 커널 Schannel credential 획득, 공식 NMR
커널 MsQuic binding을 검증합니다. 수명 단계는 마지막 Schannel credential,
완료된 coroutine frame, WSK/TLS transport chain, MsQuic configuration chain,
workspace lease를 의도적으로 `DISPATCH_LEVEL`에서 해제하며, runtime 소유 정리가
`PASSIVE_LEVEL`에서 끝나야 성공합니다. 명시적 close와 drain은 deterministic
service shutdown을 검증하기 위한 것이며 파괴 순서 요구 사항이 아닙니다.
사용자 모드 `msquic.dll` fallback은 사용하지 않습니다.
