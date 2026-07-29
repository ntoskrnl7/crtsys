# WFP stream-edit 한국어 설명

이 샘플은 선택한 outbound IPv4 TCP 스트림에서 `BLOCKME`를 같은 길이의
`REDACT!`로 바꿉니다.

핵심 순서:

1. flow-established callout이 stream callout에 typed context를 연결합니다.
2. stream callout은 고정 크기 stack buffer로 평탄화하지 않고 NBL/MDL
   `scatter_view`를 직접 검색하며 원본 포인터를 보관하지 않습니다.
3. 토큰이 indication 끝에서 잘렸다면 안전한 앞부분만 permit하거나
   `NEED_MORE_DATA`를 반환합니다.
4. 완전한 토큰이 맨 앞에 오면 새 nonpaged buffer, MDL, NBL을 하나의
   이동 전용 객체로 만듭니다.
5. `FwpsStreamInjectAsync`가 replacement를 받을 때까지 해당 객체의 소유권을
   유지합니다.
6. 주입 성공 후에만 원래 토큰 바이트를 block합니다.
7. 복사·할당·주입 실패 시 원문을 몰래 통과시키지 않고 연결을 끊습니다.

컨트롤러는 토큰을 두 번의 `send()`로 나눠 보내 경계 처리를 확인하고,
정책을 제거한 뒤에는 원래 문자열이 그대로 전달되는지도 확인합니다.

컨트롤러의 실제 client/server 경로는 overlapped socket을
`io_completion_context`에 연결하고, accepted
socket의 `co_await read_exactly()`와 각 조각의 `co_await write_all()`이
IOCP worker에서 재개됩니다. 정책 없이 조각 송수신, `CancelIoEx`, 중간 EOF
계약만 실행하려면 다음을 사용합니다.

```powershell
crtsys_wfp_stream_edit_app.exe --coroutine-self-test
```

지원 범위는 outbound IPv4 TCP에서 같은 길이의 단일 토큰 치환입니다. 전체
OOB queue, busy-threshold backpressure, 가변 길이 편집과 IPv6에는 추가
상태 머신과 검증이 필요합니다.
