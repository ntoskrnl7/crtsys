# NTL 미니필터 제어 장치 예제

[English](./README.md)

WDK `cdo` 예제의 재사용 가능한 부분을 형식화된 NTL 소유권으로 구현합니다.
`driver.add_control_device<T>()`로 legacy 제어 장치를 만들고 형식화된
IOCTL handler를 등록하며, 열린 handle이 있으면 unload를 거부합니다. 장치와
symbolic link는 `ntl::flt::driver`가 자동으로 정리합니다.

앱은 `\\.\CrtSysMinifilterControlDevice`를 열어 ping을 보내고, handle이
열린 동안의 unload 거부와 닫은 뒤의 정상 unload를 확인합니다.

```powershell
cmake -S examples\minifilter\control-device -B out\minifilter-control-device-x64 -A x64
cmake --build out\minifilter-control-device-x64 --config Debug
fltmc load CrtSysMinifilterControlDeviceSample
crtsys_minifilter_control_device_sample_app.exe
fltmc unload CrtSysMinifilterControlDeviceSample
```

앱은 의도적으로 `FilterUnload`를 시도하므로 관리자 권한이 필요합니다.
