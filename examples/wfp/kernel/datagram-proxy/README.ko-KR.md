# WFP 커널 datagram-proxy

[English](./README.md)

이 샘플은 bounded 양방향 IPv4/IPv6 UDP tuple 변환을 보여 줍니다.
`ALE_FLOW_ESTABLISHED_V4/V6`에서 typed flow 상태를 만들고,
`DATAGRAM_DATA_V4/V6`에서 클라이언트 데이터그램을 로컬 프록시로 보내며,
`OUTBOUND_IPPACKET_V4/V6`에서 해당 프록시가 보낸 응답인지 검증합니다. 응답은
크기가 제한된 새 NBL로 복사하고 원래 원격 tuple로 복원한 뒤 network-send
경로로 재주입하여 연결된 클라이언트에 전달합니다. 컨트롤러가 종료되면
ephemeral 정책도 함께 제거됩니다.

예제와 시험 트래픽은 다음처럼 분리되어 있습니다.

- `crtsys_wfp_datagram_proxy`: 드라이버
- `crtsys_wfp_datagram_proxy_controller`: 실제 정책을 설치하고 유지하는
  컨트롤러. 소켓이나 시험 트래픽을 만들지 않고 PASS를 판정하지 않습니다.
- `crtsys_wfp_datagram_proxy_acceptance`:
  `test/wfp/runtime/fixtures/kernel/datagram-proxy`의 검증 실행 파일. IPv4/IPv6
  송수신, 배타적 리디렉션, 정책 제거 후 복구를 검증합니다.
- `crtsys_wfp_datagram_proxy_fragmented_buffer_contract`:
  `BUILD_TESTING=ON`일 때만 만드는 시험용 드라이버. 폐기 가능한 시험 VM에서
  로드하면 두 MDL 사이의 모든 UDP header 분할, 경계 간 수정, bounded copy와
  정리를 검증합니다. 이 합성 NBL 검증은 제품 드라이버 load 경로에서 실행하지
  않습니다.

컨트롤러 명령행 계약은 다음과 같습니다.

```text
--original-port <1..65535> --proxy-port <1..65535> --application <exe-path>
--ready-file <경로> --stop-file <경로> --stats-file <경로>
[--duration-ms <100..300000>]
```

컨트롤러는 정책 설치가 끝난 뒤에만 ready 파일을 만듭니다. fixture는 그
후 외부에서 트래픽을 보내고 stop 파일을 만든 뒤, 컨트롤러의 stats 파일과
정상 종료를 기다립니다. acceptance는 기본적으로 같은 디렉터리의 컨트롤러를
찾으며 `--controller <경로>`로 다른 위치를 지정할 수도 있습니다.

```powershell
cmake -S examples\wfp\kernel\datagram-proxy `
      -B artifacts\examples\wfp-datagram-proxy -A x64
cmake --build artifacts\examples\wfp-datagram-proxy --config Debug
```

지원 범위는 bounded dual-stack UDP 목적지 리디렉션, 투명한 응답 tuple 복원,
재주입 loop 방지와 안전한 주입 소유권입니다. mapping, PASSIVE 지연 작업, packet,
control data와 비동기 주입 수를 각각 제한하며 quota 또는 할당 실패 시 선택된
패킷을 흡수하고 진단 통계를 증가시킵니다. IPsec과 프록시 앱의 콘텐츠 정책은
별도의 관심사입니다.
