# NTL WFP UDP content-filter 예제

이 예제는 완전한 아웃바운드 UDP datagram을 사용자 모드에서 검사하고
허용 또는 차단하는 방법을 보여줍니다. UDP는 자체적으로 datagram 경계를
보존하므로 TCP 예제와 달리 애플리케이션 메시지 framer가 필요하지
않습니다.

## 처리 과정

1. `DATAGRAM_DATA_V4` callout이 아웃바운드 UDP datagram 하나를 받습니다.
2. 드라이버가 UDP 헤더를 검증하고 최대 4096바이트 payload를 복사합니다.
3. 원본 datagram을 clone한 뒤 absorb합니다.
4. reliable typed NTL RPC가 사용자 모드 coroutine에 payload를 전달합니다.
5. `permit`이면 clone을 재주입하고, `block`이면 해당 datagram만
   폐기합니다.
6. timeout, 연결 종료, 대기 한도 초과, 잘못된 verdict 또는 할당 실패는
   fail-closed로 처리합니다.

앱은 허용할 datagram 하나와 `BLOCKME`가 포함된 datagram 하나를 보내
각각 재주입과 차단을 확인합니다. 동적 WFP 정책을 제거한 뒤에는 일반
UDP 통신이 복원되는지도 검증합니다.

`crtsys_wfp_udp_content_filter_app.exe --failure-self-test`는 대기 한도,
timeout, 잘못된 verdict 거부, 늦은 permit 거부와 정책 제거 후 복원을
검증합니다.
