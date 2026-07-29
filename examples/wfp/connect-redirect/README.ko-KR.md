# WFP connect-redirect 한국어 설명

이 예제는 선택한 IPv4 TCP 목적지 연결만 로컬 사용자 모드 프록시로
보냅니다. 프록시는 WFP가 전달한 원래 목적지와 불투명한 redirect
records를 받아서, 두 방향의 TCP 바이트 스트림을 `co_await`로
중계합니다.

커널 콜백이 사용하는 변경 API는 하나입니다.

```cpp
return redirector.redirect(event, target);
```

이 호출 안에서 redirect loop 검사, 쓰기 가능한
`FWPS_CONNECT_REQUEST0` 획득과 적용, 원래 endpoint context의 WFP
소유권 이전, 실패 시 차단을 모두 처리합니다. 드라이버 사용자는
native request나 classify output을 직접 수정하지 않습니다.

앱 쪽도 두 단계뿐입니다.

```cpp
auto handoff = ntl::wfp::redirected_connection::capture(accepted);
SOCKET outbound = handoff.connect_original();
```

`connect_original()`은 원래 목적지에 연결하기 전에 WFP redirect
records를 자동 설정합니다. 그래서 프록시가 만든 outbound 연결을
같은 드라이버가 다시 프록시로 보내는 무한 반복이 생기지 않습니다.

실행 검증은 같은 프로세스에 원본 서버와 프록시를 열고 다음을
확인합니다.

1. 정책이 있을 때 클라이언트 연결은 프록시에 도착합니다.
2. 프록시는 정확한 원래 목적지 포트를 받습니다.
3. 요청과 응답을 양방향 코루틴 relay가 손실 없이 전달합니다.
4. 프록시 outbound 연결은 redirect loop 없이 원본 서버에 도착합니다.
5. 동적 정책을 제거하면 연결은 프록시를 거치지 않고 직접 갑니다.

이 예제는 범용 TCP 바이트 스트림 프록시 기반입니다. accepted socket에서
Schannel을 종료하고 평문 정책을 적용하는 방법은
[`tls-inspection-proxy`](../tls-inspection-proxy/README.ko-KR.md)를
참고하십시오.
