# NTL RPC 교차 비트수 픽스처

이 fixture는 x64 NTL RPC driver와 x86 WOW64 client를 연결합니다. 이식 가능한 schema 표면에서 RPC 직렬화가 네이티브 pointer 폭과 무관함을 검증합니다.

검사하는 값은 다음과 같습니다.

- 고정 폭 signed/unsigned 정수 경계값, `float`, `double`, `bool`, 명시적 underlying type을 갖는 enum
- 내장 null 문자를 포함하는 `string`, `wstring`
- 빈 값과 큰 `vector`, 중첩 vector, `array`, `list`, `deque`, `set`, `multiset`, `unordered_set`, `map`, `multimap`, `unordered_map`, `pair`, `tuple`
- `optional`, `variant`, 중첩된 사용자 정의 직렬화 객체
- server callback 실행 전에 실패해야 하는 제한된 응답 불일치
- 선언된 client type이 반환 payload보다 넓은 응답
- 잘린 입력, 과도하게 큰 연속/중첩 container count, 한도 초과 요청, 알 수 없는 callback ID 뒤에 유효한 요청을 보내 endpoint가 계속 사용 가능한지 확인하는 경우
- 제한된 impersonation token을 거부하는 기본 보안 endpoint
- 하나의 불변 dispatch table을 호출하는 동시 client
- 활성 callback의 종료를 허용하면서 teardown 중 새 호출을 거부하는 server rundown
- 정확한 version, transport feature, application capability, 정렬된 x64 server method 목록을 포함한 x86 client의 contract discovery
- 각 호환성 diagnostic을 생성하는 의도적인 version, capability, method, transport-feature 불일치
- 고정 layout SPSC ring을 사용하는 양방향 session-bound shared memory, 제한된 backpressure와 region unregister 뒤 stale-token 거부 포함

`size_t`, `ptrdiff_t`, `uintptr_t`, `ULONG_PTR`와 pointer를 포함하는 구조체 같은 네이티브 폭 값은 이식 가능한 RPC schema 형식이 아닙니다. 이 fixture는 size와 difference 값을 대신 `uint64_t`, `int64_t`로 전달합니다. serializer는 기본 값을 네이티브 `sizeof`로 기록하므로 네이티브 폭 alias는 x86 app에서는 4바이트, x64 driver에서는 8바이트로 인코딩됩니다.

두 architecture를 별도로 구성하십시오. Win32 tree에서는 app target만, x64 tree에서는 driver target만 빌드한 뒤 VM에서 함께 실행하십시오.

```powershell
cmake -S . -B build_x64 -A x64
cmake --build build_x64 --config Release `
  --target crtsys_rpc_cross_bitness_driver crtsys_rpc_cross_bitness_app

cmake -S . -B build_x86 -A Win32
cmake --build build_x86 --config Release `
  --target crtsys_rpc_cross_bitness_app
```

예상되는 x64 driver/x86 client 결과:

```text
RPC cross-bitness PASS: client=x86 server=x64 boundary=1 empty=1 large_bytes=131072 large_numbers=32768 bounded_response=1 malformed=5 security=1 contract=4 shared_memory=2 stale_token=1 concurrent=1024 rundown=1
```
