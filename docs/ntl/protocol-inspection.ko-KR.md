# HTTP 및 WebSocket 프로토콜 검사

[NTL 문서로 돌아가기](./README.ko-KR.md)

NTL은 완전하고 재사용 가능한 프로토콜 계약과 제품이 선택하는 백엔드를
분리합니다. 따라서 애플리케이션이 임의의 평문 바이트를 완전한 HTTP 메시지로
취급하거나, 디코더를 사용할 수 없다는 이유만으로 트래픽을 조용히 허용하는
일을 방지합니다.

## 지원 레이어

| 레이어 | NTL 공급 | 애플리케이션 또는 제품 공급 |
| --- | --- | --- |
| HTTP/1.1 | 제한된 요청/응답 프레이밍, 검증된 길이 및 청크 경계 | 의미론적 정책 및 디코딩된 본문 처리 |
| 웹소켓 | 제한된 RFC 6455 프레이밍, 조각화된 메시지 어셈블리, 의미 체계 재작성, 협상된 `permessage-deflate` 디코드/재인코딩, 마스킹 및 출력 조각화 | 하위 프로토콜 스키마 및 비표준 협상 확장 |
| HTTP/2 | 제한된 프레임 검증, HEADERS/CONTINUATION 어셈블리, HPACK, 요청/응답 연관, 흐름 제어, GOAWAY/RST 처리, 단계적 정책 및 구조화된 양방향 프록시 세션 | 의미론적 정책 및 선택적 확장 CONNECT 서브프로토콜 처리 |
| HTTP/3 | QUIC-varint HTTP/3 프레임 검증, 분할 스트림 어셈블리, 디코딩된 헤더/DATA 라우팅 및 제한된 제로 동적 테이블 QPACK 디코더 | 동적 테이블이 협상될 때 QUIC 전송, 스트림 라이프사이클 및 상태 저장 QPACK 제공자 |
| gRPC | 증분 5바이트 ​​메시지 프레이밍, 협상된 메시지 압축, 의미 체계 재작성 및 HTTP/2 또는 HTTP/3을 통한 재인코딩 DATA | Protobuf 또는 기타 애플리케이션 스키마 |
| WebTransport | Draft-16 SETTINGS, QPACK 확장 CONNECT, 원시 MsQuic 백엔드를 통한 실제 양방향/단방향 스트림과 HTTP 데이터그램, 제한된 의미 정책 | 애플리케이션 페이로드 스키마 및 배포 신뢰 모델 |
| 콘텐츠 코딩 | 제한된 디코더/인코더 레지스트리, 체인의 역순 디코딩 및 송신자 순서 인코딩, gzip/RFC 1950 deflate/Brotli 어댑터 | Zstandard, 사전 또는 독점 어댑터 |
| TLS | Schannel coroutine stream, ClientHello 관찰, ALPN, 관리형 downstream identity 선택, frontend 소유 ECH 평문 handoff, 명시적 mTLS 인증 및 제한된 감사 | 승인된 issuer/trust 배포, 실제 ECH key/configuration provider 및 제품 identity inventory |

`<ntl/net/http2/hpack>`은 완전한 RFC 7541 정적 테이블, 동적 테이블, 정수 표현,
리터럴 형식 및 Huffman 디코더를 제공합니다. 동적 테이블과 디코딩된 헤더 크기에는
명시적인 한도가 있습니다. `<ntl/net/http3/qpack>`은 두 QPACK 동적 테이블 설정이
모두 0일 때 사용하는 상호 운용 가능 프로필을 제공합니다. 차단된 헤더 스트림,
인코더 명령 및 디코더 확인 응답은 QUIC 연결 수명에 의존하므로 동적 QPACK은
공급자 계약으로 남겨 둡니다.

HTTP/2 연결 방향당 하나의 `bounded_hpack_decoder`를 유지합니다.

```cpp
ntl::net::http2::bounded_hpack_decoder decoder({
    .maximum_dynamic_table_size = 4096
});
ntl::net::http2::borrowed_connection_inspector inspector(decoder);
```

해당 디코더를 다른 연결이나 반대 방향과 공유하지 마십시오.
HPACK 동적 테이블은 방향 연결 상태입니다.

## 협상 결과에 따른 파서 선택

`<ntl/net/tls/inspection_policy>`는 협상된 ALPN 값을 정확히 하나의 파서 계열에
대응시킵니다.

```cpp
auto selected = ntl::net::inspection::select_tls_application_protocol(
    ntl::net::inspection::encrypted_transport::tcp_tls,
    tls.negotiated_application_protocol());
if (!selected.valid_for_transport)
  block_connection();
```

`h2`는 TCP의 HTTP/2를, `http/1.1`은 HTTP/1.1을, `h3`는 QUIC에서만 HTTP/3을
선택합니다. ALPN이 비었을 때는 호출자가 레거시 대체 동작을 명시적으로 켠 경우에만
HTTP/1.1을 선택합니다. WebSocket은 임의의 본문 바이트를 보고 판단하지 않고,
나중에 검증된 HTTP/1.1 Upgrade 교환으로 선택합니다.

## 페일클로즈 기능 정책

`explicit_tls_inspection_policy`는 다음과 같은 결과를 구별합니다.

- 사용할 수 있는 서버 이름이 없음
- ECH 인식 프런트엔드가 ECH를 확인했지만 내부 ClientHello를 복구하지 못함
- 인증서 고정을 포함해 다운스트림에서 인증서를 거부함
- 오리진이 상호 TLS를 요구하지만 승인된 클라이언트 ID가 없음
- 협상된 프로토콜에 활성 어댑터가 없음
- 콘텐츠 코딩에 등록된 디코더가 없음
- 검사 백엔드가 활성화되지 않았는데 QUIC가 선택됨

기본 동작은 모두 `block`입니다. 제품이 다른 동작을 의도적으로 구성할 수 있지만,
`bypass`는 암호문을 변경하지 않고 터널링한다는 뜻이지 불투명 바이트를 평문 parser에
전달한다는 뜻이 아닙니다. `metadata_only`도 해당 계층에서 실제로 얻을 수 있는
정보만 기록하는 제품 경로가 필요합니다.

GREASE ECH가 의도적으로 같은 wire 형식을 쓰므로 확장 형식 `0xfe0d`가 있다는
사실만으로 ECH를 확인할 수 없습니다. 후보 ECH 키가 있다는 사실도 성공을 뜻하지
않습니다. ECH 인식 프런트엔드가 내부 ClientHello를 인증·복호화하고 그 결과인 서버
이름을 제공한 뒤에야 확인된 ECH 연결을 검사할 수 있습니다. 이 API로 인증서 고정을
투명하게 우회할 수 없습니다. 상호 TLS에서는 어떤 클라이언트 ID가 사용자를
나타내는지 추측할 수 없습니다.

`<ntl/net/tls/product_policy>`는 이러한 관찰 결과와 실제 배포된 기능을 결합합니다.
가로채기, 변경 없는 암호문 터널링, 메타데이터 기록, 종료 또는 일반 TCP 재시도를
위한 QUIC 차단처럼 제품이 실제로 수행할 수 있는 동작만 반환합니다.
`when_possible`은 인증서 고정 또는 ECH 연결을 변경 없이 터널링할 수 있고,
`required`는 해당 연결을 종료합니다. 어느 모드도 브라우저 인증서 검증을 끄거나
불투명 트래픽을 복호화한 것처럼 가장하지 않습니다.

`<ntl/net/tls/product_backend>`는 이에 대응하는 실행 경계입니다. 애플리케이션과
호스트의 신뢰를 분류한 뒤 캐시된 Schannel 서버 ID 또는 ECH 프런트엔드가 만든
소유 평문 채널을 반환합니다. 오리진 mTLS 선택도 크기가 제한된 감사 이벤트로
감쌉니다. 이 API는 ECH 확장만 존재하거나 후보 키가 있거나 클라이언트가 인증서를
고정했다는 사실을 가로채기 성공으로 취급하지 않습니다.

## 의미 변환

`<ntl/net/http/transform>`은 완전한 HTTP/1.1, HTTP/2 및 HTTP/3 메시지가 함께
사용합니다. `<ntl/net/http/async_transform>`은 크기가 고정된
작업자 풀과 제한된 큐, 협력적 취소, 기한, 과부하 정책, 통계를 추가합니다. 전체
본문을 할당하기 어려운 경우에는 `<ntl/net/http/stream_transform>`이 크기가 제한된
평문 본문 청크를 처리합니다. 메시지별 `content_encoding_stream`은 H1 청크, H2
DATA, H3 DATA가 임의로 나뉜 경우에도 등록된 `Content-Encoding` 체인을 점진적으로
디코딩하고 다시 인코딩합니다. 이에 대응하는 실제 wire/event 어댑터는 다음과
같습니다.
`<ntl/net/http/http1_stream_transform>`,
`<ntl/net/http2/stream_transform>` 및
`<ntl/net/http3/stream_transform>`입니다. 상태를 갖는 콜백은 메시지마다 한 번
생성되므로 다중화된 스트림이 부분 애플리케이션 레코드를 공유하지 않습니다.
별도의 의미 어댑터는 다음 프로토콜을 지원합니다.
웹소켓(`<ntl/net/websocket/transform>`), gRPC
(`<ntl/net/grpc/transform>`) 및 WebTransport
(`<ntl/net/http3/webtransport_transform>`).

소유 HTTP 메시지는 정책 콜백 형태를 바꾸지 않고도 할당자를 선택할 수 있습니다.
사용자 코드는 보통 기본 리소스를 사용합니다. 크기 제한 어댑터나 커널 어댑터는
`http::message_memory_ref`를 H1/H2/H3 파서 또는 연결에 전달하며, 메서드, 대상,
헤더, 본문, 트레일러, 다시 작성한 의미 응답이 모두 그 리소스를 사용합니다.
`borrowed_bounded_memory_resource`는 호출자가 선택한 PMR 백엔드의 전체 및 단일
할당 한도를 설정하고, `borrowed_fixed_workspace_resource`는 호출자 소유 범위를
넘어서는 대체 할당을 제공하지 않습니다.

```cpp
std::array<std::byte, 64 * 1024> storage{};
ntl::net::borrowed_fixed_workspace_resource workspace(storage);

auto request = ntl::net::http::parse_http1_request(
    wire, decoders, {.origin_scheme = "https"}, limits,
    ntl::net::http::message_memory_ref{workspace.resource()});
if (!request && request.status() == STATUS_INSUFFICIENT_RESOURCES)
  drop_flow(); // never forward a partially parsed or rewritten message
```

위에서 명시적으로 빌리는 PMR 형식은 workspace가 여기서 파생된 모든 메시지와
결과보다 오래 살아 있어야 합니다. `_ref`와 `borrowed_`라는 이름이 이 경계를
표시합니다. 일반 커널 프록시 세션은 대신 workspace lease를 소유하고 코루틴이
완료될 때까지 유지합니다. 정책 코드가 allocator나 해제 순서를 관리하지 않아도
peak allocation counter를 확인할 수 있습니다.

## 상황 인식 결정

`<ntl/net/http/inspection_context_view>`는 형식 없는 본문 버퍼에서 정책이 의미를
추측하지 않게 합니다. `inspection_policy` 하나가 의미 변환과 헤더, 본문,
메시지 완료 단계의 순차적 판정을 결합합니다. 규칙은 요청 대상 및 헤더를 응답,
연결, 애플리케이션, WFP 흐름 및 TLS 메타데이터와 함께 사용할 수 있습니다.

```cpp
ntl::net::http::inspection_policy policy;

policy.requests()
    .at_headers()
    .when([](const ntl::net::http::inspection_context_view &context) {
      return context.method() == "POST" &&
             context.path() == "/admin/import" &&
             context.query() == "mode=replace" &&
             context.headers().first("content-type") ==
                 "application/json" &&
             context.connection().application_id &&
             *context.connection().application_id == trusted_browser_app_id &&
             context.tls().server_name &&
             *context.tls().server_name == "policy.example";
    })
    .decide([](const ntl::net::http::inspection_context_view &) {
      return ntl::net::inspection::verdict::block;
    });

policy.responses()
    .at_message_complete()
    .when([](const ntl::net::http::inspection_context_view &context) {
      return context.request().headers.first("x-inspect-response") == "1" &&
             context.response() && context.response()->status >= 400;
    })
    .decide([](const ntl::net::http::inspection_context_view &) {
      return ntl::net::inspection::verdict::drop_flow;
    });
```

`context.headers()`는 현재 메시지 방향의 헤더를 선택합니다. 응답을 검사하는 동안에도
`context.request()`를 사용할 수 있습니다. 따라서 메서드, 스킴, authority, 경로,
쿼리, 헤더, 트레일러, 현재 본문, 엔드포인트, 프로세스/애플리케이션 ID, 흐름
방향/ID, SNI, ALPN을 서로 독립적인 정책 입력으로 사용할 수 있습니다.
`body_chunk()`는 콜백 수명 뷰이므로 보관하면 안 됩니다.

사용자 정의 어댑터는 `inspection_context_view::for_request(...)` 또는
`inspection_context_view::for_response(...)`를 사용합니다. 메시지 방향은 선택한
팩터리에서 결정되며, 응답 객체 없이 응답 컨텍스트를 만들 수 없습니다.

## HTTP/2 연결 및 세션 어댑터

`<ntl/net/http2/proxy_connection>`은 연결 하나의 프로토콜 상태를 소유합니다.
양방향 HPACK 컨텍스트, 요청/응답 교환, CONTINUATION 조립, 단계별 정책, 송신 창,
SETTINGS, RST_STREAM, GOAWAY가 포함됩니다. 전송 독립 상태는 프레임 단위로 구동할
수 있으며, `<ntl/net/http2/proxy_session>`은 일반적인 비동기 바이트 스트림 루프를
제공합니다.

```cpp
auto connection = std::make_shared<ntl::net::http2::proxy_connection>(
    policy, decoders, encoders, metadata,
    ntl::net::http2::inspection_observer(observer));

co_await ntl::net::http2::run_proxy_session(
    downstream_tls, upstream_tls, connection);
```

두 방향은 함께 시작합니다. 어느 한쪽에서 먼저 EOF, 실패 또는 `drop_flow`가
발생하면 반대 방향을 취소하고, 작업을 반환하기 전에 두 자식 작업의 종료를 모두
기다립니다. 분리된 릴레이 스레드나 블로킹 `task.get()`은 필요하지 않습니다.
위 오버로드는 일반 authority-form CONNECT와 확장 CONNECT를 포함한 모든 CONNECT
요청을 오리진에 HEADERS로 쓰기 전에 의도적으로 거부합니다. 제품은
`admit(stream_id, request)`가 `inspect` 또는 `passthrough`를 반환하는 터널
핸들러를 전달해야 합니다. 따라서 변경 없는 전달은 어댑터 부재가 아니라 명시적인
정책 결정입니다. 승인이 거부되면 크기가 제한된 스트림 로컬 403 응답을 만듭니다.

HTTP/1 프록시는 CONNECT와 Upgrade에 같은 오리진 전달 전 계약을 적용합니다.
핸들러는 먼저 `http1_tunnel_offer_view`를 받아
`http1_tunnel_disposition::inspect` 또는 `passthrough`를 반환해야 합니다. 승인
메서드가 없으면 거부를 뜻합니다. 이후의 101 또는 성공한 CONNECT 응답은 같은
제안에 대해 승인된 릴레이에만 진입할 수 있습니다.

완전한 1xx 응답도 최종 응답과 같은 변환, 단계적 판정 및 관찰자 경로를 거치지만,
최종 응답이 올 때까지 교환 상태를 유지합니다. 연결된 요청은 스트림 수와 총
바이트 수 제한이 있는 공유 연결 로컬 저장소에 한 번만 보관하므로, 응답 프레임을
처리할 때마다 요청 본문을 복사하지 않습니다. 디코딩된 HTTP/2 및 HTTP/3 필드
이름은 의미 헤더 컬렉션에 들어가기 전에 와이어 규격이 요구하는 소문자 형식인지
검사합니다. 잘못된 대문자 이름은 정규화하지 않고 거부합니다. 첫 값이 비어 있어도
중복 의사 필드는 중복으로 처리하며, `Host`가 `:authority`와 함께 두 번째 권한을
도입할 수 없습니다. 프로토콜상 허용되지 않는 상태 101도 HTTP/2와 HTTP/3에서
모두 거부합니다.

리디렉션된 TLS 검사에서는 전달 전 단일 오리진 연결을 선택할 때 사용한 SNI와
변환된 `:authority`도 서로 묶습니다. ASCII DNS의 대소문자 차이, 마지막의 root dot
하나, 암시적 또는 명시적 443 포트는 일치로 인정합니다. SNI가 없거나 호스트나
포트가 다르거나, IPv6 리터럴에 대괄호가 없거나, `:authority`와 일반 `Host`가 함께
있으면 fail-close합니다. 별도의 명시적 origin coalescing 허용 정책이 허용된
authority 집합을 강제할 때만 `require_http2_server_name_authority_binding`을
`false`로 설정하십시오. HTTP/1도 같은 공통 authority parser와 이에 대응하는
`require_http1_server_name_authority_binding` opt-out 계약을 사용합니다.

`<ntl/net/http2/websocket_tunnel>`은 선택적 RFC 8441 처리기입니다. Extended CONNECT
협상을 검증하고, 임의의 DATA 분할에 걸친 WebSocket 메시지를 재구성하며, 제한형
정책과 `permessage-deflate`를 적용하고 클라이언트 마스킹을 복원합니다. 알 수 없는
Extended CONNECT 프로토콜은 우연히 불투명 bypass가 되지 않도록 명시적으로
차단하거나 passthrough를 선택해야 합니다. WebSocket 처리기는 해당 프로토콜의 원시
바이트 터널 계약을 제공하지 않으므로 일반 CONNECT를 거부합니다.

## HTTP/3 연결 어댑터

`<ntl/net/http3/proxy_connection>`은 HTTP/3 연결 하나를 소유하는 서버 측
어댑터입니다. SETTINGS/control stream, 차단된 스트림 재개와 decoder acknowledgement를
포함한 제한형 static/dynamic QPACK, 요청/trailer 조립, 콘텐츠 디코드와 재인코드,
의미 정책, 응답 framing, GOAWAY 및 WebTransport 라우팅을 처리합니다. 제품 코드는
HTTP/1.1과 HTTP/2에 쓰는 것과 같은 `inspection_policy`를 제공합니다. 변환을 먼저
수행한 뒤 정책이 `headers`, `body_chunk`, `message_complete` 순으로 관찰하며,
여기에는 변환된 메서드, 경로, 쿼리, 헤더 및 본문도 포함됩니다.

핵심 오리진 경계는 완료 콜백 기반입니다.

```cpp
class product_origin final
    : public ntl::net::http3::async_origin_transport {
public:
  ntl::status submit(
      std::uint64_t exchange_id,
      ntl::net::http3::origin_request request,
      ntl::net::http3::origin_completion completion) noexcept override {
    return queue_.submit(exchange_id, std::move(request),
                         std::move(completion));
  }

  void cancel(std::uint64_t exchange_id) noexcept override {
    queue_.cancel(exchange_id);
  }

private:
  bounded_origin_queue queue_;
};

ntl::net::http3::proxy_connection connection(
    quic_backend, origin, policy, decoders, encoders, metadata, observer);
```

각 요청 스트림은 독립적으로 제출합니다. 따라서 느린 오리진이 다른 스트림을
막지 않으며 완료는 스트림 순서와 관계없이 도착할 수 있습니다. 백엔드 콜백과
완료는 연결 내부에서 직렬화합니다. reset, drain, stop, close는 보류 중인 교환을
취소하고, 이와 경쟁해 늦게 도착하거나 중복된 완료는 무시합니다. 연결은 오리진과
정책 종속성을 소유하고 작업 중에는 QUIC 백엔드를 유지합니다. 원래 래퍼를
소멸시키거나 콜백에서 `close()`를 호출해도 진행 중인 상태는 무효화되지 않습니다.

`immediate_origin_transport_adapter`는 이전의 blocking `origin_transport`를
명시적으로 감쌉니다. 마이그레이션과 결정론적 픽스처에는 유용하지만, 제출 콜백은
origin 호출이 반환될 때까지 차단되므로 여러 stream 사이의 HTTP/3 동시성을
보존하지 않습니다.

어댑터는 각 프로토콜의 authority 경계를 보존합니다. HTTP/2와 HTTP/3 flow-control
credit은 연결 백엔드가 관리하고, WebSocket 클라이언트 출력은 다시 마스킹하며,
gRPC 압축 메시지는 협상된 코딩으로 디코드하고 재인코드합니다. WebTransport
flow-control capsule은 변경 가능한 콘텐츠가 아닙니다.

`<ntl/net/http3/webtransport_session>`은 WebTransport 의미 계층을 실제 QUIC
백엔드에 연결합니다. Draft-16 SETTINGS를 교환하고, static QPACK Extended CONNECT
HEADERS를 보내거나 받으며, 세션 스트림 접두사를 쓰고 HTTP Datagram을 전송합니다.
이동 전용 스트림 권한은 여러 번의 쓰기와 FIN 또는 32비트 애플리케이션 값으로
매핑한 reset을 지원합니다. MsQuic preview reliable-offset API를 사용해 reset할
때도 세션 접두사의 신뢰성을 유지합니다. 원시 MsQuic 백엔드는
reliable-reset-at과 QUIC Datagram을 사용할 수 있고 협상한 경우에만 WebTransport를
광고합니다.

## 관리형 HTTP/3 클라이언트

`<ntl/net/http3/msh3_client>`는 선택형 msh3/MsQuic 백엔드를 위한 제한형 동기
사용자 모드 클라이언트입니다. `system_trust_client`는 일반 Windows 신뢰를
사용합니다. `private_ca_client`는 대신 전용 인메모리 chain engine에서
애플리케이션 소유 CA, 요청한 DNS 이름 및 서버 인증 EKU를 기준으로 피어를
검증합니다. 이 CA를 Windows 또는 브라우저 신뢰 저장소에 기록하지 않으며 인증서
오류 우회 옵션도 없습니다.

요청은 논리적 오리진 ID를 선택적 물리 `peer_endpoint`와 별도로 유지합니다.
따라서 관리형 검사 클라이언트는 원래 SNI와 HTTP/3 `:authority`를 유지하면서
명시적인 루프백 프록시에 연결할 수 있습니다. 이는 관련 없는 브라우저를 투명하게
가로채는 것이 아니라 애플리케이션 통합입니다.

## 브라우저 예시 경계

[`browser-https-inspection`](../../examples/wfp/user/browser-https-inspection)은
IPv4/IPv6 TCP, Schannel TLS, HTTP/1.1 HTML, `permessage-deflate`를 사용하는
WebSocket, 다중화된 HTTP/2 HTML을 아우르는 완전한 종단 간 예제입니다. 공통 크기
제한 디코더 레지스트리가 gzip, deflate, Brotli를 처리합니다. HTTP/2에서는 먼저
오리진과 프로토콜을 협상하고 선택된 ALPN을 브라우저 쪽에 반영한 뒤, 전체 연결
루프를 재사용 가능한 어댑터에 위임합니다. 어댑터는 방향별 독립 HPACK 상태를
유지하고, 동시 스트림 수와 본문 상태를 제한하며, 제어 프레임을 검증하고 연결 및
스트림 창을 계산합니다. 원본 데이터의 크레딧은 변환된 데이터를 보관하거나 쓴
뒤에만 반환합니다.

[`http3-inspection`](../../examples/wfp/user/http3-inspection)은 QUIC 공급자 경계,
임의 스트림 분할, static QPACK 및 Brotli HTML을 보여 줍니다. 브라우저 예제의
애플리케이션 범위 WFP 정책은 IPv4/IPv6 UDP 443을 차단하므로 수정하지 않은 일반
브라우저가 검사 가능한 TCP 경로를 사용합니다. 별도의 관리형 클라이언트는 명시적
루프백 엔드포인트와 애플리케이션 소유 신뢰를 사용하여, WFP나 브라우저 설정 변경
없이 실제 다운스트림 HTTP/3을 실행합니다. 지원하지 않는 콘텐츠 코딩이나 구성된
프레임, 헤더, 스트림 또는 본문 제한 위반은 fail-close합니다.
