# 미니필터 Verifier 스트레스

이 픽스처는 런타임 통신 포트를 반복해서 열고 닫은 뒤, 설치된 미니필터를 언로드하고 다시 로드합니다. 대상 드라이버에 Driver Verifier가 이미 구성된 관리자 권한 테스트 환경에서 실행하세요. 픽스처는 Verifier 설정을 변경하지 않습니다.

```text
crtsys_flt_verifier_stress_app.exe [iterations] [filter-name] [port-name]
```

기본 이름은 `test/flt/runtime`과 일치합니다. 필터를 언로드하기 전에 모든 포트 핸들을 닫으므로, 통신 포트 정리와 다음 로드의 등록 경로를 특히 검사합니다. 첫 연결, 언로드 또는 다시 로드하는 동작에서 실패를 보고합니다.
