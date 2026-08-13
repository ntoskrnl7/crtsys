# WDK KMDF Sample 지원 범위

이 문서는 `Windows-driver-samples`가 보여 주는 재사용 가능한 KMDF mechanism을 NTL API 및 저장소 검증에 대응시킵니다.

지원된다는 것은 NTL이 Microsoft sample을 복사하거나 모든 `Wdf*` routine의 이름을 바꾼다는 뜻이 아닙니다. 일반적인 driver 코드가 타입이 지정된 공개 API로 수명과 callback 경로를 표현할 수 있고, 지원 toolchain과 architecture에서 contract가 컴파일되며, 동작이 문서화되고, 반복 가능한 hardware가 있을 때 load된 driver test가 런타임 민감 동작을 관찰 가능하게 하면 해당 mechanism은 지원됩니다.

device-class protocol과 class-extension API는 공통 KMDF object model과 분리되어 있습니다. driver는 명시적인 `native()`, `native_handle()`, `native_object()`, `wdm_*()` 상호 운용 지점을 통해 해당 네이티브 WDK contract를 `ntl::kmdf`와 나란히 사용할 수 있습니다.

## 지원 매트릭스

| Microsoft sample family | 재사용 가능한 mechanism | NTL 지원 | 주요 근거 |
| --- | --- | --- | --- |
| `general/ioctl/kmdf` | control/PnP device, buffered IOCTL, queue, file object, handle별 상태 | 지원됨 | `examples/kmdf/basic`, `examples/kmdf/reference`, 소프트웨어 전용 runtime suite |
| `general/echo/kmdf` | queue 동기화, 지연 완료, 취소, timer 수명 | 지원됨 | `examples/kmdf/echo` 및 success/cancel/restart runtime 검사 |
| Toaster function `simple` / `featured` | PnP function device, hardware/D0 callback, interface, idle policy | 지원됨 | `examples/kmdf/pnp`, `examples/kmdf/wmi`, VM restart/remove 검사 |
| Toaster bus `dynamic` / `static` | child list, 동적/정적 PDO 개념, resource requirement, eject/missing 전이 | 재사용 가능한 bus 수명 주기 지원 | `examples/kmdf/bus` 및 bus/function/app runtime 경로 |
| Toaster filter `generic` / `sideband` / `toastmon` | filter FDO, forwarding, completion, lower-target 수명, sideband control pattern | 공통 filter-stack 경로 지원 | `examples/kmdf/filter-stack` 및 target/filter/app runtime 경로 |
| `general/pcidrv/kmdf` | 변환된 resource, interrupt, DPC, DMA, power, registry, WMI | 공통 KMDF mechanism 지원, hardware protocol은 compile-only | `examples/kmdf/pnp`, `dma`, `wmi`, package compile contract |
| `general/PLX9x5x` | packet DMA, common buffer, scatter/gather, interrupt 완료 | hardware template로 지원 | `examples/kmdf/dma`; runtime에는 일치하는 hardware 필요 |
| USB KMDF sample | USB target/config/interface/pipe, continuous reader, interrupt, child-device 구성 | 재사용 가능한 USB/KMDF mechanism 지원 | `examples/kmdf/usb`, `examples/kmdf/bus`, package compile contract; endpoint runtime에는 일치하는 hardware 필요 |
| `wmi/wmisamp` | MOF data block, query/set/item/method callback, event | 지원됨 | `examples/kmdf/wmi` 및 ROOT\WMI application verifier |
| `serial/serial` | queue, request, interrupt, DPC, timer, target mechanism | 공통 KMDF mechanism 지원 | 공통 example 및 compile contract; UART register/protocol 코드는 네이티브로 유지 |
| PoFx WDF sample | component power 및 framework PoFx 통합 | 네이티브 상호 운용 | 일반적이지 않은 component-power policy는 타입이 지정된 공통 표면 밖에 둠 |
| ACX, NetAdapterCx, WiFiCx, GPIO/SpbCx, UCM, HID 등 class family | WDF 위에 구성되는 class-extension/device-protocol contract | 공통 표면 범위 밖 | `ntl::kmdf`와 함께 네이티브 class contract 사용. 실제 driver에 필요할 때만 집중 adapter 추가 |
| raw IRP preprocessing 및 miniport 통합 | WDM stack location, port-driver/miniport 소유권 | 네이티브 상호 운용 | 문서화된 KMDF 표면 경계와 명시적 WDM/native escape hatch |

## 공통 검증 gate

해당되는 행에는 다음 gate를 적용합니다.

1. 공개 example과 compile contract는 x86/x64에서 `/W4 /WX`로 빌드됩니다.
2. callback signature와 request/object 소유권 전이는 컴파일 타임에 검사하며, 잘못된 copy 또는 callback 대입은 ill-formed입니다.
3. 소프트웨어 전용 control, PnP, bus, filter, WMI, cancellation, restart, unload 경로는 폐기 가능한 Windows VM에서 실행됩니다.
4. 고정 layout 사용자/커널 contract는 architecture 중립적이며 두 client architecture에서 컴파일됩니다.
5. 보류 상태는 성공한 요청만으로 검증하지 않고 cancellation, target removal, device restart, unload를 통해 검사합니다.
6. runtime application은 관찰 가능한 assertion을 수행합니다. Debug output만으로는 검증으로 간주하지 않습니다.
7. Driver Verifier stress는 동시 request, cancellation, timer, work item, WDF object 수명, 반복 unload를 다룹니다.
8. hardware 전용 경로는 resource, IRQL, 소유권 contract를 컴파일하고 문서화해야 합니다. 일치하는 device와 protocol 없이는 runtime 지원으로 보고하지 않습니다.

host-side acceptance gate는 하나의 Verifier boot에서 소프트웨어 전용 driver binary 전체를 선택하고, 각 x64 package를 x64/WOW64 application으로 실행하며, device restart와 반복 stress-driver load/unload를 수행하고, crash/dump와 device-cleanup 상태를 검사한 뒤, 명시적으로 제공된 이전 Verifier 구성을 복원합니다.

## 공통 KMDF 표면의 정의

공통 표면에는 driver/device entry, control 및 PnP device, 타입이 지정된 context와 callback, queue와 forward progress, request와 I/O target, file object, PnP/power/resource callback, interrupt, timer, work item, DPC, child list와 PDO, query interface, registry/property, DMA, USB, WMI가 포함됩니다.

일반적인 control, function, filter 또는 bus driver가 framework 수명과 일반 I/O 경로를 `ntl::kmdf` 안에 둘 수 있고, device별 register layout, protocol structure, class-extension 호출은 알아볼 수 있는 네이티브 WDK 코드로 유지할 수 있을 때 이 표면은 충분합니다. 네이티브 호출이 0개인 것은 지원 범위의 목표가 아닙니다.

대응하는 공개 API, example, compile contract 또는 runtime 근거가 바뀔 때만 이 매트릭스를 갱신하십시오. VM build number, verifier log, hardware별 결과는 이 저장소 전체 요약이 아니라 fixture 문서에 보관하십시오.
