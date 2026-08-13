# 사용자 connect-redirect 허용성 테스트

이 픽스처가 루프백 IPv4/IPv6 원본 서버, 클라이언트, 판정, PASS 출력을
소유합니다. 제품 `*_proxy_service`를 실행하고 `controller.ready`를 기다린 뒤
트래픽을 보내며, `stop.request`와 `controller.stats`로 종료와 통계를
검증합니다. 원래 대상 캡처, 코루틴 바이트 카운터, 정책 제거,
사용할 수 없는 프록시에서의 실패 시 차단, 원본 서버 우회 방지, 복구까지 확인합니다. WFP
세션을 열거나 드라이버를 제어하지 않습니다.
