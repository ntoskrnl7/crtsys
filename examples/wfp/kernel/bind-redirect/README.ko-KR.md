# NTL WFP 바인드 리디렉션

이 예제는 `ALE_BIND_REDIRECT_V4`와 `ALE_BIND_REDIRECT_V6`를 함께
검증합니다. 사용자 코드에는 쓰기 가능한 `FWPS_BIND_REQUEST0`를 노출하지
않습니다.

컨트롤러는 `--application`으로 명시한 실행 파일에만 적용되는 IPv4/IPv6
UDP 동적 필터를 설치합니다. loopback의 0번 포트로 bind하면 드라이버가 소유한 시험용
주소와 포트로 변경됩니다. 필터는 완전한 주소 구조를 64비트 context에
억지로 넣지 않고 불투명한 selector만 전달합니다. 드라이버가 selector를
완전한 target으로 매핑하며, 알 수 없는 selector와 UDP가 아닌 요청은
fail-closed로 거부합니다.

예제와 런타임 검증은 서로 다른 실행 파일입니다.
`crtsys_wfp_bind_redirect_controller.exe`는 WFP 정책 수명만 소유합니다.
소켓을 만들고 PASS를 판정하는 코드는
`test/wfp/runtime/fixtures/kernel/bind-redirect`에 있으며
`crtsys_wfp_bind_redirect_acceptance.exe`로 빌드됩니다. acceptance는 자신의
실행 경로를 controller에 넘기고 ready 신호 뒤 IPv4/IPv6 bind를 발생시킨
다음 controller를 종료합니다. 그 뒤 새 bind가 일반 ephemeral 포트로
복원됐는지 확인합니다. fixture는 WFP 정책 API를 호출하지 않고 controller는
시험용 네트워크 흐름을 만들지 않습니다.

생성된 드라이버 패키지는 일회용 시험 VM에만 설치하십시오.
