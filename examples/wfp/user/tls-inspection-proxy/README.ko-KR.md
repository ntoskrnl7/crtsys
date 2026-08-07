# 사용자 모드 WFP TLS 검사 프록시

[English](./README.md)

이 디렉터리에는 실제 배포 경로에 해당하는 구성만 둡니다.

- `crtsys_wfp_tls_inspection_proxy.sys`: 선택한 IPv4/IPv6 연결 redirect
- `crtsys_wfp_tls_inspection_proxy_service.exe`: 임시 WFP 정책, 원래 목적지와
  redirect record 복구, SNI 인증서 선택, Schannel 양방향 TLS, ALPN 일치,
  bounded HTTP/1.1·HTTP/2 검사와 변환, 운영 통계
- `crtsys_wfp_tls_inspection_proxy_acceptance.exe`: 별도 디렉터리
  `test/wfp/runtime/fixtures/user/tls-inspection-proxy`에 있는 검증 프로그램

loopback origin, 제어된 client, 비정상 TLS 입력 생성, PASS 판정은 예제에서
제거하고 acceptance fixture로 옮겼습니다. 서비스와 fixture는 다음의 bounded
파일 IPC를 사용합니다.

1. `--ready-file`: 인증서·listener·정책 준비 완료
2. `--remove-policy-file`: 임시 WFP 정책 제거 요청
3. `--policy-removed-file`: direct 연결 검증 가능 확인
4. `--stop-file`: 서비스 종료 요청
5. `--stats-file`: 프로토콜·변환·SNI·tuple·실패 통계

실제 데이터 경로 기능은 그대로 유지됩니다. IPv4/IPv6 redirect record,
검증된 두 TLS 구간, bounded ClientHello와 SNI identity cache, 양쪽 TLS 구간의
필수 `http/1.1` 또는 `h2` ALPN, HTTP/1 framing, HTTP/2 frame/HPACK 상태,
요청 header 변환, HTML 응답 변환, 내용 기반 fail-closed 차단을 수행합니다.

기본 HTTP/2 redirect session은 변환된 모든 `:authority`를 전달 전에 단일
upstream TLS 연결을 선택한 SNI와 결합해 검증합니다. tunnel handler가 inspect
또는 passthrough를 명시적으로 승인하지 않은 일반 CONNECT와 Extended CONNECT도
전달 전에 차단합니다. SNI 결합 검증은 제품이 별도의 명시적 origin coalescing 정책을
제공할 때만 해제해야 합니다.

## 범용 API와 예제의 경계

서비스가 TLS/HTTP 프록시 연결 절차를 직접 다시 구현하지는 않습니다. 반복되는
제품 경로는 다음 공개 NTL 구성 요소가 담당합니다.

- `redirected_tls_session_registry`: 연결 수를 제한하고 연결마다 detached
  thread를 만들지 않은 채 coroutine 수명을 종료 시점까지 drain
- `redirected_tls_session`: bounded ClientHello 검증, 원래 WFP tuple과 정확한
  process/application identity 복구, 원래 목적지로 비동기 연결, 하나의 선택된
  ALPN으로 양쪽 Schannel 구간 수립
- `standard_redirected_tls_inspection`: 프로토콜 중립 정책 하나를 둘러싼 연결별
  HTTP/1·HTTP/2 framing, 요청 연결 관계, HPACK/flow-control, Upgrade와 Extended
  CONNECT 상태 제공

따라서 `app`의 파일 역할은 좁고 명확합니다.

- `inspection_policy.{hpp,cpp}`: 이 예제에만 해당하는 정책
- `proxy_engine.{hpp,cpp}`: 범용 dispatcher 주변의 예제용 진단
- `main.cpp`: listener, 임시 WFP 정책, 인증서와 서비스 수명

합성 트래픽과 frame assertion은 `test` 또는 별도 runtime fixture에만 있습니다.
특히 HTTP/2 frame 생성·assert 계약 코드는 서비스에 링크되지 않습니다.

허용·차단 판단은 본문에만 한정되지 않습니다. HTTP/1과 HTTP/2 adapter 모두
동일한 staged `inspection_context_view`를 제공하므로 method, 경로, header, body, TLS,
process/application identity와 원래 WFP tuple을 한 규칙에서 조합할 수 있습니다.

```cpp
namespace c = ntl::net::http::condition;

policy.requests()
    .at_message_complete()
    .when(c::all_of(
        c::method_is("POST"),
        c::path_is("/inspect"),
        c::original_destination_port_is(443),
        c::any_of(c::header_is("x-ntl-block", "1"),
                  c::complete_body_contains("BLOCKME"))))
    .decide([](const ntl::net::http::inspection_context_view &) {
      return ntl::net::inspection::verdict::block;
    });
```

`block`은 HTTP/1과 HTTP/2에서 bounded semantic 403 응답을 만들고,
`drop_flow`는 전송 연결을 닫습니다. 비정상 입력이나 상한 초과는 설정된
fail-closed 정책을 따릅니다.

## 인증서 경계

제어된 서비스는 임시 private CA를 만들고, 별도 fixture 프로세스가 origin을
열 수 있도록 origin leaf만 `LocalMachine\My`에 잠시 게시합니다. CA는 private
IPC 디렉터리에 공개 인증서만 내보내며 client와 upstream은
`certificate_authority_policy`로 검증합니다. 신뢰 root는 설치하지 않습니다.
종료할 때 leaf, key, CA key와 IPC 디렉터리를 모두 정리합니다.

제품은 관리자 승인 issuer, 배포·회전·감사·고지·예외·실패 정책을 별도로
제공해야 합니다. NTL은 제품용 interception root를 몰래 만들거나 신뢰시키지
않습니다.

## 빌드와 검증

```powershell
cmake -S examples\wfp\user\tls-inspection-proxy `
      -B artifacts\examples\wfp-user-tls -A x64 -DBUILD_TESTING=ON
cmake --build artifacts\examples\wfp-user-tls --config Debug
ctest --test-dir artifacts\examples\wfp-user-tls `
      -C Debug --output-on-failure
```

폐기 가능한 VM에서 드라이버를 테스트 서명·로드한 뒤 관리자 셸에서 실행합니다.

```powershell
.\crtsys_wfp_tls_inspection_proxy_acceptance.exe
```

acceptance는 인접한 서비스를 시작하고 IPv4/IPv6 HTTP/1.1·HTTP/2 허용/차단,
요청·응답 변환, 양쪽 주소군의 비정상 TLS, 실제 WFP process/application
identity handoff, 정책 제거 뒤 direct TLS와 서비스 통계를 검증합니다. 성공
표시는 다음으로 시작합니다.

```text
NTL WFP TLS inspection acceptance PASS:
```

드라이버를 설치하지 않는 CTest는 동일한 HTTP/1·HTTP/2 정책뿐 아니라
pointer-free redirect handoff record와 bounded public redirected-session 계약도
검증합니다.

## 제어된 실제 HTTPS 검사

기존의 호출자 지정 HTTPS 검증은 별도 `https-live` acceptance 실행 파일로
유지했습니다. 따라서 생성된 client와 성공 판정은 proxy service에 없습니다.

```powershell
.\crtsys_wfp_tls_inspection_proxy_live_acceptance.exe `
    --inspect-host $env:NTL_WFP_TEST_HOST
```

공개 사이트를 코드에 고정하지 않습니다. 호출자가 승인된 host를 선택하며 필터는
현재 실행 파일, bounded IPv4 후보 하나, TCP 443에만 적용됩니다. upstream은
Windows의 정상 chain/name 검증을 사용합니다. `--allow-unavailable-revocation`은
누락·offline revocation 정보만 제한적으로 허용하며 신뢰되지 않은 인증서,
이름 불일치, 만료, 실제 revoke 결과는 계속 실패합니다.
