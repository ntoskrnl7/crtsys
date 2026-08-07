# NTL RPC 수명 주기 스트레스 테스트

이 문서는 온보딩 예제가 아니라 테스트 fixture입니다. 단일 RPC 호출로는 다룰 수 없는 수명 경계를 검사합니다.

- 동시에 수행되는 open, contract query, call, close cycle
- 짧은 수명의 모든 handle에서 이루어지는 직렬화 container 할당
- 긴 RPC callback이 server rundown 보호를 소유한 상태에서의 service 중지
- driver unload 전에 해당 진행 중 호출의 완료
- service 재시작 뒤 새 contract query와 RPC 호출
- 반복되는 외부 load, run, stop, unload, delete cycle

app 인수:

```text
crtsys_rpc_lifecycle_stress_app.exe [iterations] [workers] [service-name] [slow-ms]
```

`service-name`이 있으면 app은 진행 중인 중지를 수행하고 반환 전에 service를 다시 시작한 뒤, 재시작된 service를 다음 cycle에 사용할 수 있게 유지합니다. 기본값은 worker당 handle cycle 128회, worker 8개, 느린 callback 1500ms입니다.

## 빌드

```powershell
cmake -S test\rpc\lifecycle-stress `
  -B test\rpc\lifecycle-stress\build_x64 -A x64
cmake --build test\rpc\lifecycle-stress\build_x64 --config Debug
```
