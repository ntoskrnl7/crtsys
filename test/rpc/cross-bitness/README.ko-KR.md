# NTL RPC 32/64비트 호환 픽스처

이 픽스처는 x64 NTL RPC 드라이버와 x86 WOW64 클라이언트를 연결합니다. 이식 가능한 스키마 범위에서 RPC 직렬화가 네이티브 포인터 폭과 무관한지 검증합니다.

검사하는 값은 다음과 같습니다.

- 고정 폭 signed/unsigned 정수 경계값, `float`, `double`, `bool`, 명시적인 기반 타입을 갖는 enum
- 포함된 null 문자가 있는 `string`, `wstring`
- 비어 있거나 큰 `vector`, 중첩 vector, `array`, `list`, `deque`, `set`, `multiset`, `unordered_set`, `map`, `multimap`, `unordered_map`, `pair`, `tuple`
- `optional`, `variant`, 중첩된 사용자 정의 직렬화 객체
- 서버 콜백을 실행하기 전에 실패해야 하는 제한 응답 불일치
- 선언된 클라이언트 타입이 반환 페이로드보다 넓은 응답
- 잘린 입력, 과도하게 큰 연속/중첩 컨테이너 수, 한도 초과 요청, 알 수 없는 콜백 ID 뒤에 유효한 요청을 보내 엔드포인트를 계속 사용할 수 있는지 확인하는 경우
- 제한된 가장 토큰을 거부하는 기본 보안 엔드포인트
- 하나의 불변 디스패치 테이블을 호출하는 동시 클라이언트
- 활성 콜백은 종료할 수 있게 두면서 정리 중 새 호출은 거부하는 서버 rundown
- 정확한 버전, 전송 기능, 애플리케이션 기능, 정렬된 x64 서버 메서드 목록을 포함한 x86 클라이언트의 계약 검색
- 각 호환성 진단을 발생시키는 의도적인 버전, 기능, 메서드, 전송 기능 불일치
- 고정 레이아웃 SPSC 링을 사용하는 양방향 세션 결합 공유 메모리, 제한된 배압과 영역 등록 해제 뒤의 오래된 토큰 거부

`size_t`, `ptrdiff_t`, `uintptr_t`, `ULONG_PTR`처럼 네이티브 폭을 갖는 값과 포인터를 포함하는 구조체는 이식 가능한 RPC 스키마 타입이 아닙니다. 이 픽스처는 크기와 차이 값을 각각 `uint64_t`, `int64_t`로 전달합니다. 직렬화기는 기본 값을 네이티브 `sizeof`로 기록하므로, 네이티브 폭 별칭은 x86 앱에서 4바이트, x64 드라이버에서 8바이트로 인코딩됩니다.

두 아키텍처를 따로 구성하세요. Win32 트리에서는 앱 대상만, x64 트리에서는 드라이버 대상만 빌드한 뒤 VM에서 함께 실행합니다.

```powershell
cmake -S . -B build_x64 -A x64
cmake --build build_x64 --config Release `
  --target crtsys_rpc_cross_bitness_driver crtsys_rpc_cross_bitness_app

cmake -S . -B build_x86 -A Win32
cmake --build build_x86 --config Release `
  --target crtsys_rpc_cross_bitness_app
```

예상되는 x64 드라이버/x86 클라이언트 결과:

```text
RPC cross-bitness PASS: client=x86 server=x64 boundary=1 empty=1 large_bytes=131072 large_numbers=32768 bounded_response=1 malformed=5 security=1 contract=4 shared_memory=2 stale_token=1 concurrent=1024 rundown=1
```
