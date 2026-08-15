# NTL NDIS 예제

첫 NDIS 기준 예제는 [`lwf-monitor`](./lwf-monitor/README.ko-KR.md)입니다.
`ntl::ndis` 내부에서 forwarding ownership을 관리하고, callback 범위의
NBL/MDL 데이터를 bounded `ntl::net::scatter_view`로 제공하며, offload metadata를
변경하지 않고 관찰합니다. 복사된 payload에만 사용하는 bounded TCP
reassembler도 함께 검증합니다.

`send_event`와 `receive_event`에서는 `try_preserve_metadata()`로 metadata
보존을 준비할 수 있습니다. 반환된 mutable view를 통해 실제로 변경한 field만
send 완료 또는 receive 반환 시 복구합니다. 단, send 완료의 LSO slot에는
miniport completion 결과가 들어가므로 그 값은 그대로 상위로 전달합니다.
Adapter는 regular/direct OID request를 clone하여 전달하고 callback 범위의
request/completion view를 제공합니다. Status와 device/network PnP 알림도
읽기 전용 module 관찰 후 원래 방향으로 전달합니다. Synchronous OID
interception은 이 NDIS 6.30 foundation의 범위에 포함하지 않습니다.

프로세스, 연결, TCP stream, 응용프로그램 내용 정책에는 WFP가 우선입니다.
Ethernet/NBL/NIC 동작 자체가 요구사항일 때 NDIS를 사용하십시오.
