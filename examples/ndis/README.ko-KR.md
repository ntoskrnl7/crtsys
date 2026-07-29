# NTL NDIS 예제

첫 NDIS 기준 예제는 [`lwf-monitor`](./lwf-monitor/README.ko-KR.md)입니다.
`ntl::ndis` 내부에서 forwarding ownership을 관리하고, callback 범위의
NBL/MDL 데이터를 bounded `ntl::net::scatter_view`로 제공하며, offload metadata를
변경하지 않고 관찰합니다. 복사된 payload에만 사용하는 bounded TCP
reassembler도 함께 검증합니다.

프로세스, 연결, TCP stream, 응용프로그램 내용 정책에는 WFP가 우선입니다.
Ethernet/NBL/NIC 동작 자체가 요구사항일 때 NDIS를 사용하십시오.
