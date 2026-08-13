# WFP 런타임 허용성 검사

런타임 테스트는 관찰 가능한 샘플 동작별로 묶습니다. 픽스처 이름은 검증하는 네트워크 결과를 나타냅니다.

| 샘플 | 런타임 게이트 |
| --- | --- |
| [`ale-connect-block`](./ale-connect-block) | 선택한 아웃바운드 IPv4 TCP 연결은 `WSAEACCES`로 거부되고 세션 제거 시 복원됩니다. 영구 매니페스트 조정, 컨트롤러 종료 뒤 생존, 상태 확인, 명시적 제거를 검증하며 드라이버는 Driver Verifier에서 로드/언로드됩니다. |
| [`advanced`](./advanced) | 이중 스택 데이터그램 리디렉션, 지연된 비동기 검사, flow/stream 원격 분석, UDP 콘텐츠 판정, 프레임 TCP 콘텐츠 판정, 로컬 TCP 연결 리디렉션, bind 리디렉션, 기능을 과장하지 않는 IPsec/MAC/vSwitch/고속/엔드포인트 종료 관찰을 선택 드라이버를 함께 대상으로 하는 Driver Verifier에서 실행합니다. stream-edit는 IOCP 읽기/쓰기/취소/EOF와 동적 프레이밍도 검사합니다. |
| [`https-live`](./https-live) | 브라우저를 시작·종료·프로필 변경·플래그 추가하지 않고 이미 실행 중인 일반 브라우저를 관찰하는 인터넷 의존 호스트 검사입니다. IPv4/IPv6 TCP HTTPS 응답을 제한된 HTML로 기록하고, 네이티브 WFP UDP/443 필터는 같은 실행의 인벤토리와 classify-drop 증거로 검증합니다. |

[고급 허용성 검사 안내서](./advanced/README.ko-KR.md#hyper-v-vswitch-및-ipsec-증적)에는
실제 Hyper-V vSwitch 분류와 활성 전송 모드 TCP/UDP IPsec 실행에 필요한 추가
토폴로지, 증적 및 정리 조건을 기록합니다.

각 VM 게이트는 VM 경로, 자격 증명, 스테이징 디렉터리를 매개변수로 받습니다. 운영자는 일회용 게스트를 미리 부팅하고 선택한 Driver Verifier 대상을 미리 구성합니다. 실행기는 VM을 재부팅·reset·revert하지 않고 Driver Verifier도 변경하지 않으며, 전후 상태만 검증합니다. 드라이버 서비스 또는 인증서를 설치하는 제품군에는 명시적 승인과 호출자가 만든 일회용 게스트 센티널도 필요합니다. 어떤 테스트도 특정 체크아웃, 사용자 계정, VM 이름에 묶이지 않습니다.
