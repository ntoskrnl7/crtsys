# NTL KMDF DMA 드라이버 템플릿

[English](./README.md)

PnP 장치, DMA enabler, common buffer, transaction, 인터럽트와 순차 쓰기 큐를
구성하고 scatter/gather 테이블을 프로그래밍한 뒤 DPC에서 전송을 완료하는
패킷 DMA 흐름을 보여줍니다.

이 템플릿에는 INF가 없습니다. `sample_hardware`의 레지스터, 상태 비트,
descriptor, 전송 방향과 정렬을 실제 PCI/SoC 장치에 맞게 바꾸고 올바른
하드웨어 ID의 INF를 추가해야 합니다. 무관한 장치에는 설치하지 마십시오.
DMA 콜백과 DPC는 `DISPATCH_LEVEL`, ISR은 DIRQL에서 실행되므로 해당 IRQL에서
허용되는 비페이지 코드만 사용해야 합니다.

```powershell
cmake -S examples/kmdf/dma -B artifacts/examples/kmdf-dma -A x64
cmake --build artifacts/examples/kmdf-dma --config Debug
```

Visual Studio에서는 `crtsys_kmdf_dma_ntl_sample_vs.sln`을 사용합니다.
빌드는 하드웨어 없이 가능하지만 실행 검증에는 코드와 일치하는 장치가 필요합니다.
