# NTL WFP 커널 모드 TCP 콘텐츠 필터

이 예제는 사용자 TCP 예제와 같은 bounded wire contract를 사용하지만 WFP
stream callout 안에서 동기적으로 parsing하고 판정합니다.

```text
[u32 big-endian 레코드 길이]
["NTLR"][version=1][classification][flags=0]
[u32 rule-id][u32 body 길이][body]
```

길이 prefix는 TCP 표준이 아니라 예제 애플리케이션 프로토콜입니다.
`ordinary`는 허용하고 `restricted`는 flow를 종료하며, 비정상·초과·미완성
또는 missed stream data는 fail closed입니다. 허용 본문에도 의도적으로
`BLOCKME`를 넣어 타입이 지정된 classification만 정책을 결정함을 입증합니다.

IPv4/IPv6 ALE flow-established callout이 상태를 연결하며 payload나 verdict는
사용자 모드로 전달하지 않습니다. 이 차이로 사용자 예제에 있는 RPC queue,
timeout, coroutine, 취소 및 stream continuation이 필요하지 않습니다. unload 시
drain할 비동기 작업도 없습니다. 드라이버는 먼저 새 요청 수락을 중단하고,
진행 중인 callback이 끝날 때까지 기다리며 callout 등록을 해제한 뒤 flow target과
장치를 해제합니다. flow 연결 실패와 사용할 수 없는 callout은 계속 fail closed로
처리됩니다.

`crtsys_wfp_kernel_tcp_content_filter_controller.exe`는 `--port`,
`--ready-file`, `--stop-file`, `--stats-file`을 받습니다. 컨트롤러는 드라이버
열기, 포트 한정 임시 정책 설치, ready 신호, IOCTL 통계 기록과 종료 시 정책
제거만 수행합니다. listener, exchange, 비정상 입력 생성 및 PASS 판정은
포함하지 않습니다.

별도 target `crtsys_wfp_kernel_tcp_content_filter_acceptance.exe`의 소스는
`test/wfp/runtime/fixtures/kernel/tcp-content-filter`에 있습니다. acceptance가
컨트롤러를 직접 시작하고 ready를 기다린 뒤 IPv4/IPv6 split framing과
타입이 지정된 허용/차단/비정상 traffic을 생성합니다. 이후 컨트롤러 종료를 요청하고
보고된 counter를 검증하며 정책 제거 후 traffic이 복원됨을 입증합니다.
