# WFP 커널 specialized-observation

[English](./README.md)

이 샘플은 엔드포인트 종료, MAC 프레임, vSwitch 프레임, 고속 전송, IPsec
관리 같은 특수 WFP 계층을 모델링합니다. 드라이버는 정적 콜아웃을
허용하는 엔드포인트/MAC/vSwitch 계층에만 관찰 전용 콜아웃을 등록합니다.
컨트롤러는 고속 및 IPsec 관리 계층을 검사하며, 이 계층이
정적 콜아웃을 허용하는 것처럼 표현하지 않습니다.

책임은 다음처럼 분리됩니다.

- `crtsys_wfp_specialized_observation`: 드라이버
- `crtsys_wfp_specialized_observation_controller`: 계층 가용성을 확인하고
  타입이 지정된 통계 디바이스를 열며, 지정된 애플리케이션의 관찰 정책과 전후 통계를
  관리합니다.
- `crtsys_wfp_specialized_observation_acceptance`:
  `test/wfp/runtime/fixtures/kernel/specialized-observation`의 픽스처. 실제
  IPv4/IPv6 endpoint를 만들고 통계를 판정합니다.

컨트롤러 계약:

```text
--application-path <실행 파일 절대 경로>
--ready-file <경로> --stop-file <경로> --stats-file <경로>
[--duration-ms <100..300000>]
```

endpoint closure filter는 애플리케이션 범위이므로, 컨트롤러 프로세스가 아니라
실제로 소켓을 만드는 픽스처 실행 파일 경로를 전달해야 합니다. 허용성 검사는
기본적으로 같은 디렉터리의 컨트롤러를 실행하며 `--controller <경로>`도
지원합니다.

환경 의존적인 MAC 및 vSwitch 게이트는 다음 인자를 추가로 사용합니다.

```text
--traffic-target <ICMP echo에 응답하는 IPv4 주소>
--traffic-duration-ms <100..300000>
--require-mac <true|false>
--require-vswitch <true|false>
```

기본 실행은 결정적인 dual-stack endpoint만 요구합니다. 실제 Ethernet
게이트는 `--require-mac true`, Hyper-V 토폴로지 게이트는
`--require-vswitch true`를 추가합니다. 요구한 양방향 classify counter가
증가하지 않으면 시험은 실패합니다.

```powershell
cmake -S examples\wfp\kernel\specialized-observation `
      -B artifacts\examples\wfp-specialized-observation -A x64
cmake --build artifacts\examples\wfp-specialized-observation --config Debug
```

선택적인 MAC/vSwitch counter가 0인 것은 현재 장비가 그 기능을 통과하지
않았다는 뜻입니다. 기본 endpoint 게이트에서만 0을 허용하며, 명시적인 기능
요구 옵션을 지정하면 해당 counter가 반드시 증가해야 합니다.
