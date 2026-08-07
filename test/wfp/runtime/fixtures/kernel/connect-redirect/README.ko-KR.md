# 커널 connect-redirect acceptance

이 traffic 전용 fixture는 제품 controller를 실행하고 정책 ready를 기다린 뒤
드라이버의 WSK proxy를 통과하는 IPv4/IPv6 loopback echo를 생성합니다. 종료를
요청하고 opaque redirect-record 수, 양방향 relay byte, origin unavailable
fail-closed counter, 정책 제거, direct 복구를 검증합니다. WFP 관리나 device
control 호출은 포함하지 않습니다.

인자 없이 실행하면 같은 디렉터리의 controller를 찾아 고유한 임시 IPC
디렉터리를 직접 소유합니다. fixture를 따로 디버깅할 때만
`acceptance.exe <controller.exe> <ipc-directory>` 형태로 두 경로를 명시할 수
있습니다.
