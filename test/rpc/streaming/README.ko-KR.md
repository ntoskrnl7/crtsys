# NTL RPC 스트리밍 fixture

이 fixture는 양방향 session-bound 형식화된 streaming을 검증합니다.

검사 범위:

- STL을 포함하는 형식화된 app-to-driver, driver-to-app chunk
- x64/x86 app에서 사용할 수 있는 x64 driver wire contract
- 제한된 session별 download queue와 `STATUS_DEVICE_BUSY` backpressure
- 명시적 ACK 및 queue capacity 회복
- read timeout, 대상 지정 `CancelIoEx`, 범위 정리
- 실행 중인 upload callback의 협력적 취소
- 연결 해제 뒤 token 재연결 및 확인되지 않은 record 재생
- WDK `NTSTATUS` 값을 갖는 성공/실패 terminal record
- terminal-state 거부, terminal reconnect replay, close 뒤 reopen
- storage hook이 terminal/chunk record를 역순으로 반환할 때의 persistence restore 순서
- terminal record를 포함한 제한된 multi-chunk upload/download batch
- 보류 중인 빈 batch 수신 취소와 잘못된 batch 범위
- 어느 channel에서든 batch 선택 시, 각 channel 내부 FIFO를 바꾸지 않으면서 나중의 critical record가 더 이른 background record보다 앞서는 경우
- 잘못된 형식, 잘린 형식, 과도하게 큰 upload 거부
- 일반 및 authorization stream contract macro의 client/server 확장

streaming은 upload에는 일반적인 제한된 RPC 호출을, download에는 reliable notification record를 사용합니다. timeout은 살아 있는 I/O 요청을 해제하지 않습니다. app은 계속 기다리거나, 취소하거나, 소유자를 파기해야 합니다. stream을 닫으려면 전달된 모든 record가 ACK되어야 하고, 모든 보류 read/write가 취소되거나 drain되어야 합니다.
