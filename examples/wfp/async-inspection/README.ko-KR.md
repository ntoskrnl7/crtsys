# WFP async-inspection 한국어 설명

WFP 분류 함수는 보통 그 자리에서 허용·차단을 결정합니다. 그러나 실제
제품에서는 사용자 모드 정책 서비스나 평판 조회 결과를 기다려야 할 수
있습니다. 이 샘플은 그때 필요한 ALE 비동기 수명 규칙을 보여줍니다.

동작 순서:

1. 컨트롤러가 허용 포트와 차단 포트 규칙을 설치합니다.
2. 첫 `ALE_AUTH_CONNECT_V4` 호출에서 드라이버가
   `FwpsPendOperation`을 호출합니다.
3. 현재 분류는 `block-and-absorb`로 끝내 원래 연결을 임의로 통과시키지
   않습니다.
4. PASSIVE_LEVEL 작업 항목이 100ms 뒤 operation을 완료합니다.
5. WFP가 같은 연결을 reauthorize합니다.
6. reauthorize 호출에서는 규칙에 고정된 permit 또는 block만 반환합니다.

`ntl::wfp::pended_operation`은 이동 전용이며 소멸 시에도 미완료 operation을
정확히 한 번 완료합니다. 드라이버는 rundown protection으로 모든 작업이
끝나기 전 언로드되지 않습니다.

판단 결과는 filter context에 고정하여 pend/complete/reauthorize 수명만
독립적으로 검증합니다. 제품에서는 이 수명 모델에 bounded 사용자 모드
정책 broker를 연결하고 IPv6·IPsec 정책을 별도로 정의할 수 있습니다.
