# 사용자 모드 Schannel TLS 스트림

[NTL 문서로 돌아가기](./README.ko-KR.md)

`<ntl/net/tls/stream>`는 위에 코루틴 TLS 전송을 추가합니다.
[`async_socket`](./async-socket.ko-KR.md). Windows에서 지원하는 사용자 모드 API입니다.
채널. 관련 헤더에 제한된 ClientHello 관찰, 동적 서버 추가
ID 및 애플리케이션 프레이밍. 그들 중 누구도 WFP 정책을 설치하지 않거나
Windows 신뢰 저장소를 수정합니다.

일반 클라이언트 경로는 Windows 인증서 체인 및 호스트 이름을 사용합니다.
검증. 프로토콜 및 암호 제품군 선택은 현재 Schannel에 남아 있습니다.
`SCH_CREDENTIALS`를 통해 요청된 강력한 암호화를 사용하는 정책:

```cpp
auto credentials = ntl::net::tls_credentials::client();
ntl::net::tls_stream tls(socket, credentials);

co_await tls.handshake_client({
    .server_name = L"service.example",
    .application_protocols = {"h2", "http/1.1"},
    .require_application_protocol = true
});

auto protocol = tls.negotiated_application_protocol();
auto received = co_await tls.read_some_borrowed(buffer);
co_await tls.write_all(reply);
co_await tls.shutdown();
```

`tls_credentials`는 여러 세션에서 공유될 수 있습니다. 각 스트림은
자격 증명과 소켓 상태가 사용되므로 해당 파사드와 멤버 선언
순서는 세션 수명을 제어하지 않습니다. 모든 `tls_stream`는 하나의 TLS입니다.
이미 연결된 `async_socket`를 통한 세션.

## 인증서 정책

서버는 Schannel이 열 수 있는 개인 키가 있는 인증서를 제공합니다.

```cpp
auto credentials =
    ntl::net::tls_credentials::server(certificate);
ntl::net::tls_stream tls(socket, credentials);
co_await tls.handshake_server();
```

이미 서버 이름을 알고 있는 응용 프로그램은 다음을 사용할 수 있습니다.
`tls_server_certificate_policy`. 대신 투명한 TLS 엔드포인트가 관찰합니다.
하나의 제한된 ClientHello를 만들고 소유 ID를 선택합니다.

```cpp
auto issuer = std::make_shared<ntl::net::windows_tls_certificate_issuer>(
    authorized_ca,
    {.key_name_prefix = L"product-tls-leaf",
     .rsa_bits = 2048,
     .validity_days = 7,
     .machine_keys = true});
auto identities = std::make_shared<
    ntl::net::cached_tls_server_identity_provider>(
    issuer, 256);

auto accepted = co_await ntl::net::accept_tls(
    accepted_socket, identities);
auto &tls = accepted.stream();
auto sni = accepted.client_hello_ref().server_name();
```

`<ntl/net/tls/client_hello>`는 조각화된 TLS 레코드 및 핸드셰이크를 허용합니다.
메시지, 보유된 모든 바이트의 경계, SNI 보고, ALPN 식별자 제공,
확장 유형 `0xfe0d`가 있는지 여부와 사용된 모든 항목을 유지하는지 여부
암호문. 해당 확장은 단지 관찰일 뿐입니다. GREASE ECH는 동일합니다.
와이어 모양이며 이 파서로 구별할 수 없습니다. `accept_tls`는 다음을 제공합니다.
ID 선택 후 정확한 바이트를 Schannel에 전달합니다. 결코 재구성되지 않습니다
또는 TLS 스트림의 일부를 삭제합니다.

`<ntl/net/tls/certificate>`는 전역 CA가 아닌 인터페이스를 제공합니다.

- `tls_certificate_issuer`는 애플리케이션 주입 지점입니다.
- `windows_tls_certificate_issuer`는 단기 DNS SAN 리프에 서명합니다.
  개인 키가 Windows에서 열 수 있는 응용 프로그램 제공 CA입니다.
- `cached_tls_server_identity_provider`는 다음의 제한되고 동기화된 LRU입니다.
  리프 인증서와 재사용 가능한 Schannel 자격 증명 그리고
- 제거된 생성 리프는 지속되는 CNG 개인 키를 삭제합니다.
  마지막 ID 소유자가 이를 해제합니다.

인증서 발급은 중복 방지를 위해 캐시로 직렬화됩니다.
동일한 호스트 생성. HSM, 원격 발급자, 비동기식을 사용하는 제품
승인을 받거나 사전 프로비저닝된 매장에서 발급자/공급자를 구현해야 합니다.
자체 일정 정책과 인터페이스합니다.

클라이언트 자격 증명은 다음을 제외하고 일반 시스템 유효성 검사를 사용합니다.
`manual_peer_validation`가 명시적으로 활성화되었습니다. 수동 검증은
페일클로즈되며 `tls_peer_certificate_policy`가 필요합니다.

```cpp
auto credentials = ntl::net::tls_credentials::client({
    .manual_peer_validation = true
});
auto pin = std::make_shared<ntl::net::exact_certificate_policy>(
    expected_certificate);
ntl::net::tls_stream tls(socket, credentials);

co_await tls.handshake_client({
    .server_name = L"fixture.example",
    .certificate_policy = pin
});
```

`exact_certificate_policy`는 전체 DER 인증서를 비교합니다.
`certificate_authority_policy`는 하나를 중심으로 프라이빗 체인 엔진을 구축합니다.
애플리케이션 소유 CA이며 서버 EKU 및 호스트 이름 유효성 검사를 수행합니다.
신뢰할 수 있는 루트 저장소를 작성합니다. 이는 통제된 클라이언트와
테스트. 일반 배포 클라이언트는 해당 클라이언트를 통해 인증된 CA를 받아야 합니다.
일반 Schannel 검증을 활성화한 상태로 유지합니다.

재서명하는 관리형 네트워크의 업스트림 구간에도 동일한 규칙이 적용됩니다.
HTTPS. 해당 필터링 CA는 해당 Windows에 이미 있어야 합니다.
신뢰할 수 있는 루트 저장소. 그런 다음 `tls_credentials::client()`는 해당 체인을 검증합니다.
일반적으로; CA가 없으면 `SEC_E_UNTRUSTED_ROOT`가 의도된 것입니다.
실패 종료 결과.

인증서 해지 검사는 별도의 명시적 credential 정책입니다. 기본값은 시스템 동작을
따릅니다. 특정 검사가 필요한 애플리케이션은 최종 인증서, 전체 chain 또는 root를
제외한 chain을 선택할 수 있습니다. 명시적인 검사를 선택하지 않은 경우에는
가용성 관련 예외 옵션을 거부합니다.

```cpp
auto credentials = ntl::net::tls_credentials::client({
    .revocation_check =
        ntl::net::tls_certificate_revocation_check::
            chain_excluding_root,
    .ignore_missing_revocation_information = true,
    .ignore_offline_revocation = true
});
```

두 가용성 옵션은 Schannel에 문서화된
`SCH_CRED_IGNORE_NO_REVOCATION_CHECK`와
`SCH_CRED_IGNORE_REVOCATION_OFFLINE` 플래그에 대응합니다. chain, enhanced key
usage, host name, 만료 또는 명시적으로 확인된 인증서 해지 검증을 끄지는 않습니다.

핸드셰이크 옵션은 사용자 지정 인증서 정책을 보관합니다. 따라서 TLS 1.3
post-handshake 검증은 원래 policy facade가 해제된 뒤에도 해당 정책을 다시 사용할
수 있습니다.

애플리케이션은 요청하는 대신 하나의 명시적 클라이언트 인증서를 제공할 수 있습니다.
기본 ID를 선택하는 채널:

```cpp
auto client_credentials = ntl::net::tls_credentials::client({
    .certificate = client_certificate
});
```

상호 TLS가 필요한 서버는 인증 정책도 제공해야 합니다.
정책 없이 인증서를 요구하는 것은 핸드셰이크 전에 거부됩니다.

```cpp
ntl::net::exact_client_certificate_policy authorized(client_certificate);
co_await server_tls.handshake_server({
    .application_protocols = {"http/1.1"},
    .require_client_certificate = true,
    .client_certificate_policy = &authorized
});
```

인증서 선택 및 인증 정책은 애플리케이션 관련 사항입니다.
브라우저 검사 예제는 오리진별 클라이언트 ID를 임의로 만들어낼 수 없습니다. ID가
필요한데 구성된 ID 공급자가 제공하지 못하면 fail-closed로 처리합니다.

NTL에는 의도적으로 내장 가로채기 CA, CA 생성 지름길, 조용한 루트 인증서 설치기,
내보낸 개인 키, 인증서 검증 비활성화 스위치가 없습니다. HTTPS 검사 제품은 권한
부여, 보호된 CA 공급, 클라이언트 신뢰 배포, 감사와 고지, 우회 목록, 인증서 고정
동작, fail-open/fail-closed 정책을 명시적으로 소유해야 합니다.

## 레코드 및 바이트 스트림 동작

호출자는 TLS 레코드가 아닌 해독된 바이트를 봅니다. `tls_stream`:

- 소켓 읽기 전반에 걸쳐 단편화되고 통합된 암호문을 유지합니다.
- 다음 레코드를 위해 Schannel `SECBUFFER_EXTRA` 바이트를 보존합니다.
- TLS 1.3 핸드셰이크 이후 연속 처리
- 큰 평문 쓰기를 Schannel의 최대 메시지 크기로 나눕니다.
- 인증된 `close_notify`를 원시 전송 EOF와 구별합니다.

TLS는 애플리케이션 메시지 경계를 만들지 않습니다.
`<ntl/net/tls/framed_stream>`은 복호화된 TLS 위에 `async_framed_stream`과 같은 크기
제한 `Framer` 계약을 적용합니다. TLS read 하나에 메시지가 여러 개 들어오면 남은
접미사 바이트를 보존합니다.

```cpp
ntl::net::tls_framed_stream requests(
    tls,
    ntl::net::http::http1_message_framer{
        ntl::net::http::http1_message_kind::request},
    ntl::net::framing::frame_limits{2 * 1024 * 1024});

auto request = co_await requests.read_frame();
```

`<ntl/net/http/http1_framing>`는 제한된 HTTP/1.0 및 HTTP/1.1을 인식합니다.
`<ntl/net/http/http1_framing>`은 크기가 제한된 HTTP/1.0과 HTTP/1.1의
`Content-Length` 및 마지막 `chunked` 메시지 경계를 인식합니다. 서로 모순되는
길이, 길이와 Transfer-Encoding의 동시 사용, 폐기된 헤더 접기, 지나치게 큰
헤더·본문·청크 행·트레일러, 자체적으로 끝을 구분할 수 없는 모호한 일반 응답을
거부합니다. 호출자는 연결 종료로 구분되는 응답을 명시적으로 활성화할 수 있습니다.
이 응답은 하위 TLS 스트림이 인증된 `close_notify`를 받은 뒤에만 완료되며, 본문
크기 제한을 지키고 부분 read만으로 종료를 추론하지 않습니다. 전송 디코딩, 콘텐츠
압축 해제, 의미 HTTP 정책은 이후 단계에서 수행합니다. 검증된 HTTP/1.1 WebSocket
업그레이드는 이미 복호화된 접미사를 `release_buffered()`/`append_buffered()`로
`<ntl/net/websocket/framing>`에 넘겨 바이트 손실을 막습니다. 협상된 WebSocket
`permessage-deflate`는 전달할 프레임을 바꾸지 않고
`<ntl/net/websocket/permessage_deflate>`로 디코딩합니다. HTTP/2는 연결 방향마다
하나의 완전한 크기 제한 `<ntl/net/http2/hpack>` 디코더와
`<ntl/net/http2/framing>`을 사용합니다. HTTP/3는 `<ntl/net/http3/framing>`과
`<ntl/net/http3/qpack>`의 동적 테이블 없는 디코더 또는 별도로 선택한 복호화 QUIC
스트림 백엔드 위의 호출자 제공 상태 저장 QPACK 디코더를 사용합니다. HTTP 본문은
`<ntl/net/inspection/standard_content_decoders>`의 크기 제한
gzip/deflate/Brotli 레지스트리를 공유합니다.

`tls_stream_limits`는 보관하는 암호문과 각 하위 소켓 receive 크기를 제한합니다.
기본값은 최대 1MiB와 receive 청크 16KiB입니다. 이 제한은 애플리케이션 수준의
디코딩 크기 또는 프레이밍 제한과 별개입니다.

## 동시성, 종료 및 수명

reader 하나와 writer 하나는 동시에 활성화할 수 있습니다. 두 번째 reader, 두 번째
writer, 핸드셰이크와 종료의 경쟁은 변경 가능한 Schannel 상태를 실수로 공유하지
않고 실패합니다.

소켓, 자격 증명, 완료 컨텍스트, 애플리케이션 버퍼, 대기 중인 코루틴, 사용자 정의
인증서 정책은 해당 작업이 끝날 때까지 살아 있어야 합니다. `shutdown()`은 하위
소켓을 닫지 않고 `close_notify`를 보냅니다. 이후에도 peer의 `close_notify`를 받기
위해 read를 계속할 수 있으며, 소켓 닫기와 취소는 여전히 애플리케이션이 소유합니다.

Schannel 및 Winsock 오류는 `std::system_error`로 나타납니다. 인증되지 않은
설정된 TLS 읽기 중 전송 EOF는 오히려 오류로 보고됩니다.
깨끗한 TLS 종료보다.

## WFP 구성

[`tls-inspection-proxy`](../../examples/wfp/user/tls-inspection-proxy)는 다음과 같은
역할 분리를 보여줍니다.

1. 커널 WFP 콜아웃이 선택한 TCP 연결 하나를 로컬 프록시로 보냅니다.
2. 사용자 모드는 원래 대상 및 리디렉션 기록을 복구합니다.
3. 제한된 ClientHello 프로브는 SNI별 서버 ID를 선택합니다.
4. Schannel 세션 하나는 accept된 구간을 종료하고, 다른 세션은 outbound 구간을
   검증하고 보호합니다.
5. 크기가 제한된 HTTP/1.1 프레이밍이 복호화된 바이트 스트림에서 실행됩니다.
6. [`ntl::net::inspection`](./inspection.ko-KR.md)는 형식화된 콘텐츠 결과를 반환합니다.

커널은 TLS 키나 평문을 받지 않습니다. 따라서 TLS 라이브러리, 인증서 정책, 파서,
블로킹될 수 있는 제품 판정을 WFP classify 콜백 밖에 둘 수 있습니다.

[`browser-https-inspection`](../../examples/wfp/user/browser-https-inspection)은
브라우저를 시작·종료·재구성하지 않고 이미 실행 중인 정확한 실행 파일 경로를
관찰하는 장기 실행 워크플로를 보여줍니다. 크기가 제한된 HTTP/1.1과 다중화된
HTTP/2 HTML 로깅, gzip/deflate/Brotli 본문 디코딩, 협상된 WebSocket
`permessage-deflate`, 투명한 종단 간 HTTP/2 흐름 제어를 제공합니다.

이는 승인된 TCP/TLS 가로채기의 기반일 뿐 모든 HTTPS 클라이언트를 가로챌 수 있다는
보장은 아닙니다. 원시 `0xfe0d` 확장이 있다고 ECH가 입증되지는 않습니다. 확인된
ECH는 `ech_frontend_provider`가 inner ClientHello를 성공적으로 복구한 경우에만
검사할 수 있습니다. `downstream_trust_provider`는 가로채기 전에 알려진 인증서
고정 엔드포인트를 식별할 수 있지만, 그 엔드포인트가 다른 인증서를 받아들이게 만들
수는 없습니다. 오리진 mutual TLS에는 명시적 `origin_client_identity_provider`를
사용하며 NTL은 ID를 추측하지 않습니다.
[`http3-inspection`](../../examples/wfp/user/http3-inspection) 예시에서는
QUIC/정적-QPACK 경계를 해독했지만 투명 브라우저 HTTP/3은 여전히
제품 QUIC 터미네이터와 협상된 동적 QPACK 공급자가 필요합니다.
`<ntl/net/tls/inspection_policy>`는 누락된 각 기능을 개별적으로 나타냅니다.
모든 예외 경로는 기본적으로 페일클로즈됩니다. 참조
[HTTP 및 WebSocket 프로토콜 검사](./protocol-inspection.ko-KR.md).
