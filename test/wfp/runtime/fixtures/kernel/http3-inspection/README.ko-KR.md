# 커널 HTTP/3 허용성 검사 픽스처

[English](./README.md)

이 디렉터리는 커널 HTTP/3 예제의 제어된 클라이언트, 생성 트래픽, 검증과 최종 판정을
담당합니다. 제품 컨트롤러는 정책, 인증서, 드라이버 제어와 수명 주기 IPC를
담당하고, 픽스처는 검증 동작만 담당하도록
`examples/wfp/kernel/http3-inspection` 밖에 분리했습니다.

픽스처 C++ 소스는 `ntl::wfp`, `Fwpm*`, `DeviceIoControl`을 사용하지 않고 Windows
서비스를 설치하거나 제어하지도 않습니다. 실행 형식은 다음과 같습니다.

```text
crtsys_wfp_kernel_http3_inspection_acceptance.exe
  <controller.exe> <ipc-directory>
```

픽스처는 IPv4/IPv6 HTTP/3 허용·차단, 동적 QPACK, 압축 HTML,
WebTransport 스트림/Datagram/Capsule/reliable-reset, 정책 제거 후 직접 트래픽,
사용할 수 없는 콜아웃 격리, 정상 복구, 캡처 검증과 96개 순차 연결을
실행합니다. 마지막에는 다음 안정된 표식을 출력합니다.

```text
Kernel HTTP/3 inspection PASS: IPv4/IPv6 WFP, ... capture, cleanup PASS
```

픽스처만 별도로 구성하려면 다음과 같이 실행합니다.

```powershell
cmake -S . -B build -A x64
cmake --build build --config Debug
```

권장 VM 흐름은 제품 예제 CMake에서 함께 빌드해 드라이버, 컨트롤러와 픽스처를
같은 구성 출력 디렉터리에 두는 것입니다. 실시간 실행에는 호환되는 공식
`msquic.sys` 공급자와 샘플 드라이버가 로드된 관리자 권한 일회용 VM,
그리고 픽스처 옆의 아키텍처가 맞는 공식 `msquic.dll`이 필요합니다. 이
디렉터리를 빌드하는 것만으로 설치가 수행되지는 않습니다.
