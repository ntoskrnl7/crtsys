# 사용자 connect-redirect acceptance

이 fixture가 loopback IPv4/IPv6 origin, client, assertion, PASS 출력을
소유합니다. 제품 `*_proxy_service`를 실행하고 `controller.ready`를 기다린 뒤
트래픽을 보내며, `stop.request`와 `controller.stats`로 종료와 통계를
검증합니다. original-destination capture, coroutine byte counter, 정책 제거,
unavailable-proxy fail-closed, origin bypass 방지, 복구까지 확인합니다. WFP
session을 열거나 드라이버를 제어하지 않습니다.
