# NTL KMDF DMA 드라이버 template

[English](./README.md)

이 빌드 가능한 driver template은 NTL/KMDF packet-DMA control flow 전체를
보여줍니다.

1. PnP device, DMA enabler, common buffer, transaction, interrupt 및 sequential
   write queue를 만듭니다.
2. request-backed DMA transaction을 초기화합니다.
3. program-DMA callback에서 scatter/gather descriptor table을 설정합니다.
4. interrupt DPC에서 transfer 완료를 보고합니다.
5. transaction을 해제하고 원본 request를 완료합니다.

prepare-hardware 경로는 `ntl::kmdf::resource_list`를 순회하고
`resource_descriptor::memory()`로 device register range를 얻습니다. 따라서 일반
driver 코드에서 raw `CM_PARTIAL_RESOURCE_DESCRIPTOR` union을 다룰 필요가 없으며,
드문 resource type에는 계속 `native()`를 사용할 수 있습니다.

이 template에는 의도적으로 INF가 없습니다. DMA register layout, interrupt status
bit, descriptor format, transfer direction, alignment 및 hardware ID는 device마다
다릅니다. `sample_hardware` 내용을 바꾸고 실제 PCI 또는 SoC device에 맞는 INF를
추가하세요. 무관한 device에는 이 드라이버를 설치하지 마세요.

program-DMA callback과 interrupt DPC는 `DISPATCH_LEVEL`에서 실행됩니다. 이들은
nonpageable state와 해당 level에서 유효한 WDF/WDK operation만 사용합니다. 예제는
두 callback에서 PASSIVE_LEVEL CRT/STL 영역을 사용하지 않습니다. ISR은 DIRQL에서
실행되므로 제약이 더 큽니다. 예제를 수정할 때 이 callback에 추가하는 모든 NTL,
WDF, WDK, CRT 또는 STL operation이 실제 IRQL을 명시적으로 지원해야 합니다. 높은
IRQL에서도 안전할 것이라고 가정하지 말고 일반 STL 작업은 passive queue callback이나
passive work item으로 옮기세요.

DPC와 공유하는 유일한 표준 라이브러리 객체는 `std::atomic<WDFREQUEST>`입니다.
source는 `static_assert`로 `is_always_lock_free`를 강제하므로 지원 target에서
`DISPATCH_LEVEL`용 runtime lock 구현으로 조용히 대체될 수 없습니다.

## CMake 빌드

```powershell
cmake -S examples/kmdf/dma `
      -B artifacts/examples/kmdf-dma `
      -A x64
cmake --build artifacts/examples/kmdf-dma --config Debug
```

## Visual Studio

`crtsys_kmdf_dma_ntl_sample_vs.sln`을 여세요. project는 기본적으로 설치된 최신
`crtsys` NuGet package를 사용합니다. 고정된 package version이 필요하면
`CrtSysPackageVersion`을 override하세요.

DMA hardware 없이도 compile할 수 있습니다. runtime 검증에는 수정한 코드의
resource 및 register 계약과 일치하는 device가 필요합니다.
