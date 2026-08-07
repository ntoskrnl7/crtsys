# 제한된 콘텐츠 검사 및 프레이밍

[NTL 문서로 돌아가기](./README.ko-KR.md)

검사 인터페이스는 네이티브 패킷 API에서 흔히 뒤섞이는 세 가지 질문을
분리합니다.

1. **무엇이 완전한 단위인가?** UDP는 데이터그램 하나를 제공하지만 TCP에는
   애플리케이션이 선택한 프레이머가 필요합니다.
2. **어떤 바이트가 검사됩니까?** `content_view`는 불변 연속 또는
   제한된 바이너리 접두사 및 포함 작업이 포함된 조각화된 뷰입니다.
3. **정책은 무엇을 결정할 수 있는가?** `inspection::verdict`는 형식화되어
   있습니다. 네이티브 차단, 흡수, 지연 및 주입 동작은 애플리케이션 정책이
   아니라 WFP 어댑터가 담당합니다.

## UDP와 TCP는 동일한 경계를 갖지 않습니다.

`udp_datagram_view`는 하나의 검증된 UDP 페이로드를 나타냅니다. UDP 전송은 다음에 매핑됩니다.
저장소가 여러 MDL에 걸쳐 있는 경우에도 하나의 데이터그램입니다. IP 단편화 및
네트워크 전달은 전송 계층 WFP 아래에서 처리되는 별도의 문제입니다.
샘플에서 사용되는 뷰입니다.

TCP는 순서가 지정된 바이트 스트림입니다. 하나의 수신에는 메시지의 절반이 포함될 수 있습니다.
메시지 또는 여러 메시지. `async_framed_stream<Framer>`는 부분적으로 유지됩니다.
바이트를 과도하게 읽고 완전한 소유 `framed_message`만 반환합니다.

```cpp
ntl::net::async_framed_stream messages(
    socket,
    ntl::net::framing::u32_be_length_prefix{64 * 1024},
    ntl::net::framing::frame_limits{64 * 1024 + 4});

auto message = co_await messages.read_frame();
inspect(ntl::net::inspection::content_view(message.content()));
```

위의 4바이트 접두사는 TCP 헤더나 TCP 표준이 아니며,
고정된 메시지 길이. 이는 접두사가 붙는 하나의 예시적인 애플리케이션 프로토콜입니다.
빅엔디안, 메시지별 페이로드 길이를 포함합니다. 프레이머가 먼저 기다립니다.
접두사를 읽고 해당 메시지의 길이를 읽고 구성된 최대값을 적용합니다.
그런 다음 정확히 완전한 프레임을 기다립니다. 다음에 속하는 바이트
메시지는 버퍼링된 상태로 유지됩니다.

내장된 프레이밍 계약은 고정 크기, 빅엔디안 16/32비트 길이를 포괄합니다.
접두사 및 구분 기호. 플래그, 선택적 필드, varint 또는
콘텐츠 종속 길이는 상태 저장 `noexcept` 프레이머를 제공합니다.

```cpp
struct my_protocol_framer {
  ntl::net::framing::frame_probe
  probe(ntl::net::scatter_view available) noexcept {
    // Validate the currently available header.
    // Return need_more(required_total), complete(...), or malformed(status).
  }
};
```

`frame_limits`는 사용자 정의 파서와 독립적으로 적용되므로 버그가 있거나
적대적인 길이는 경계 없이 수신 버퍼를 늘릴 수 없습니다. 하나
`async_framed_stream`는 하나의 활성 판독기를 허용하고 읽기 전에 깨끗한 EOF를 보고합니다.
프레임을 오류로 완료하고 소유 소켓의 취소 경로를 사용합니다.

## 콘텐츠 정책

`content_view`는 바이트가 텍스트나 프로토콜 구조라고 가정하지 않습니다.
바이너리에 안전한 접근 기능만 제공하며 파서는 애플리케이션이 선택합니다.

```cpp
ntl::net::inspection::verdict policy(
    const ntl::net::inspection::udp_datagram_view &datagram) {
  const auto forbidden = datagram.payload().contains("BLOCKME");
  if (!forbidden)
    return ntl::net::inspection::verdict::block;
  return *forbidden ? ntl::net::inspection::verdict::block
                    : ntl::net::inspection::verdict::permit;
}
```

구조화된 프로토콜의 경우 고정 필드를 커서로 구문 분석하고 유효성을 검사합니다.
할당하기 전에 콘텐츠가 선택한 길이, 프레이머가 완료될 때까지 기다립니다.
전체 메시지를 확인한 다음에만 정책을 호출합니다. 응용 프로그램의 `auto data`
코드는 일반적으로 소유 `framed_message`, `udp_datagram_view` 또는
호출자 정의 구문 분석 값은 지정되지 않은 기본 패킷이 아닙니다.

강제 적용 예제는 전송마다 다릅니다.
[`udp-content-filter` 샘플](../../examples/wfp/user/udp-content-filter)은 완전한
데이터그램 하나를 흡수한 뒤 형식화된 `permit` 판정이 내려진 경우에만 보관한
복제본을 재주입합니다.
[`tcp-content-filter` 샘플](../../examples/wfp/user/tcp-content-filter)은 u32
빅엔디언 길이 접두사를 사용하는 애플리케이션 프로토콜을 선택합니다. WFP에 한
프레임을 완성할 만큼의 스트림 바이트를 요청하고 해당 스트림을 지연한 뒤, 사용자
코루틴이 허용한 정확히 그 프레임만 재개합니다. TCP 차단은 임의의 바이트를
삭제하는 대신 전체 흐름을 종료합니다. 응답이 없거나 늦거나 형식이 잘못되었거나
할당량을 초과하면 실패 시 차단합니다.

## 압축은 제한적이며 확장 가능합니다.

`<ntl/net/inspection/content_decoder>`는 크기가 제한된 출력 sink, runtime decoder
계약 및 정규화된 content-coding registry를 제공합니다. 선택적
`NtlContentCodecs.cmake` backend는 zlib과 Brotli 버전을 고정하며,
`<ntl/net/inspection/standard_content_decoders>`는 HTTP `gzip`, 엄격한 RFC 1950
`deflate` 및 `br`를 등록합니다. 애플리케이션은 같은 fresh-decoder factory를 통해
Zstandard, 사전 기반 형식 또는 독점 형식을 추가할 수 있습니다. 완전한
`Content-Encoding` chain은 적용 순서의 역순으로 decode됩니다.

```cpp
ntl::net::inspection::content_decoder_registry decoders;
ntl::net::inspection::register_standard_content_decoders(decoders);

auto decoded = ntl::net::inspection::decode_content_encoding(
    decoders, encoded_message, "gzip, br",
    {.maximum_encoded_size = 1024 * 1024,
     .maximum_decoded_size = 4 * 1024 * 1024,
     .maximum_expansion_ratio = 32});
if (!decoded)
  return ntl::net::inspection::verdict::block;
```

프레이밍은 해당 형식에 필요한 압축 단위를 설정해야 합니다.
연결 전체 압축은 연결당 하나의 디코더 상태를 유지합니다.
메시지별 편의 기능을 사용하는 것보다 인코딩된 입력, 디코딩됨
출력, 확장 비율, CPU 시간, 중첩 및 사전 선택이 모두 필요합니다.
명시적인 한계. 누락된 코딩 어댑터는 `STATUS_NOT_SUPPORTED`를 반환합니다. 그것
인코딩된 바이트를 평문으로 재해석하지 않습니다.

## HTTP/1.1, HTTP/2, HTTP/3에 대한 하나의 변환 정책

`<ntl/net/http/transform>`는 다음에 대한 완전한 디코딩된 요청 또는 응답을 제공합니다.
신청 정책. TLS 레코드, HTTP/1 청크, HTTP/2 프레임 경계,
HPACK/QPACK 상태 및 전송 흐름 제어 권한이 해당 상태로 누출되지 않습니다.
정책:

```cpp
ntl::net::http::transform_pipeline pipeline;

pipeline.requests().transform(
    [](ntl::net::http::request_message &request) {
      request.headers.set("x-inspected-by", "ntl");
      return ntl::net::http::rewrite_result::headers_changed();
    });

pipeline.responses()
    .html()
    .transform(
        [](const ntl::net::http::request_message &,
           ntl::net::http::response_message &response) {
          std::string html(
              reinterpret_cast<const char *>(response.body.data()),
              response.body.size());
          html.append("<!-- inspected -->");
          return ntl::net::http::rewrite_result::replace_text(
              std::move(html));
        });
```

콜백 본문은 크기가 제한되고 콘텐츠 디코딩까지 끝난 완전한 HTTP 메시지 본문이며,
TCP 패킷 하나나 HTTP/2 DATA 프레임 하나가 아닙니다. 규칙은 `unchanged`,
`headers_changed`, 교체 본문, `block`, `drop`, 즉시 의미 응답 중 하나를 반환합니다.
기본 실패 정책은 fail-closed입니다. `forward_original` 정책을 명시적으로 선택하면
콜백 호출 전의 전체 메시지를 복원하며, 일부만 변경된 객체를 전달하지 않습니다.

`request_message` 및 `response_message`는 이 편리한 의미 체계 API를 유지합니다.
문자열, 헤더 목록, 본문 및 트레일러를 PMR에 저장하는 동안
`message_memory_ref`가 선택한 리소스입니다. 사용자 모드는 기본적으로 표준 PMR로 설정됩니다.
자원. 커널 H1/H2/H3 어댑터는 제한된 비페이징 리소스를 제공하므로
정책 코드는 `response.body`, `response.headers`를 계속 사용하며
`rewrite_result::body_changed()`를 직접 사용합니다. 자원 고갈은 다음과 같이 보고됩니다.
`STATUS_INSUFFICIENT_RESOURCES` 및 구성된 장애 폐쇄 정책을 따릅니다.
해당 경로에는 무제한 대체 할당자가 없습니다.

### 결정은 전체 요청 및 연결 컨텍스트를 사용합니다.

콘텐츠 평결은 본문에만 국한되지 않습니다. `<ntl/net/http/inspection_context_view>`
및 `<ntl/net/http/inspection_conditions>`는 의미론적 HTTP 필드를 다음과 결합합니다.
그들을 이어준 연결. 동일한 규칙에서 방법, 체계,
권한, 경로, 쿼리, 헤더, 트레일러, 디코딩된 본문, 관련 응답,
HTTP 버전 및 스트림 ID, 소스 및 원래 대상 엔드포인트, 흐름
프로세스 ID, 애플리케이션 ID, TLS SNI 및 협상된 ALPN입니다.

`<ntl/net/http/inspection_policy>`는 두 가지 모두를 소유하는 `inspection_policy`를 정의합니다.
메시지 변환 및 단계적 결정. Staged만 필요한 어댑터
판정 규칙은 `<ntl/net/http/decision_policy>`의 `decision_policy`를 사용합니다.
`inspection_policy::decisions()`는 해당 구성 요소를 명시적으로 노출합니다.

```cpp
ntl::net::http::inspection_policy policy(limits);
using namespace ntl::net::http::condition;

policy.transforms_ref().responses().html().transform(rewrite_html);

policy.requests()
    .at_headers()
    .when(method_is("POST"))
    .when(path_starts_with("/admin/"))
    .when(header_is("x-policy", "inspect"))
    .when(tls_server_name_is("example.test"))
    .when(alpn_is("h2"))
    .when(application_label_is("browser.exe"))
    .decide(check_request_headers);

policy.requests()
    .at_message_complete()
    .when(complete_body_contains("BLOCKME"))
    .decide([](const auto &) {
      return ntl::net::inspection::verdict::block;
    });

policy.responses()
    .at_message_complete()
    .when(response_status_is(200))
    .when(complete_body_contains("restricted"))
    .decide(check_complete_response);
```

반복되는 `when` 호출은 논리 AND입니다. 콜백은 원시를 검사할 수도 있습니다.
제품별 조합을 위한 `inspection_context_view`. `headers()` 및
`body()`는 현재 방향의 메시지를 참조합니다. `request()` 항상
연관된 요청을 유지하며 `response()`는 응답에 대해 null이 아닙니다.
같은 방향/단계의 규칙은 등록순, 선착순을 사용합니다.
의미론; `permit`, `block` 및 `drop_flow`는 모두 해당 단계의 터미널입니다.
이렇게 하면 구성에서 기본값을 숨기지 않고 허용 목록을 명시적으로 만들 수 있습니다.

```cpp
policy.requests().at_headers()
    .when(application_id_is(trusted_app_id))
    .when(path_starts_with("/allowed/"))
    .decide([](const auto &) { return ntl::net::inspection::verdict::permit; });
policy.requests().at_headers()
    .decide([](const auto &) { return ntl::net::inspection::verdict::block; });
```

일치 규칙이 없으면 단계가 허용됩니다. 이전에 좁은 예외를 등록하세요.
광범위한 규칙을 적용하고 원격 측정에 허용 규칙 대신 관찰자를 사용합니다.
규칙 순서에 영향을 주어서는 안 됩니다.
형식화된 도우미는 닫힌 규칙 언어가 아니라 편리합니다. 을 위한예를 들어 제품 정의 헤더 네임스페이스는 추가하지 않고도 선택할 수 있습니다.
모든 명명 규칙에 대한 새로운 NTL API:

```cpp
policy.requests()
    .at_headers()
    .when(header_name_starts_with("custom-"))
    .when(any_header([](const ntl::net::http::header_field &header) {
      return header.name.starts_with("custom-") &&
             header.value == "enabled";
    }))
    .decide([](const ntl::net::http::inspection_context_view &context) {
      return evaluate_product_rule(
          context.method(), context.path(), context.query(),
          context.headers(), context.connection(), context.tls());
    });
```

헤더 이름 접두사만 조건이라면 `header_name_starts_with`를 사용합니다. 요청·응답별
형식은 `request_header_name_starts_with`와 `response_header_name_starts_with`입니다.
필드 이름과 값 사이에 애플리케이션 정의 관계가 필요할 때만 `any_header`를
추가합니다. 마지막의 원시 `when` 콜백은 제한 없는 확장 지점으로, 하나의 조건자에서
메시지, 전송, TLS, WFP ID, 엔드포인트 속성을 조합할 수 있습니다.

`any_header`, `any_request_header`, `any_response_header`는 임의의 필드 조건자를
받습니다. 원시 `when` 조건자는 어떤 컨텍스트 멤버든 조합할 수 있으며, `all_of`,
`any_of`, `none_of`는 재사용 가능한 조건을 구성합니다. 필드 조건자에 전달하는 일반
헤더 이름은 소문자로 정규화하지만 값은 원래 대소문자와 바이트를 보존합니다.
`header_is`와 요청/응답별 변형은 같은 이름으로 반복된 모든 필드를 검사하므로,
문제가 없는 첫 필드가 뒤의 일치 값을 숨길 수 없습니다. 이 존재 검사는 거부 규칙에
적합합니다. 단일 정규 값이 필요한 허용 규칙에는 `unique_header_is` 또는 요청/응답별
변형을 사용합니다. `header_count_is`와 `all_header_values`를 사용하면 반복 필드의
다른 요구 사항도 명시할 수 있습니다. 연관 메시지용 형식은 다음과 같습니다.
`request_header_count_is`, `response_header_count_is`,
`all_request_header_values` 및 `all_response_header_values`. 트레일러 정책
일치하는 `any_trailer`가 있습니다.
`request_trailer_*` 및 `response_trailer_*` 도우미.
응답을 처리할 때 요청 측 조건에는 `request_header_is`, 응답 측 조건에는
`response_header_is`를 사용합니다. 두 메시지를 모두 볼 수 있을 때 모호한
"헤더" 검사가 되는 것을 막습니다.
`query_is("")`는 요청 대상에 명시적인 후행이 포함되어 있음을 의미합니다.
빈 쿼리가 있는 `?`입니다. 다음과 같은 경우에만 `query_present()` 및 `query_absent()`를 사용하십시오.
존재가 중요합니다. 부재 쿼리는 더 이상 명시적으로 비어 있는 쿼리의 별칭을 지정하지 않습니다.
어댑터는 `headers`, 0개 이상의 `body_chunk`, `message_complete`를 차례로 호출합니다.
완전한 메시지 HTTP/1, HTTP/2 및 HTTP/3 어댑터는 제한된 메시지를 제공합니다.
비어 있지 않은 몸체를 하나의 덩어리로 디코딩합니다. `stream_transform`를 기반으로 구축된 어댑터는 다음과 같습니다.
정책 컨텍스트를 변경하지 않고 많은 청크를 제공합니다. 콘텐츠 결정
임의의 청크 경계를 확장해야 하는 이는 `message_complete`에 속합니다.
`complete_body_contains`. `current_body_chunk_contains` 고의로 검색
콜백 청크는 하나만 있으며 전체 메시지 보안 규칙으로 사용되어서는 안 됩니다.

리디렉션 또는 프록시 소유자가 연결 메타데이터를 제공합니다. NTL은 발명하지 않습니다
PID, 애플리케이션 ID, 원래 대상, SNI 또는 ALPN
기본 WFP/TLS 레이어는 이를 제공하지 않았습니다. 다음을 요구하는 구성된 규칙
따라서 신원이 누락되어 일치하지 않습니다. 잘못된 입력, 콜백 실패,
명시적인 정책 실패는 기본적으로 페일클로즈 상태로 유지됩니다.
`inspection_session_metadata`는 엔드포인트 텍스트, TLS 텍스트, 애플리케이션을 소유합니다.
레이블 및 불투명한 애플리케이션 ID 바이트이므로 프록시가 안전하게 구성할 수 있습니다.
임시 버퍼에서. `inspection_context_view` 자체는 콜백 수명입니다.
보기를 유지하면 안 됩니다. `application_label_is`는 디스플레이 라벨일 뿐입니다.
상태; 보안 정책은 다음을 통해 정확한 WFP ID를 사용해야 합니다.
`application_id_is`(추가로 프로세스 ID를 제한할 수 있음)

사용자 정의 프로토콜 어댑터는 다음을 통해 컨텍스트를 구성합니다.
`inspection_context_view::for_request(...)` 또는
`inspection_context_view::for_response(...)`를 통해 컨텍스트를 만듭니다. 응답
팩터리는 응답 참조를 요구하지만 요청 팩터리에는 응답 매개변수가 없습니다. 따라서
메시지 방향과 현재 메시지의 헤더·본문·트레일러 뷰가 서로 불일치할 수 없습니다.
원시 방향/응답 포인터 생성자는 공개하지 않습니다.

동시에 사용하기 전에 파이프라인 구성을 마쳐야 합니다. 정책 조건자와 결정 함수는
const 호출이 가능해야 하지만, 하나의 구성된 정책을 서로 다른 연결에서 동시에
평가할 수 있습니다. 따라서 캡처한 참조, 포인터가 가리키는 상태, 형식 소거 콜백
뒤에 숨은 상태에는 자체 동기화가 필요합니다. 일반 필드와 트레일러는 따로
검증합니다. 제어 문자, 트레일러의 의사 필드, hop-by-hop 필드, 트레일러의
`Content-Length` 같은 프레이밍 필드는 출력 전에 거부합니다.

`<ntl/net/inspection/standard_content_encoders>`는 디코더 레지스트리에 크기가 제한된
gzip, zlib `deflate`, Brotli 출력을 추가합니다. 본문을
`transformed_body_coding::preserve`로 변경하면 어댑터는 발신자 순서대로 원래
`Content-Encoding` 체인을 다시 적용하고 `Content-Length`를 갱신하며, `ETag`,
`Digest`, `Content-MD5`처럼 무효가 된 응답 검증자를 제거합니다. `identity`는
콘텐츠 코딩을 명시적으로 제거합니다.

와이어 어댑터마다 다음과 같이 서로 다른 전송 책임이 있습니다.

- `<ntl/net/http/http1_transform>`는 프레이밍을 검증하며 정확히 하나가 필요합니다.
  `Host`는 원본/절대/권한 요청 대상 형식을 구문 분석하고
  독립형 `chunked` 전송 코딩을 지원하고 정책을 적용하며
  새로운 HTTP/1.1 메시지를 직렬화합니다. 전송은 명시적으로 제공합니다.
  `http1_request_target_context::origin_scheme`; 절대 목표가 일치해야 함
  해당 계획과 `Host` 모두. 지원되지 않는 전송 코딩 체인은 이전에 실패합니다.
  정책을 평문으로 재해석하는 대신
- `<ntl/net/http2/transform>`는 독립적인 상태 저장 HPACK 디코더를 유지합니다.
  연결 방향별로 상태 비저장 HPACK을 방출하고 재구성합니다.
  HEADERS/CONTINUATION/DATA는 다중화된 요청과 응답을 연관시킵니다.
  수신된 흐름 제어 바이트를 전송 어댑터에 보고합니다.
  요청 연결은 라이브 스트림당 하나의 공유 의미론적 요청을 유지합니다.
  스트림 수와 집계 바이트로 제한됩니다. 정보용 1xx 메시지
  해당 연관을 완료하지 않은 채 변환 및 검사됩니다.
- `<ntl/net/http3/standard_inspection_proxy>`는 QUIC 스트림, QPACK,
  동일한 변환을 적용하면서 HTTP/3 백엔드에 대한 흐름 제어
  오리진 전달 전과 다운스트림 응답 전의 파이프라인입니다.

모든 어댑터는 헤더 이름, 금지된 홉별 필드, 의사 필드의 유효성을 검사합니다.
모양, 콘텐츠 길이, 헤더/본문 할당량, 코딩 깊이 및 다시 인코딩된 크기.
`<ntl/net/http/authority>`는 할당이 필요 없는 공유 HTTPS 권한을 제공합니다.
HTTP/1.1, HTTP/2 및 HTTP/3에서 사용되는 파서 및 SNI 바인딩. 엄격한
리디렉션된 TLS
경로는 원본 전달 직전에 변환된 권한을 확인합니다.
SNI 누락 또는 호스트/포트 불일치는 원본 요청이 될 수 없습니다.
HEAD, 1xx, 204 및 304 응답은 다음과 같은 법적 표현 메타데이터를 유지합니다.
디코딩이나 방출을 시도하지 않고 `Content-Length` 및 `Content-Encoding`로
메시지 본문. 정책은 본문 바이트나 예고편을 첨부할 수 없습니다.
응답.
HTTP/2 연결 어댑터는 동시 쓰기를 직렬화하고, 자신이 보관한 바이트만큼만 흐름 제어
창을 보충하며, 피어의 연결 및 스트림 송신 창을 따라야 합니다. 고전적인
authority-form CONNECT와 Extended CONNECT를 포함한 모든 CONNECT 요청은 HEADERS를
전달하기 전에 핸들러의 결정이 필요합니다. 세션 기본값은 거부이며, 실제 바이트
터널 핸들러가 passthrough를 명시적으로 선택해야 합니다. 브라우저 HTTPS 예제에
완전한 어댑터가 구현되어 있습니다.
HTTP/2와 HTTP/3 어댑터는 의미 헤더 정규화 전에 wire의 대문자 헤더 및 트레일러
이름을 거부합니다. HTTP/1에서는 대소문자가 계속 유효하며 이 프로토콜별 규칙을
적용하지 않습니다.
HTTP/1 CONNECT 및 `Connection: Upgrade` 요청은 동일한 승인을 따릅니다.
규칙: 핸들러는 직렬화되기 전에 `inspect` 또는 `passthrough`를 반환해야 합니다.
요청이 업스트림에 기록됩니다. `admit(offer)`가 없는 핸들러 또는 명시적인 핸들러
`reject`는 제한된 로컬 403을 수신하고 원본에는 요청 바이트가 표시되지 않습니다.

## 제한된 비동기 및 스트리밍 정책

HTTP/1.1, HTTP/2, HTTP/3 제한은 하나를 통해 제공됩니다.
`inspection_resource_profile`. 파서 수용 및 지원 작업 공간 예산
함께 검증됩니다. 작업 공간 예산이 0인 경우 정확한 금액을 선택합니다.
파서 제한에서 파생됩니다. 0이 아닌 명시적인 예산만 허용됩니다.
충분히 클 때. `validate()`는 정확한 프로토콜 예산을 식별하거나
모든 구성 오류를 하나로 축소하는 대신 잘못된 제한
일반적인 경우.

```cpp
ntl::net::http::inspection_resource_profile resources;
resources.http1.framing.maximum_body_size = 4 * 1024 * 1024;
resources.http1.maximum_wire_message_size = 4 * 1024 * 1024 + 64 * 1024;
resources.http2.maximum_frame_payload = 1024 * 1024;

if (!resources.valid())
  return reject_configuration(resources.validate());
```

커널 디스패처도 실행 경계를 소유합니다. HTTP/1, HTTP/2, HTTP/3,
gRPC 및 콘텐츠 코덱 변환은 다음 위치에서 공통 디스패처에 입력됩니다.
`PASSIVE_LEVEL`, 제한된 확장 스택 및 작업 공간을 사용하고 다음으로 돌아갑니다.
기다리고 있는 코루틴. 애플리케이션 코드가 `expand_stack()`를 호출하지 않거나
검사를 중심으로 작업자 항목을 관리합니다.

`<ntl/net/http/async_transform>`는 프로토콜 구문 분석을 동기식으로 유지하지만 이동합니다.
고정 작업자 풀에 대한 애플리케이션 정책이 잠재적으로 느려질 수 있습니다. 대기열,
동시성, 기한, 취소 및 페일오픈/페일클로즈 동작은
모두 명시적:

```cpp
ntl::net::http::async_transform_policy_builder builder(
    {}, {.maximum_concurrency = 8,
         .maximum_queue_depth = 1024,
         .timeout = std::chrono::milliseconds(250)});

builder.responses().html().transform(
    [](const auto&, auto& response, const auto& context) {
      if (context.cancellation_requested())
        return ntl::net::http::rewrite_result::block();
      return decide_and_rewrite(response);
    });

auto policy = std::move(builder).build();
auto runtime = ntl::net::http::async_transform_runtime::create(policy);

auto outcome = co_await runtime.apply(request, response, stop_token);
```

`build()`는 런타임이 구성을 실행하기 전에 구성을 고정하므로 규칙
규칙 변경과 `apply()` 실행이 서로 경쟁할 수 없습니다. 기한 만료 또는 외부 중지
실행 중인 콜백의 취소를 요청하고 그 후에만 완료됩니다.
콜백이 반환되므로 파이프라인이나 해당 메시지가 삭제될 수 없습니다.
정책 코드에서는 여전히 이를 사용합니다. 대기 중인 작업을 취소할 수 있습니다.
즉시. `statistics()` 보고서 제출, 완료, 취소,
시간 초과, 오버로드, 대기 및 실행 중인 작업.

`<ntl/net/http/stream_transform>`는 다음과 같이 유지되어서는 안 되는 신체를 위한 것입니다.
완전한 allocation 하나를 사용하며 decode된 평문마다 정책 소유 출력을 제공합니다.
청크별, 전체 스트림 및 확장 범위당 청크 및 트랙:

```cpp
ntl::net::http::stream_transform_pipeline body_policy;
body_policy.chunks().transform(
    [](const auto&, const ntl::net::http::stream_chunk_view& chunk) {
      return rewrite_chunk(chunk.bytes, chunk.input_offset, chunk.final);
    });

ntl::net::inspection::content_decoder_registry decoders;
ntl::net::inspection::content_encoder_registry encoders;
ntl::net::inspection::register_standard_content_decoders(decoders);
ntl::net::inspection::register_standard_content_encoders(encoders);

body_policy.prepare_headers(request, response);
auto body = body_policy.open(request, response, decoders, encoders);
if (!body)
  block();
auto output = body.consume(input_chunk, end_of_message);
```

상태 저장 애플리케이션 프레이밍은 HTTP 메시지별로 생성되어야 합니다. 이렇게 하면 2개가 유지됩니다
부분 gRPC 레코드(또는
사용자 정의 프로토콜 파서):

```cpp
body_policy.chunks()
    .when(is_grpc_message)
    .transform_session([&](const auto& context) {
      return make_grpc_chunk_transformer(context);
    });
```

`<ntl/net/http/http1_stream_transform>`은 임의로 분할된 TLS 평문을 받습니다.
제한된 프레임 상태만 버퍼링하고 고정 길이, 청크, 트레일러를 검증합니다.
구분된 완성을 인증하고 청크된 HTTP/1.1을 내보냅니다.
출력. `<ntl/net/http2/stream_transform>`는 독립적인 HPACK 및 메시지를 소유합니다.
스트림당 상태, 동적 테이블 결합 없이 HEADERS를 재직렬화하고 내보냅니다.
DATA를 즉시 전송하고 흐름 제어를 위해 정확한 보유 바이트 수를 보고합니다.
신용. `<ntl/net/http3/stream_transform>`는 디코딩된 QPACK 헤더, 데이터,
및 QUIC FIN 이벤트; `borrowed_streaming_inspection_sink`는 직접 연결됩니다.
`borrowed_connection_inspector`, 호출자는 QUIC 스트림 ID 매핑을 유지하고
일정을 작성합니다.

`prepare_headers`는 더 이상 유효하지 않은 길이와 검증자 헤더를 제거합니다.
`<ntl/net/inspection/content_stream>`은 HTTP 메시지마다 하나의 증분 코덱 체인을
소유합니다. 임의로 나뉜 입력에 걸쳐 `gzip`, RFC 1950 `deflate`, `br` 또는 등록된
코딩 체인을 디코딩하고, 평문 청크마다 정책을 호출한 다음 압축 상태를 재설정하지
않고 같은 체인으로 다시 인코딩합니다. 입력, 디코딩 결과, 변환 결과, 인코딩 결과,
단계별 크기, 확장률 및 코딩 깊이의 제한은 서로 독립적입니다. 최종 입력에서는
체크섬과 스트림 종료 여부를 검증합니다. 이미 내보낸 바이트는 되돌릴 수 없으므로
스트리밍 변환은 전체 메시지용 `forward_original` 실패 모드를 허용하지 않습니다.
뒤늦게 차단되면 H2/H3 스트림을 재설정하고(H1은 연결을 닫음), 첫 본문 바이트를
전달하기 전에 판정을 원자적으로 내려야 한다면 완전한 메시지 어댑터를 사용하십시오.

## WebSocket, gRPC 및 WebTransport 변환

동일한 분리가 일반 HTTP 메시지를 넘어 확장됩니다.

- `<ntl/net/websocket/transform>`는 조각난 RFC 6455 메시지를 조립합니다.
  UTF-8을 검증하고, 협상된 `permessage-deflate`를 디코딩하고 다시 인코딩합니다.
  클라이언트 마스킹을 시행하고 제한된 재작성 출력을 조각화합니다.
- `<ntl/net/grpc/transform>`는 임의의 HTTP/2 또는 HTTP/3 데이터 분할을 허용합니다.
  5바이트 접두사가 붙은 전체 gRPC 메시지를 추출하고 협상된 내용을 적용합니다.
  `grpc-encoding`는 의미 체계 정책을 호출하고 유효한 메시지 스트림을 내보냅니다.
- `<ntl/net/http3/webtransport_transform>`는 다음에 세션별 정책을 적용합니다.
  신뢰할 수 있는 스트림, 신뢰할 수 없는 데이터그램 및 캡슐을 공유하면서
  WebTransport 스트림/데이터 할당량. 권위를 지닌 흐름 제어 캡슐
  검사할 수는 있지만 콘텐츠 정책에 따라 다시 작성할 수는 없습니다.
- `<ntl/net/http3/webtransport_session>`는 실제 초안-16 HTTP/3을 제공합니다.
  설정, 정적-QPACK 확장 CONNECT 요청/응답, 세션 스트림
  접두사, HTTP 데이터그램 및 백엔드 쓰기. 이동 전용 아웃바운드 스트림
  권한은 반복된 제한된 쓰기, FIN 및 애플리케이션 재설정을 지원합니다.
  재설정은 32비트 WebTransport 응용 프로그램 오류를 등록된 항목으로 매핑합니다.
  HTTP/3 범위 및 MsQuic의 신뢰할 수 있는 오프셋을 전체 세션 접두사로 설정
  송신 측을 중단하기 전에. MsQuic 어댑터는 다음 경우에만 이를 노출합니다.
  QUIC 데이터그램과 Reliable-Reset-at이 모두 협상되었습니다.

이러한 어댑터는 protobuf 스키마, WebSocket 하위 프로토콜 또는
WebTransport 애플리케이션 형식. 애플리케이션은 스키마 파서를 계층화합니다.
완전한 제한된 의미론적 페이로드.

## 재사용 가능한 HTTP/3 검사 구성

`<ntl/net/http3/inspection_proxy>`는 다음과 같은 전송 중립 정책 계층입니다.
HTTP/3 서버 백엔드 및 원본 전송. 백엔드는 QUIC을 소유합니다.
TLS 1.3, QPACK 및 스트림 수명. 프록시는 다음과 같은 규칙을 소유합니다.
모든 애플리케이션에서 다시 구현되어야 합니다.

- 의사 헤더 순서, 고유성 및 필수 필드
- HTTPS SNI-`:authority` 바인딩;
- 홉별 헤더 거부 및 정확한 `Content-Length` 검증
- 요청, 응답, 헤더, 디코딩된 본문, 확장 비율 및 코딩 깊이
  한계;
- 정책 이전에 요청 및 응답 콘텐츠 디코딩 그리고
- 페일클로즈 오류가 있는 `permit`, `block` 및 `drop_flow` 결정을 입력했습니다.

오리진은 검증된 `origin_request`만 받습니다. 정책에는 변경할 수 없는 디코딩 본문
뷰를 제공하고, 허용된 응답은 원래 인코딩된 wire 본문을 유지합니다.

```cpp
auto origin = ntl::net::http3::make_origin_transport(
    [](const ntl::net::http3::origin_request &request) noexcept
        -> ntl::result<ntl::net::http3::origin_response> {
      return send_to_origin_over_h3(request);
    });

auto policy = ntl::net::http3::make_inspection_policy(
    [](const ntl::net::http3::request_view &) noexcept {
      return ntl::net::inspection::verdict::permit;
    },
    [](const ntl::net::http3::response_view &response) noexcept {
      return contains_forbidden_content(response.decoded_body)
                 ? ntl::net::inspection::verdict::block
                 : ntl::net::inspection::verdict::permit;
    });

ntl::net::http3::standard_inspection_proxy proxy(origin, policy);
auto response = proxy.forward(std::move(decoded_http3_request));
```

`<ntl/net/http3/standard_inspection_proxy>`는 표준 gzip, zlib `deflate`, Brotli
디코더를 소유하고 등록합니다. 변환 파이프라인 오버로드는 대응하는 인코더도
등록합니다. 압축 콘텐츠를 변환하는 대상은 디코더와 인코더 백엔드를 모두 포함한
`crtsys_ntl_content_codecs`에 연결합니다. 애플리케이션이 자체 레지스트리를
제공한다면 저수준 `inspection_proxy`를 사용합니다. 일반 생성자는 공유 소유권으로
오리진과 정책 객체를 유지하고 변환/레지스트리 상태도 소유합니다. 동시 요청을
처리하는 서버에서는 오리진과 정책 구현 자체도 스레드 안전해야 합니다.

라이브 raw MsQuic 엔드포인트에서는 `<ntl/net/http3/proxy_connection>`이 해당 메시지
정책 위의 재사용 가능한 연결 어댑터입니다. HTTP/3 제어 스트림과 SETTINGS,
조각난 요청 스트림, 크기가 제한된 QPACK 상태, 요청/오리진 연결, 최종 응답, 취소,
Extended CONNECT, WebTransport 라우팅을 소유합니다. 애플리케이션은 리스너
콜백에서 이 상태 머신을 다시 구현하지 않습니다.

```cpp
ntl::net::http3::async_origin_pool origins(blocking_origin);
ntl::net::http3::msquic_backend::server listener;

listener.open(
    runtime, select_configuration,
    [&](std::shared_ptr<ntl::net::quic::transport_backend> backend,
        const auto &accepted)
        -> ntl::result<std::shared_ptr<ntl::net::quic::backend_sink>> {
      ntl::net::http::inspection_session_metadata session;
      session.tls.server_name = std::string(accepted.server_name);
      session.tls.alpn = "h3";
      auto connection = ntl::net::http3::proxy_connection::create(
          std::move(backend), origins.make_transport(), policy,
          decoders, encoders, std::move(session), observer);
      if (!connection)
        return ntl::unexpected(connection.status());
      return ntl::ok(std::static_pointer_cast<
          ntl::net::quic::backend_sink>(std::move(connection).value()));
    });
```

`<ntl/net/http3/msquic_runtime>`는 공개 MsQuic API, 등록 및
자격 증명이 포함된 구성을 소유합니다. `<ntl/net/http3/msquic_server>`는
리스너와 수락된 연결의 수명을 소유하고 정확한 종료 처리를 보장합니다.
`async_origin_pool::make_transport()`가 반환하는 각 전송 객체는 연결별로
독립적입니다. HTTP/3 스트림 ID는 하나의 QUIC 연결 안에서만 고유하기
때문입니다. 크기가 제한된 작업자 풀은 계속 공유합니다. 종료 순서는 리스너 중지,
수락된 연결 종료 대기, 오리진 풀 종료 대기, 구성 해제, 런타임 해제입니다.

라이브 연결은 기본적으로 오리진에 요청을 제출하기 전에 SNI와 `:authority`를
엄격하게 결합합니다. DNS 비교는 ASCII 대소문자를 구분하지 않고 후행 루트 점
하나를 허용하며, 포트를 생략하면 443으로 처리합니다. SNI가 없거나 호스트/포트가
다르거나 IPv6 authority가 모호하게 괄호 없이 쓰이면 실패 시 차단합니다. 일반
CONNECT와 알 수 없는 확장 CONNECT 프로토콜도 터널 핸들러가 명시적으로 승인하지
않으면 오리진 제출 전에 종료합니다. 의미상 본문이 빈 경우 정책은 `headers`와
`message_complete` 단계는 받지만 길이 0의 가상 `body_chunk` 단계는 받지 않습니다.

## TLS 및 HTTPS 경계

TLS용 WFP 스트림 데이터는 암호문입니다. 코덱이나 코루틴을 다시 조립할 수 있습니다.
TLS record를 볼 수는 있지만 session key나 TLS 상태 없이는 HTTP 평문을 얻을 수
없습니다. 따라서 평문 정책은 다음 경계 중 하나에 속합니다.

- 이미 TLS 세션을 소유하고 있는 엔드포인트 내부
- 애플리케이션이 승인된 키 자료를 명시적으로 제공한 후 또는
- 인증서가 있는 별도로 설계된 TLS 종료 프록시에서,
  신원, 신뢰, 우회, 업데이트 및 실패 정책.

`<ntl/net/tls/stream>`은 사용자 모드 Schannel 전송 경계를 제공합니다. TLS를
종단한 뒤 `<ntl/net/tls/framed_stream>`은 복호화하고 남은 바이트를 보존하며,
호출자가 정의한 완전한 메시지를 동일한 `content_view`와 형식화된 판정 API에
전달합니다. `<ntl/net/http/http1_framing>`은 크기가 제한된
`Content-Length`/`chunked` HTTP/1.x 경계. `<ntl/net/websocket/framing>`
검증된 업그레이드 후 RFC 6455 프레임과 조각난 메시지를 처리합니다.
`<ntl/net/http2/framing>` 및 `<ntl/net/http3/framing>`는 경계선 프레임을 제공합니다.
`<ntl/net/http2/hpack>`는 완전한 경계형 상태 저장 HPACK 디코더를 추가합니다.
`<ntl/net/http3/qpack>`는 제한된 제로 동적 테이블 QPACK 프로필을 추가하는 반면
동적 QPACK은 명시적인 공급자 계약으로 남아 있습니다. 전송 디코딩,
압축 해제, 의미론적 정책 및 QUIC 전송은 나중에 명시적으로 유지됩니다.
단계. 브라우저 예제는 HTTP/2 프레임, HPACK, 콘텐츠를 구성합니다.
디코더/인코더 및 WebSocket permessage-deflate 레이어를 다중화
변신 릴레이. 제한된 전체 메시지만 버퍼링하고 보충합니다.
유지된 수신 창은 대상 송신 창을 따르고,
두 가지 정책 방향을 엔드포인트당 하나의 TLS 기록기로 직렬화합니다. 참조
[사용자 모드 Schannel TLS 스트림](./tls-stream.ko-KR.md) 및
[`tls-inspection-proxy` 샘플](../../examples/wfp/user/tls-inspection-proxy).
장기 실행 브라우저 수명 주기 및 HTML 로깅 워크플로를 시연합니다.
에 의해
[`browser-https-inspection`](../../examples/wfp/user/browser-https-inspection).
해독된 QUIC 공급자 경계는 다음과 같이 설명됩니다.
[`http3-inspection`](../../examples/wfp/user/http3-inspection).

TLS 전송은 신뢰 배포 정책과 별개입니다.
NTL은 애플리케이션이 제공한 승인된 CA로 호스트별 인증서를 발급할 수 있지만, 해당
CA를 만들거나 설치하지 않으며 피어 검증도 비활성화하지 않습니다. ALPN은 하나의
파서 계열을 선택하며, 구현되지 않은 계열을 HTTP/1로 추측하지 않습니다. 확인된
ECH, 인증서 고정, mTLS ID 선택, 누락된 압축 코덱, 사용할 수 없는 QUIC 백엔드는
`<ntl/net/tls/inspection_policy>`에서 서로 다른 결과입니다. 원시 확장 형식
`0xfe0d`는 GREASE ECH와 wire 형태가 같으므로 관찰 정보로만 사용합니다. 정책의
기본값은 fail-closed입니다. 자세한 내용은
[HTTP 및 WebSocket 프로토콜 검사](./protocol-inspection.ko-KR.md).

`<ntl/net/tls/product_policy>`는 이러한 관찰 결과와 배포된 기능을 하나의 실행 동작으로
변환합니다. 가능한 동작은 가로채기, 암호문을 변경하지 않는 터널링, 메타데이터만
관찰, QUIC 종료, 또는 QUIC를 차단하고 애플리케이션의 정상 TCP 재시도를 기다리는
것입니다. 브라우저 설정은 절대 변경하지 않으며 ECH, 인증서 고정, 누락된 mTLS
ID를 "우회됨"으로 표시하지도 않습니다. WFP가 암호화 권한을 만들어낼 수는 없으므로
ECH 프런트엔드와 승인된 mTLS ID 공급자는 제품이 제공해야 합니다.

`<ntl/net/tls/product_backend>`는 이러한 제품 기능을 감사 가능한 단일 선택으로
묶습니다. ECH 공급자, 애플리케이션/호스트 신뢰 분류, 캐시된 다운스트림 인증서
발급자를 조합합니다. ECH가 아닌 연결에는 Schannel 서버 ID를 반환하고, 성공적으로
복호화한 ECH에는 프런트엔드 소유 평문 채널을 반환합니다. 외부 TLS/QUIC 상태는
Schannel에서 재개할 수 없기 때문입니다. 확인된 불투명 ECH, 고정됐거나 알 수 없는
다운스트림 신뢰, 누락된 오리진 mTLS ID는 각각 별도의 fail-closed 감사 이벤트입니다.
`audited_origin_client_identity_provider`는 애플리케이션이 제공하는 mTLS 선택기에도
같은 크기 제한 감사 계약을 적용합니다.
