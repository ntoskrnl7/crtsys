# NTL 예제 드라이버

[English](./README.md)

`crtsys`의 NTL 도우미를 사용하는 작은 WDK 드라이버입니다. `ntl::main`,
registry 설정용 `driver_config`, 장치와 DOS link를 소유하는
`device_endpoint`, 형식화된 `ioctl`, unload 동기화용 `remove_lock`,
PASSIVE_LEVEL 실행기와 커널 pool 기반 PMR을 보여줍니다.

```bat
cmake -S examples\ntl-driver -B examples\ntl-driver\build_x64 -A x64
cmake --build examples\ntl-driver\build_x64 --config Debug
sc create CrtSysNtlSample binpath= "C:\path\to\crtsys_ntl_sample.sys" type= kernel start= demand
sc start CrtSysNtlSample
examples\ntl-driver\build_x64\Debug\crtsys_ntl_sample_app.exe 41
sc stop CrtSysNtlSample
sc delete CrtSysNtlSample
```

공유 IOCTL 계약은
[`shared/ntl_sample_ioctl.hpp`](./shared/ntl_sample_ioctl.hpp)에 있습니다.
성공 출력은 `ping ok: request=41 reply=42 ...` 형식이며, sequence는 요청마다
증가하고 선택적 registry `Parameters\Flags`가 응답과 checksum에 반영됩니다.
