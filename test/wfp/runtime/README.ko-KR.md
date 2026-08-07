# WFP 런타임 acceptance

런타임 테스트는 관찰 가능한 sample 동작으로 묶습니다. fixture 이름은 검증하는 네트워크 결과를 나타냅니다.

| Sample | 런타임 gate |
| --- | --- |
| [`ale-connect-block`](./ale-connect-block) | 선택한 outbound IPv4 TCP connection은 `WSAEACCES`로 거부되고, session 제거 시 복원됩니다. persistent manifest reconcile, controller-close 이후 생존, health, 명시적 uninstall을 검증하며 driver는 Driver Verifier에서 load/unload됩니다. |
| [`advanced`](./advanced) | dual-stack datagram redirect, 지연된 async inspection, flow/stream telemetry, UDP 콘텐츠 판정, framed-TCP 콘텐츠 판정, 로컬 TCP connect redirect, bind redirect, capability에 충실한 IPsec/MAC/vSwitch/fast/endpoint-closure 관찰을 선택 driver를 함께 대상으로 하는 Driver Verifier에서 실행합니다. stream-edit는 IOCP read/write/cancel/EOF와 동적 framing도 검사합니다. |
| [`https-live`](./https-live) | browser를 시작·종료·재프로파일·flag 추가 없이 이미 실행 중인 일반 browser를 관찰하는 Internet 의존 host inspection입니다. IPv4/IPv6 TCP HTTPS 응답을 제한된 HTML로 기록하고, 네이티브 WFP UDP/443 filter는 같은 실행의 inventory와 classify-drop 증거로 검증합니다. |

각 VM gate는 VM 경로, credential, staging directory를 매개변수로 받습니다. operator는 폐기 가능한 guest를 미리 부팅하고 선택한 Driver Verifier target을 미리 구성합니다. runner는 VM을 재부팅·reset·revert하지 않고, Driver Verifier도 변경하지 않습니다. 전후 상태만 검증합니다. driver service 또는 certificate를 설치하는 suite는 명시적 acknowledgement와 호출자가 만든 폐기 가능 guest sentinel도 필요합니다. 어떤 테스트도 특정 checkout, 사용자 계정, VM 이름에 묶이지 않습니다.
