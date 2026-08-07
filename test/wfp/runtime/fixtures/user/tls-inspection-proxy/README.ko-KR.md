# 사용자 TLS 검사 acceptance fixture

이 디렉터리는 사용자 모드 TLS 프록시의 제어된 트래픽과 판정만 소유합니다.
인접한 `crtsys_wfp_tls_inspection_proxy_service.exe`를 시작하고 준비 신호를
기다린 뒤, 서비스가 게시한 단기 leaf로 IPv4/IPv6 TLS origin을 엽니다.

- 양쪽 주소군의 HTTP/1.1·HTTP/2 허용/차단
- SNI·ALPN, 요청 header와 HTML 응답 변환
- origin에 도달하지 않는 비정상 TLS 차단
- 정책 제거 확인 뒤 direct TLS 복원
- bounded 서비스 통계와 PASS 표시

fixture는 WFP를 관리하거나 driver IOCTL을 호출하거나 서비스를 변경하지
않습니다. 권한 작업과 실제 proxy 데이터 경로는 예제 서비스가 소유합니다.
