# NTL NDIS LWF monitor

이 예제는 pass-through NDIS lightweight filter 기준 구현입니다.

`ntl::ndis::lightweight_filter<Module>`이 등록, attach, restart, pause,
detach, send/complete, receive/return, OID, status/PnP 순서와 pass-through를
소유합니다.
Module에는 callback 동안만 유효한 view만 전달되므로 원본 NBL을 보관하거나
완료할 수 없고, 짝이 되는 forward·complete·return 호출을 빠뜨릴 수도
없습니다.

다음 항목을 관찰합니다.

- module attach와 restart/pause 수명주기
- send, completion, receive NBL 및 byte 수
- checksum, LSO, RSC, VLAN, receive-hash metadata
- regular/direct OID request의 투명한 전달, 완료, 취소
- status 및 device/network PnP 관찰과 전달
- send 완료 또는 receive 반환 시 해제되는 metadata 보존 context
- out-of-order, 손실/재전송, overlap 거부, FIN, 32비트 sequence
  wraparound를 포함하는 bounded TCP reassembly load-time 계약
- 하위 OID 호출의 즉시 완료 및 pending 분기, cancel 순서,
  `NDIS_RECEIVE_FLAGS_RESOURCES` 즉시 receive 반환을 강제하는 load-time 계약

컨트롤러는 외부 UDP 송신과 선택된 경로의 외부 next hop을 대상으로 하는 ICMP
요청/응답을 발생시키고 send/receive counter 증가를 모두 확인합니다. 설치는
일반 서비스 생성이 아니라 network component INF로 해야 합니다.
`netcfg -l <inf> -c s -i crtsys_ntl_lwf_monitor`로 설치하고
`netcfg -u crtsys_ntl_lwf_monitor`로 제거합니다.

이 드라이버는 일회용 VM에서만 설치하고 실행하십시오. 이 예제는 실제
traffic을 차단, clone 또는 생성하지 않으며 packet metadata도 변경하지
않습니다. 별도 compile 계약이 mutation API를 검증합니다. 이 API는 filter가
실제로 바꾼 field만 복구하고 miniport의 LSO completion 결과는 보존합니다.

선택한 adapter stack이 Direct OID, device-PnP, native cancel 또는 resource
제한 receive를 자연스럽게 발생시키지 않으면 해당 counter는 0일 수 있습니다.
등록과 공통 전달 경로는 항상 컴파일되며 결정적 edge 계약은 driver load 때
실행됩니다.
