# 커널 WFP TLS 검사 프록시

[English](./README.md)

실제 커널 TLS 데이터 경로와 제어된 런타임 검증을 분리했습니다.

- `crtsys_wfp_kernel_tls_inspection_proxy.sys`: WSK redirect record와 원래
  목적지 복구, 커널 Schannel, ALPN, bounded HTTP/1.1·HTTP/2 처리,
  요청·응답 변환, 차단, capture telemetry
- `crtsys_wfp_kernel_tls_inspection_proxy_controller.exe`: 단기 인증서 준비,
  driver 설정, 임시 WFP 정책, 원시 counter/capture 통계
- `crtsys_wfp_kernel_tls_inspection_proxy_acceptance.exe`: 별도 디렉터리
  `test/wfp/runtime/fixtures/kernel/tls-inspection-proxy`의 origin/client,
  비정상·idle 입력, 판정, PASS 표시

fixture에는 WFP 관리, driver IOCTL, 서비스 제어 호출이 없습니다. IPC는
`ready → 정책 제거 요청 → 제거 확인 → direct 연결 증명 → stop → stats`
순서입니다. 따라서 마지막 direct 증명이 끝날 때까지 임시 인증서 key가 살아
있습니다.

driver는 원래 IPv4/IPv6 tuple과 opaque redirect record를 보존하고, 임의로
조각난 bounded ClientHello를 읽고, SNI로 machine-store identity를 선택하고,
inbound 커널 Schannel을 종료합니다. upstream은 Windows 인증서 검증을 사용하며
같은 `http/1.1` 또는 `h2` ALPN을 필수로 요구합니다.

- 요청에 `x-ntl-inspected: 1` 추가
- `X-NTL-Block: 1` 또는 `BLOCKME`를 upstream 연결 전에 403 차단
- 허용 요청은 실제 TLS origin으로 전달
- HTML 응답에 `<!-- inspected by ntl -->` 추가

각 session은 `io::with_async_transport` owner 안에서 실행됩니다. 이 owning
작업은 WSK backend, stream, Schannel 상태와 callback의 수명을 함께 유지하고,
close가 시작되면 신규 작업을 거절하며, 모든 완료 경로를 join한 뒤 끝납니다.
따라서 호출자가 멤버 선언 순서나 수동 callback drain 순서를 기억할 필요가
없습니다.

controller는 시험 CA를 `LocalMachine\Root`, origin leaf를
`LocalMachine\My`에 잠시 설치하고 종료 시 모두 제거합니다. 폐기 가능한 시험
VM에서만 실행하십시오.

## 빌드와 검증

```powershell
cmake -S examples\wfp\kernel\tls-inspection-proxy `
      -B artifacts\examples\wfp-kernel-tls -A x64 -DBUILD_TESTING=ON
cmake --build artifacts\examples\wfp-kernel-tls --config Debug
ctest --test-dir artifacts\examples\wfp-kernel-tls `
      -C Debug --output-on-failure
```

드라이버를 테스트 서명·로드한 뒤 관리자 셸에서 실행합니다.

```powershell
.\crtsys_wfp_kernel_tls_inspection_proxy_acceptance.exe
```

acceptance는 인접 controller를 시작하여 실제 IPv4/IPv6 redirect record, 두 TLS
구간, SNI, HTTP/1.1·HTTP/2 ALPN, 허용/차단, 요청·응답 변환, malformed와
idle ClientHello 실패, bounded capture, 정책 제거, direct 연결과 정리를
검증합니다. 성공 표시는 다음으로 시작합니다.

```text
Kernel TLS inspection acceptance PASS:
```

설치가 필요 없는 CTest는 `driver/inspection_policy.cpp`를 직접 사용해 같은 ALPN,
변환, 차단, HTML rewrite와 32 KiB fail-closed 정책을 검증합니다.
