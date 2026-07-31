# NTL KMDF 예제

[English](./README.md)

각 디렉터리는 독립적으로 빌드할 수 있는 KMDF 프로젝트입니다.

| 디렉터리 | 핵심 내용 | 실행 조건 |
| --- | --- | --- |
| [`basic`](./basic/README.ko-KR.md) | 비 PnP 제어 장치, 형식화된 요청과 컨텍스트, 큐, 취소, WDF 객체 도우미 | 별도 하드웨어가 없는 폐기 가능한 VM |
| [`echo`](./echo/README.ko-KR.md) | 큐 동기화, 타이머 완료, 취소 경쟁과 큐 복구 | 예제 INF를 설치한 VM |
| [`pnp`](./pnp/README.ko-KR.md) | PnP/전원 콜백, 리소스, 장치 인터페이스와 IOCTL | 예제 INF를 설치한 VM |
| [`bus`](./bus/README.ko-KR.md) | 동적 PDO 추가·제거·꺼내기와 자식 함수 드라이버 | 두 INF를 설치한 VM |
| [`filter-stack`](./filter-stack/README.ko-KR.md) | 함수 드라이버와 상위 필터의 요청 전달 | 통합 INF를 설치한 VM |
| [`reference`](./reference/README.ko-KR.md) | 버전 ABI와 세션을 갖춘 제품 지향 시작점 | VM과 Driver Verifier |
| [`dma`](./dma/README.ko-KR.md) | 패킷 DMA, scatter/gather, 인터럽트와 DPC 템플릿 | 일치하는 DMA 하드웨어 |
| [`usb`](./usb/README.ko-KR.md) | USB 장치·인터페이스·파이프와 연속 판독기 | 일치하는 USB 하드웨어 |
| [`wmi`](./wmi/README.ko-KR.md) | MOF 기반 WMI 조회·설정·메서드·이벤트 | 예제 INF를 설치한 VM |

모든 프로젝트는 CMake와 Visual Studio 프로젝트를 제공합니다. 스트레스 및
Verifier 검증은 예제가 아니라 [`test/kmdf`](../../test/kmdf)에 있습니다.
