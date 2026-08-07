# WFP 커널 flow-monitor

[English](./README.md)

이 관찰 전용 샘플은 선택한 outbound IPv4/IPv6 TCP flow에 typed 상태를
연결하고, 트래픽을 변경하지 않은 채 stream indication, 바이트, 누락 바이트,
flow 수명 통계를 기록합니다.

실행 책임은 다음처럼 분리됩니다.

- `crtsys_wfp_flow_monitor`: 드라이버
- `crtsys_wfp_flow_monitor_controller`: control device를 열고 전후 통계를
  수집하며 포트 범위 정책을 설치합니다. client/server, 부하 발생, PASS 판정을
  포함하지 않습니다.
- `crtsys_wfp_flow_monitor_acceptance`:
  `test/wfp/runtime/fixtures/kernel/flow-monitor`의 fixture. IPv4/IPv6 서버와
  클라이언트, 부하 시험, 통계 판정을 담당합니다.

컨트롤러 계약:

```text
--ipv4-port <1..65535> --ipv6-port <1..65535>
--ready-file <경로> --stop-file <경로> --stats-file <경로>
[--duration-ms <100..300000>]
```

fixture는 컨트롤러를 자식 프로세스로 실행하고 ready 이후에만 트래픽을
보냅니다. 시험이 끝나면 stop을 신호하고 컨트롤러가 쓴 전후 통계를 판정합니다.

```powershell
crtsys_wfp_flow_monitor_acceptance.exe
crtsys_wfp_flow_monitor_acceptance.exe --load-test 10000 32
crtsys_wfp_flow_monitor_acceptance.exe --controller <경로> --load-test 10000 32
```

`--load-test`의 두 값은 주소 계열별 flow 수와 worker 동시성입니다.

```powershell
cmake -S examples\wfp\kernel\flow-monitor `
      -B artifacts\examples\wfp-flow-monitor -A x64
cmake --build artifacts\examples\wfp-flow-monitor --config Debug
```
