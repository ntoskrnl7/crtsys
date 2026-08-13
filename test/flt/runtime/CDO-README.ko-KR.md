# CDO 미니필터 런타임 fixture

이 driver/app 쌍은 `ntl::flt::driver::add_control_device()`를 검증합니다. driver source는 공개 NTL 경계에서 `<ntl/flt/all>`만 include합니다. `fltKernel.h`를 include하거나, 원시 WDM major-function table을 지정하거나, 네이티브 device object를 만들거나 unload 중 삭제하지 않습니다.

미니필터는 `driver.start()` 전에 이름 있는 `ntl::device<cdo_extension>`를 queue에 넣습니다. NTL은 filter 등록과 filtering 시작 사이에 이를 만들고, 타입이 지정된 create/cleanup/close/device-control handler를 구성한 후 `\\DosDevices\\CrtSysFltCdoRuntime`을 공개합니다.

VM app은 다음을 검증합니다.

- `\\.\CrtSysFltCdoRuntime`을 사용자 모드에서 열 수 있음
- 두 번째 동시 create가 driver의 single-open 실패를 받음
- 공유 `ntl::ioctl_from_contract` ping이 타입이 지정된 입력과 출력을 검증함
- `FilterUnload`가 미니필터에 도달하며 CDO에 열린 참조가 있을 때 거부됨
- 거부 후에도 열린 handle이 IOCTL을 dispatch함
- cleanup과 close 뒤에 다시 열 수 있음
- runner의 마지막 unload가 link와 device를 제거하고 미니필터를 unload함

application 성공 contract:

```text
cdo_integration=PASS concurrent_open_error=548
unload_veto=0x801F0010 creates=2 ioctls=3
```

이 시나리오는 Standard Driver Verifier flag `0x1209BB`와 x64 driver에 대한 x64/WOW64 application 모두로 검증하십시오. 생성된 log는 테스트 환경에 구성된 artifact 대상에 보존하십시오.
