# WFP flow-monitor 한국어 설명

이 샘플은 트래픽을 허용하거나 차단하지 않고 선택한 TCP 흐름의 수명과
바이트 수만 관찰합니다.

동작:

1. `ALE_FLOW_ESTABLISHED_V4`에서 outbound TCP 흐름이 시작됐음을 확인합니다.
2. 그 flow handle에 `monitor_flow` typed context를 연결합니다.
3. `STREAM_V4` 호출마다 indication 수, 바이트 수, missed byte를 원자적으로
   누적합니다.
4. 흐름이 삭제되면 context 소멸자가 closed 수를 올립니다.
5. 컨트롤러는 관리자·SYSTEM 전용 control device의 읽기 전용 IOCTL로
   통계를 가져옵니다.

두 WFP 필터 모두 inspection action이므로 콜백이 실수로 block을 반환해도
`ntl::wfp` adapter가 `continue`로 정규화합니다. 즉 관찰 샘플이 정책 집행
샘플로 변질되지 않도록 API 구조에서 제한합니다.

드라이버 적재 시 bounded coroutine reader 자체 시험도 실행합니다. 헤더와
본문을 여러 조각으로 전달해 연속 `read_exactly()`가 `PASSIVE_LEVEL`에서
재개되는지 확인하고, timeout, cancel, EOF, 동시 두 번째 reader, buffer
limit 경로가 정확히 한 번만 완료되는지도 검사합니다. 입력은 즉시
nonpaged buffer로 복사되므로 callback 범위의 WFP 포인터를 보관하지 않습니다.

지원 범위는 flow context의 생성·바이트 관찰·삭제·언로드 수명과 사용자
모드 통계 전달입니다. 애플리케이션 프로토콜 분석, payload 보관과 감사 로그
영속화는 제품 정책에 맞는 consumer에서 구성합니다.
