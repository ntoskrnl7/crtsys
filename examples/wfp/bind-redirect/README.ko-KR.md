# NTL WFP bind redirect

이 예제는 `ALE_BIND_REDIRECT_V4`와 `ALE_BIND_REDIRECT_V6`를 함께
검증합니다. 사용자 코드에는 쓰기 가능한 `FWPS_BIND_REQUEST0`를 노출하지
않습니다.

컨트롤러는 현재 실행 파일에만 적용되는 IPv4/IPv6 UDP 동적 필터를
설치합니다. loopback의 0번 포트로 bind하면 드라이버가 소유한 시험용
주소와 포트로 변경됩니다. 필터는 완전한 주소 구조를 64비트 context에
억지로 넣지 않고 불투명한 selector만 전달합니다. 드라이버가 selector를
완전한 target으로 매핑하며, 알 수 없는 selector와 UDP가 아닌 요청은
fail-closed로 거부합니다.

redirect된 소켓을 열어 둔 채 동적 세션을 제거한 후 새 소켓을 bind하면
다시 일반 ephemeral 포트를 받습니다. 따라서 영구 WFP 정책을 남기지
않았음도 함께 확인합니다.

생성된 드라이버 패키지는 일회용 시험 VM에만 설치하십시오.
