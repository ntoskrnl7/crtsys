# NTL WFP 사용자 모드 UDP 콘텐츠 필터

[English](./README.md)

이 예제는 완전한 아웃바운드 IPv4/IPv6 UDP datagram을 사용자 모드에서
크기를 제한해 fail-closed 방식으로 판정합니다. UDP는 메시지 경계를 보존하므로
datagram payload 하나가 공유 콘텐츠 필터 레코드 하나를 정확히 담으며 TCP의
길이 prefix framer는 필요하지 않습니다.

```text
["NTLR"][version=1][classification][flags=0]
[u32 rule-id][u32 body 길이][body]
```

구조화된 parser는 `ordinary`를 허용하고 `restricted`를 차단하며 잘못된 헤더나
길이를 거부합니다. 일반 테스트 본문에도 `BLOCKME`를 넣어 판정이 단순 문자열
검색이 아님을 입증합니다.

## 적용 경로

1. `DATAGRAM_DATA_V4/V6`에서 완전한 UDP 헤더와 크기가 제한된 payload를
   검증합니다.
2. 드라이버가 제한된 payload를 복사하고 endpoint, compartment, 원격 주소,
   IPv6 scope 및 transport control data를 비동기 주입 완료까지 소유하는
   이동 전용 transport-send 요청을 만듭니다.
3. 원본 datagram을 복제한 뒤 흡수합니다.
4. reliable NTL RPC 알림이 사용자 coroutine에 도달하고, coroutine이 구조화된
   레코드를 해석합니다.
5. `permit`이면 clone을 재주입하고, `block` 또는 `malformed`이면 보내지 않고
   해제합니다.
6. passive 작업을 큐에 넣기 전에 pending 한도를 예약합니다. timeout, 세션
   손실, 과부하, 할당·게시·주입 실패 및 unload는 모두 fail closed입니다.
   자체 주입 패킷은 식별해서 통과시키며, injector 종료 시 완료 루틴이 소유한
   clone과 metadata가 모두 정리될 때까지 기다립니다.

`crtsys_wfp_udp_content_filter_policy_service.exe`에는 실제 정책 경로만 있습니다.
이 서비스는 `--port`, `--ready-file`, `--stop-file`, `--stats-file`,
`--expected-requests`, `--behavior normal|failure`를 받아 임시 정책을 설치하고,
reliable RPC 요청을 처리하고, 구조화된 판정을 반환하며, 드라이버 통계를
기록합니다. sender, receiver, 비정상 traffic 생성 또는 PASS 판정은 포함하지
않습니다.

`test/wfp/runtime/fixtures/user/udp-content-filter`에서 빌드되는
`crtsys_wfp_udp_content_filter_acceptance.exe`는 서비스를 직접 시작하고 ready
파일을 기다린 뒤 IPv4/IPv6 traffic을 모두 생성합니다. 구조화된 허용·차단·비정상
판정과 정책 제거를 검증한 다음 stop 파일을 만듭니다. `--failure-self-test` 모드는
pending 한도, timeout, 잘못되거나 늦은 판정 및 세션 취소 traffic을 담당합니다.

커널 대응 예제는 같은 레코드 parser를 사용하지만 동기식으로 판정하므로 RPC나
clone/reinjection이 필요하지 않습니다.
