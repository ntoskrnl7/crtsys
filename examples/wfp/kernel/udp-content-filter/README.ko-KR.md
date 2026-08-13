# NTL WFP 커널 모드 UDP 콘텐츠 필터

이 예제는 사용자 UDP 예제와 같은 구조화된 레코드를
`DATAGRAM_DATA_V4/V6`에서 parsing하고 동기적으로 판정합니다.

```text
["NTLR"][version=1][classification][flags=0]
[u32 rule-id][u32 body 길이][body]
```

`ordinary`는 허용하고 `restricted`는 absorb합니다. 잘못된 header와 길이,
지원하지 않는 packet topology 및 초과 레코드는 fail closed입니다. 일반 테스트
본문에도 `BLOCKME`를 넣어 이전의 substring marker 방식이 사용되지 않음을
입증합니다.

classify가 반환되기 전에 최종 판정하므로 pending queue, RPC, clone 및
reinjection은 없습니다. 이는 `examples/wfp/user/udp-content-filter`와 의도적으로
다른 runtime 방식이지 검증 범위가 빠진 것이 아닙니다. unload에서는 새 요청
수락을 중단하고 동기 callout 등록만 해제하면 되며, 사용할 수 없는 callout은
차단하도록 구성합니다.

`crtsys_wfp_kernel_udp_content_filter_controller.exe`는 `--port`,
`--ready-file`, `--stop-file`, `--stats-file`을 받습니다. 컨트롤러는 드라이버
열기, 포트 한정 임시 정책 설치, ready 신호, IOCTL 통계 기록과 종료 시 정책
제거만 수행합니다. sender, receiver, 비정상 datagram 및 PASS 판정은
포함하지 않습니다.

별도 target `crtsys_wfp_kernel_udp_content_filter_acceptance.exe`의 소스는
`test/wfp/runtime/fixtures/kernel/udp-content-filter`에 있습니다. acceptance가
컨트롤러를 직접 시작하고 ready를 기다린 뒤 IPv4/IPv6 타입이 지정된
허용/차단/비정상 traffic을 생성합니다. 이후 종료를 요청하고 counter를 검증하며
두 주소 계열 모두에서 정책 제거 후 traffic이 복원됨을 입증합니다. acceptance는
드라이버가 설치된 test-signing VM에서만 실행하세요.
