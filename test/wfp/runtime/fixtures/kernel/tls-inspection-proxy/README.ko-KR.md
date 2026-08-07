# 커널 TLS 검사 acceptance fixture

이 디렉터리는 커널 TLS 프록시의 제어된 origin/client, malformed·idle 입력과
판정만 소유합니다. 인접한
`crtsys_wfp_kernel_tls_inspection_proxy_controller.exe`를 시작하며, driver
설정·임시 machine 인증서·WFP 정책은 controller만 담당합니다.

fixture는 HTTP/1.1·HTTP/2 허용/차단, IPv4/IPv6, SNI·ALPN, 요청·응답 변환,
malformed와 timeout 실패, bounded capture record, 명시적 정책 제거, direct
복원과 정리를 검증합니다. 마지막에 controller 통계를 판정하고 PASS를
출력합니다.

fixture에는 WFP 관리, driver IOCTL, 서비스 제어 호출이 없습니다.
