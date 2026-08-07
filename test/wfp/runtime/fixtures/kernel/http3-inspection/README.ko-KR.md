# 커널 HTTP/3 acceptance fixture

[English](./README.md)

이 디렉터리는 커널 HTTP/3 예제의 제어된 client, 생성 트래픽, 검증과 최종 판정을
담당합니다. 제품 controller는 정책, 인증서, driver 제어와 lifecycle IPC를
담당하고, fixture는 검증 동작만 담당하도록
`examples/wfp/kernel/http3-inspection` 밖에 분리했습니다.

fixture C++ 소스는 `ntl::wfp`, `Fwpm*`, `DeviceIoControl`을 사용하지 않고 Windows
service를 설치하거나 제어하지도 않습니다. 실행 형식은 다음과 같습니다.

```text
crtsys_wfp_kernel_http3_inspection_acceptance.exe
  <controller.exe> <ipc-directory>
```

fixture는 IPv4/IPv6 HTTP/3 허용·차단, dynamic QPACK, 압축 HTML,
WebTransport stream/Datagram/Capsule/reliable-reset, 정책 제거 후 직접 트래픽,
사용할 수 없는 callout 격리, 정상 복구, capture 검증과 96개 순차 connection을
실행합니다. 마지막에는 다음 안정된 marker를 출력합니다.

```text
Kernel HTTP/3 inspection PASS: IPv4/IPv6 WFP, ... capture, cleanup PASS
```

fixture만 별도로 구성하려면 다음과 같이 실행합니다.

```powershell
cmake -S . -B build -A x64
cmake --build build --config Debug
```

권장 VM 흐름은 제품 예제 CMake에서 함께 빌드해 driver, controller와 fixture를
같은 구성 출력 디렉터리에 두는 것입니다. live 실행에는 호환되는 공식
`msquic.sys` provider와 sample driver가 로드된 관리자 권한 disposable VM,
그리고 fixture 옆의 아키텍처가 맞는 공식 `msquic.dll`이 필요합니다. 이
디렉터리를 빌드하는 것만으로 설치가 수행되지는 않습니다.
