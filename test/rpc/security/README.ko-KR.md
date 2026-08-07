# NTL RPC 호출자 보안 테스트

이 fixture는 비동기 method가 시스템 worker thread로 이동하기 전에 NTL이 원래 I/O 요청자의 보안 context를 캡처하는지 검증합니다.

테스트 범위:

- 사용자 모드 requestor mode와 원래 process ID
- 비동기 dispatch 이후에도 유지되는 참조된 subject context
- 이미 열린 endpoint를 통해 적용되는 제한된 impersonation token
- 요청 deserialization과 callback 실행 전에 이루어지는 method authorization
- allow/deny security descriptor를 사용하는 `call_context::check_access()`
- 거부된 method가 application callback에 도달하지 않음
- x64/x86 client에서 동일한 고정 폭 contract

process ID는 진단 metadata이며 인증 credential이 아닙니다. method authorization은 캡처된 Windows subject context, Authenticated Users DACL, security reference monitor를 사용합니다. 이 fixture는 `SECURITY_SUBJECT_CONTEXT`를 불투명하게 취급하며 멤버를 검사하지 않습니다.
