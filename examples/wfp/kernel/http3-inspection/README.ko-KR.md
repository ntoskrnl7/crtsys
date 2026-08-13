# 커널 HTTP/3 검사

[English](./README.md) · [WFP 예제](../../README.ko-KR.md)

이 예제는 공식 MsQuic 커널 NMR 공급자에 연결해 드라이버 내부에서 실제
QUIC/TLS 1.3 및 HTTP/3 엔드포인트를 실행합니다. 파서 재생이 아닙니다.

```text
제어된 client UDP connect
  -> 타입이 지정된 ALE_AUTH_CONNECT_V4/V6 WFP 콜아웃
  -> msquic.sys NMR 공급자
  -> 커널 TLS 1.3 및 QUIC 스트림
  -> HTTP/3 SETTINGS, frame 및 제한된 QPACK
  -> X-NTL-Block: 1 정책
  -> 200 또는 403 HTML 응답
```

제품과 허용성 검사 역할은 분리되어 있습니다.

- `crtsys_wfp_kernel_http3_inspection.sys`는 콜아웃, 커널 MsQuic 엔드포인트,
  HTTP/3/QPACK/codec 처리, WebTransport 상태, 제한된 connection quota,
  원격 분석과 PASSIVE_LEVEL 회수 작업을 담당합니다.
- `crtsys_wfp_kernel_http3_inspection_controller.exe`는 임시 인증서, 동적 WFP
  정책, 드라이버 제어와 수명 주기 IPC를 담당합니다. 제어된 HTTP/3 클라이언트나
  `PASS` 판정 코드는 포함하지 않습니다.
- MsQuic client, 생성 트래픽, 검증과 최종 marker는
  `test/wfp/runtime/fixtures/kernel/http3-inspection`에 있습니다.

드라이버는 IPv4/IPv6, 허용·차단, 동적 QPACK 차단 스트림 재개와 확인 응답,
gzip/deflate/Brotli로 압축된 HTML, WebTransport Extended CONNECT,
양방향·단방향 stream, Datagram, 분할 Capsule과 reliable reset을 처리합니다.
`X-NTL-Block: 1`은 session 활성화 전에 Extended CONNECT를 최종 403으로
거부합니다. 등록하지 않은 callout을 쓰는 별도 정책은 origin에 도달하지 않고
`callout_unavailable::block`이 동작하는지도 검증합니다.

64개 연결 제한은 누적 개수가 아니라 동시 연결 제한입니다. 허용성 검사는
추가로 96개 연결을 순차 실행합니다. 종료 콜백과 스트림 drain이
끝난 뒤 회수 작업이 슬롯을 반환해야 하며, 마지막 원격 분석은 활성 연결 0과
종료된 슬롯의 완전한 회수를 요구합니다.

## 빌드 결과

```powershell
cmake -S . -B build -A x64 -DBUILD_TESTING=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

한 빌드에서 같은 구성 출력 디렉터리에 다음 target을 만듭니다.

- `crtsys_wfp_kernel_http3_inspection`
- `crtsys_wfp_kernel_http3_inspection_controller`
- `crtsys_wfp_kernel_http3_inspection_acceptance`
- `crtsys_wfp_kernel_http3_inspection_policy_contracts`

policy contract는 설치 없이 실행할 수 있습니다. MsQuic이나 driver를 로드하지
않고, WFP 상태나 인증서 저장소도 바꾸지 않습니다. 실제 acceptance에는 호환되는
공식 `msquic.sys` provider와 sample driver가 설치된 disposable VM, 그리고
acceptance 실행 파일 옆의 아키텍처가 맞는 공식 `msquic.dll`이 필요합니다.
빌드 과정은 host에 driver를 설치하지 않습니다.

## 제품 controller 인터페이스

```text
crtsys_wfp_kernel_http3_inspection_controller.exe
  <controlled-application.exe> <ipc-directory>
```

fixture가 제어 대상 실행 파일과 lifecycle 명령을 제공합니다. 자세한 내용은
[커널 HTTP/3 fixture 문서](../../../../test/wfp/runtime/fixtures/kernel/http3-inspection/README.ko-KR.md)를
참조하십시오.

이 예제는 제어된 kernel endpoint입니다. 임의 원격 UDP/443에 대한 투명 NAT나
브라우저 MITM이 아닙니다. 브라우저 인증서 정책, ECH, pinning, mTLS와 투명
양방향 UDP routing은 별도의 제품 경계입니다.
