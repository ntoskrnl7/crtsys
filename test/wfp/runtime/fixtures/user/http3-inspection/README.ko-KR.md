# 사용자 HTTP/3 허용성 검사 픽스처

[English](./README.md)

이 디렉터리에는 제품 WFP 코드가 아니라 검증 트래픽이 있습니다. 여기의 C++
소스는 서비스를 설치하거나 `DeviceIoControl`, `Fwpm*`, `ntl::wfp`를 사용하지
않습니다. 제품 서비스를 실행하고 제어된 MsQuic 클라이언트 역할을 수행하며,
프로토콜 트래픽을 만든 뒤 수집된 증거의 성공 여부를 판정합니다.

`acceptance_main.cpp`의 실행 형식은 다음과 같습니다.

```text
crtsys_wfp_http3_inspection_acceptance.exe
  <service.exe> <ipc-directory>
```

픽스처는 IPv4와 IPv6에서 일반 HTTP/3, 동적 QPACK,
gzip/deflate/Brotli, WebTransport, 정책이 만든 403, 정책 제거와 사용할 수 없는
콜아웃의 실패 시 차단 동작을 검사합니다. 수치로 된 서비스 증거를 확인한 뒤에만
다음과 같은 안정된 marker를 출력합니다.

```text
controlled-msquic-http3: WebTransport PASS ...
controlled-msquic-http3: dynamic QPACK and codecs PASS
controlled-msquic-http3: WFP gate PASS ...
raw-msquic-loopback: ... malformed=replay-contract PASS
```

`replay_contract.cpp`는 설치 없이 실행하는 제한된 프레이밍/코덱 계약이며
CTest에 등록됩니다. 실시간 허용성 검사에는 제품 드라이버가 로드된 일회용 VM,
관리자 권한과 아키텍처가 맞는 공식 `msquic.dll`이 필요합니다.

권장 방식은 제품 예제 CMake에서 함께 빌드해 모든 실행 파일을 같은 구성 출력에
두는 것입니다. 픽스처만 별도로 구성할 수도 있습니다.

```powershell
cmake -S . -B build -A x64 -DBUILD_TESTING=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```
