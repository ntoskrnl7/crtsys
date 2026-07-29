# NTL NDIS LWF monitor

이 예제는 읽기 전용 NDIS lightweight filter 기준 구현입니다.

`ntl::ndis::lightweight_filter<Module>`이 등록, attach, restart, pause,
detach, send/complete, receive/return 순서와 pass-through를 소유합니다.
Module에는 callback 동안만 유효한 view만 전달되므로 원본 NBL을 보관하거나
완료할 수 없고, 짝이 되는 forward·complete·return 호출을 빠뜨릴 수도
없습니다.

다음 항목을 관찰합니다.

- module attach와 restart/pause 수명주기
- send, completion, receive NBL 및 byte 수
- checksum, LSO, RSC, VLAN, receive-hash metadata
- out-of-order, overlap 거부, FIN, 32비트 sequence wraparound를 포함하는
  bounded TCP reassembly load-time 계약

컨트롤러는 UDP 송신을 발생시키고 send counter 증가를 확인합니다. 설치는
일반 서비스 생성이 아니라 network component INF로 해야 합니다.
`netcfg -l <inf> -c s -i crtsys_ntl_lwf_monitor`로 설치하고
`netcfg -u crtsys_ntl_lwf_monitor`로 제거합니다.

이 드라이버는 일회용 VM에서만 설치하고 실행하십시오. 이 예제는 실제
traffic을 수정, 차단, clone 또는 생성하지 않습니다.
