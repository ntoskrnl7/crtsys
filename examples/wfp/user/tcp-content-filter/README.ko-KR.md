# NTL WFP 사용자 모드 TCP 콘텐츠 필터

[English](./README.md)

이 예제는 완전한 인바운드 TCP 애플리케이션 메시지를 제한된 크기로 검사하고
사용자 모드에서 fail-closed 판정을 내립니다. TCP는 바이트 스트림이므로 UDP
예제와 의도적으로 분리되어 있습니다.

## 예제 wire 형식

TCP에서는 먼저 다음과 같은 예제 전용 메시지 framing을 사용합니다.

```text
[u32 big-endian 레코드 길이][콘텐츠 필터 레코드 하나]
```

4바이트 prefix는 TCP 헤더나 표준이 아닙니다. 공유 레코드는 다음과 같습니다.

```text
["NTLR"][version=1][classification][flags=0]
[u32 rule-id][u32 body 길이][body]
```

`ordinary`는 허용하고 `restricted`는 차단합니다. parser는 magic, version,
flags, 0이 아닌 rule ID, 정확한 본문 길이 및 4 KiB 본문 한도도 검증합니다.
허용 acceptance 레코드에도 `BLOCKME`를 넣어 정책이 단순 문자열 검색이 아니라
구조화된 필드를 기준으로 판정함을 입증합니다.

## 적용 경로

1. IPv4/IPv6 ALE flow-established callout이 타입이 지정된 flow 상태를 연결합니다.
2. `STREAM_V4/V6`가 크기 제한을 지키는 완전한 frame 하나에 필요한 바이트를
   요청합니다.
3. 인바운드 바이트 처리를 보류하고 드라이버 소유 저장소로 복사합니다.
4. reliable NTL RPC 알림이 사용자 coroutine에 도달합니다.
5. `permit`이면 해당 frame만 정확히 재개하고, `block` 또는 `malformed`이면
   flow를 폐기합니다.
6. timeout, 세션 손실, 과부하, 할당·게시 실패, 누락 바이트 및 unload는
   미처리 작업을 취소하며 fail closed로 동작합니다.

실행 파일에는 실제 정책 서비스만 포함되어 있습니다.

```text
crtsys_wfp_tcp_content_filter_policy_service.exe
  --port <fixture-port>
  --ready-file <path> --stop-file <path> --stats-file <path>
  --expected-requests <count> [--behavior normal|failure]
```

서비스는 IPv4/IPv6 임시 WFP 정책을 설치하고 reliable RPC 검사 요청을 처리하며,
구조화된 판정을 제출하고 ready 신호와 드라이버 통계를 기록한 뒤 종료 시 정책을
제거합니다. listener, traffic generator, exchange 도우미 또는 PASS 판정은
포함하지 않습니다.

`test/wfp/runtime/fixtures/user/tcp-content-filter`에서 빌드되는
`crtsys_wfp_tcp_content_filter_acceptance.exe`는 정책 서비스를 직접 시작하고
ready 파일을 기다립니다. split-prefix와 동일 flow의 IPv4/IPv6 traffic을 만들고,
구조화된 허용·차단·비정상 판정과 정책 제거를 검증한 뒤 stop 파일을 만듭니다.
`--failure-self-test` 모드는 timeout, 잘못되거나 늦은 판정 및 취소 traffic을
담당합니다.

커널 대응 예제는 같은 framing과 레코드 parser를 사용하지만 callout에서 동기식으로
판정합니다. 따라서 RPC 판정 큐가 없습니다. 자세한 내용은
`examples/wfp/kernel/tcp-content-filter`를 참조하세요.
