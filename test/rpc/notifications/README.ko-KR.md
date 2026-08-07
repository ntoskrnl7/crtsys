# NTL RPC 알림 fixture

이 fixture는 보류 중인 `METHOD_BUFFERED` IOCTL 및 커널 `IO_CSQ` cancel-safe queue를 통한 형식화된 커널-사용자 모드 알림을 검증합니다.

검사 범위:

- x64 driver와 x86/x64 app 전반의 고정 폭 알림 선택
- `std::string`, `std::vector<std::uint32_t>` payload 직렬화
- 동기 및 overlapped 형식화된 수신
- timeout, 대상 지정 취소, 범위 정리
- FIFO 전달 및 보류 수신 한도
- `PASSIVE_LEVEL`보다 높은 IRQL에서 직렬화 거부
- 보류 수신 취소, handle 정리, unload, restart, 새 호출을 포함한 service 중지
- app 수신이 보류되지 않은 경우 암시적 event buffering 없음
- 불투명 cross-bitness token을 사용하는 재연결 가능한 session
- 명시적 ACK까지 reliable 전달 재생
- session별 제한된 backpressure와 ACK 뒤 capacity 회복
- subscription 취소와 명시적 session close
- 형식화된 session state와 외부 persistence hook 호출
- 활성 비동기 session RPC callback이 끝날 때까지 기다리는 session close

VM 권위 사례는 Driver Verifier 아래 x64 Debug driver와 x64/x86 Debug app을 사용합니다. service 이름을 app에 전달하면 stop, unload, restart, endpoint recovery를 포함합니다.
