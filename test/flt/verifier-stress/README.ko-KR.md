# 미니필터 Verifier 스트레스

이 fixture는 runtime 통신 port를 반복해서 열고 닫은 뒤, 설치된 미니필터를 unload하고 다시 load합니다. 대상 driver에 대해 Driver Verifier가 이미 구성된 관리자 권한 테스트 환경에서 실행하십시오. fixture는 verifier 설정을 바꾸지 않습니다.

```text
crtsys_flt_verifier_stress_app.exe [iterations] [filter-name] [port-name]
```

기본 이름은 `test/flt/runtime`과 일치합니다. filter를 unload하기 전에 모든 port handle을 닫으므로 통신 port teardown과 다음 load의 등록 경로를 특히 검사합니다. 첫 connection, unload 또는 reload 연산에서 실패를 보고합니다.
