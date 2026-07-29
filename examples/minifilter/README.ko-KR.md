# NTL 미니필터 예제

[English](./README.md)

각 예제는 드라이버 진입점부터 앱까지 한 가지 핵심 개념을 읽기 쉽게 보여줍니다.
대규모 호환성, 실패 경로, 파일 시스템, WOW64 및 Driver Verifier 검증은
[`test/flt`](../../test/flt)에 분리되어 있습니다.

| 예제 | 핵심 내용 |
| --- | --- |
| [`basic`](./basic/README.ko-KR.md) | 형식화된 I/O 콜백, 이름과 stream context |
| [`control-device`](./control-device/README.ko-KR.md) | 미니필터 수명에 속한 제어 장치와 unload 거부 |
| [`communication`](./communication/README.ko-KR.md) | 통신 포트, RPC, 알림, stream과 공유 메모리 ring |
| [`operation-log`](./operation-log/README.ko-KR.md) | I/O 콜백, 열린 파일 상태와 제한된 로그 큐 |
| [`swap-buffers`](./swap-buffers/README.ko-KR.md) | 안전한 쓰기 입력 및 읽기 출력 버퍼 교체 |
| [`volume-metadata`](./volume-metadata/README.ko-KR.md) | 잠금·해제·스냅샷·PnP 전반의 볼륨 메타데이터 수명 |

권장 순서는 표의 위에서 아래입니다. INF의 altitude는 모두 개발용이며, 실제
제품을 배포하려면 Microsoft가 할당한 고유 altitude가 필요합니다.
