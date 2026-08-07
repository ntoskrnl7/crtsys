# WFP 커널 stream-edit

[English](./README.md)

이 샘플은 선택한 outbound IPv4 TCP stream을 수정합니다. inline 경로는
indication 경계를 가로지르는 `BLOCKME`를 같은 길이의 `REDACT!`로 바꿉니다.
bounded out-of-band 경로는 pass-through 데이터를 복제하고 `OOBBLOCK`에 대해
가변 길이 replacement를 주입할 수 있습니다.

예제에는 실제 정책과 데이터 경로만 남겨 두었습니다.

- `crtsys_wfp_stream_edit`: stream buffering, 주입 완료, bounded OOB 작업을
  담당하는 드라이버
- `crtsys_wfp_stream_edit_controller`: 선택한 포트의 ephemeral 정책을
  설치하는 컨트롤러. 소켓이나 코루틴 시험을 만들지 않습니다.
- `crtsys_wfp_stream_edit_acceptance`:
  `test/wfp/runtime/fixtures/kernel/stream-edit`의 fixture. client/server,
  경계 분할, OOB, 정책 제거, 코루틴 계약을 검증합니다.

컨트롤러 계약:

```text
--port <1..65535>
--ready-file <경로> --stop-file <경로> --stats-file <경로>
[--duration-ms <100..300000>]
```

기본 acceptance는 같은 디렉터리의 컨트롤러를 자식 프로세스로 실행하고,
ready 이후 트래픽을 보낸 뒤 stop과 stats로 종료를 동기화합니다.
`--controller <경로>`로 다른 컨트롤러를 지정할 수도 있습니다.

소켓/코루틴 계약은 드라이버 없이 host에서 실행됩니다.

```powershell
crtsys_wfp_stream_edit_acceptance.exe --coroutine-contract
ctest --test-dir artifacts\examples\wfp-stream-edit -C Debug
```

이 모드는 IOCP read/write, 취소, EOF, 분할된 prefix, 합쳐진 framed message와
드라이버가 실제 사용하는 bounded OOB pending budget을 검증합니다. 해당 시험
코드는 `test`에만 있으며 제품 드라이버 load 중에는 합성 self-test를 실행하지
않습니다.

```powershell
cmake -S examples\wfp\kernel\stream-edit `
      -B artifacts\examples\wfp-stream-edit -A x64
cmake --build artifacts\examples\wfp-stream-edit --config Debug
```
