# NTL WFP TCP content-filter 예제

이 예제는 인바운드 TCP **애플리케이션 메시지**를 사용자 모드에서
검사하고, 제한된 시간 안에 허용 또는 flow 차단을 결정하는 방법을
보여줍니다. TCP는 datagram 경계가 없는 바이트 스트림이므로 UDP 예제와
분리되어 있습니다.

## 예제 애플리케이션 프로토콜

런타임 검증은 다음 형식을 사용합니다.

```text
[본문 길이: 4바이트 big-endian][본문: 지정된 길이만큼]
```

이 4바이트 필드는 **TCP 헤더도 아니고 TCP 표준도 아닙니다.** 오직
이 예제가 선택한 애플리케이션 프로토콜입니다. 실제 제품에서는
`ntl::net::framing::u32_be_length_prefix`를 검사 대상 프로토콜에 맞는
framer로 교체해야 합니다.

## 처리 과정

1. ALE flow-established callout이 선택된 인바운드 TCP flow에 타입 상태를
   연결합니다.
2. stream callout이 완전한 애플리케이션 메시지가 될 때까지 필요한
   바이트를 요청합니다.
3. 완성된 인바운드 메시지를 defer하고 독립 소유 버퍼에 복사합니다.
4. reliable typed NTL RPC가 사용자 모드 coroutine에 메시지를 전달합니다.
5. `permit`이면 deferred stream을 재개하고 정확히 그 frame만 허용합니다.
6. `block`, timeout, 잘못된 framing, 연결 종료, 한도 초과 또는 할당
   실패는 flow 전체를 차단합니다.

앱은 4바이트 길이 필드를 두 번의 socket write로 나눠 보내도 메시지가
정상 조립되는지 확인합니다. 이어 `BLOCKME` 메시지가 flow 전체를
닫는지 확인하고, 동적 WFP 정책을 제거하면 일반 TCP 통신이 복원되는지
검증합니다.

`crtsys_wfp_tcp_content_filter_app.exe --failure-self-test`는 잘못된
verdict 거부, timeout flow 차단, 늦은 permit 거부와 정책 제거 후 복원을
검증합니다.
