# NTL 네트워크 dual-runtime 모델

[English](./network-dual-runtime.md) · [NTL 문서](./README.ko-KR.md)

NTL은 의미와 비용을 모두 보존할 수 있을 때 사용자 모드와 커널에 API를
공유합니다. allocator, scheduler, transport, IRQL, 암호 provider 차이 때문에
한쪽이 느려지거나 불안전해진다면 동일한 signature나 구현을 억지로 강제하지
않습니다.

```text
bounded 프로토콜 코어       사용자 직접 | 커널 직접
정책 / transform 계약       사용자 직접 | 커널 직접 | 명시적 offload
OS·codec backend             Winsock/WSK, Schannel, MsQuic, zlib/Brotli, RPC
```

커널 통합 헤더는 [`<ntl/net/kernel/all>`](../../include/ntl/net/kernel/all)이며,
지원되는 커널 API만 포함합니다.

## API 공유 원칙

- protocol 값, parser, message model, 정책 callback, verdict, transform 결과는
  양쪽에서 같은 작업이 유효하면 하나의 구현을 사용합니다.
- `crtsys`는 드라이버에서 MSVC STL container, `std::function`, smart pointer,
  C++ exception을 지원합니다. 따라서 문서화된 `PASSIVE_LEVEL` 경로에서는
  인위적인 커널 전용 container 계층 대신 같은 owning C++ API를 사용할 수 있습니다.
- `PASSIVE_LEVEL`보다 높은 WFP callback은 resident bounded fast path를 사용하거나
  allocation·callback·exception 가능 코드 전에 passive worker로 넘깁니다.
- user/kernel transport는 같은 connection·stream API를 노출하되 native handle,
  callback, shutdown 규칙은 명시적인 `user_*`·`kernel_*` backend가 담당합니다.
- 공유 때문에 불필요한 copy, allocation, lock, capability check가 hot path에
  추가된다면 그 API는 분리합니다.

## 공개 소유권 계약

- 일반 factory와 callback은 사용하는 provider, runtime, credential, policy,
  sink, workspace, native state를 보유합니다. member 선언 순서와 상위 객체 소멸
  순서는 API 계약이 아닙니다.
- `close()`는 멱등이며 새 작업 접수를 중단합니다. close 전에 접수된 작업은
  child와 callback이 끝날 때까지 native state를 유지합니다.
- 커널의 마지막 해제가 `DISPATCH_LEVEL` 이하에서 발생하면 할당 없이 드라이버
  런타임의 합류 가능한 PASSIVE 정리 도메인으로 넘깁니다. 애플리케이션은 이
  객체들을 위해 detached 정리 작업이나 rundown을 관리하지 않습니다.
- 이름에 `borrowed_`, `_view`, `_ref`가 있으면 명시적인 비소유 값입니다. 이
  값은 동기 callback이나 low-level adapter 범위에서 사용하며, 그 범위를 넘어
  보존해야 하면 데이터를 복사하거나 owning factory를 사용합니다.
- 일반 예제는 소유권을 갖는 객체를 사용합니다. native ABI 경계를 설명하는 파일만
  이름으로 borrowed 입력임을 분명히 표시한 API를 호출합니다.

## 커널에서 직접 실행되는 범위

| 기능 | 커널 계약 |
|---|---|
| 조각난 버퍼·framing | `scatter_view`, `borrowed_bounded_writer`, caller-owned 저장소 |
| 비동기 수신·코루틴 | 고정 용량 `async_byte_stream`, 단일 reader 강제, timeout/cancel, PASSIVE_LEVEL resume, owning task state, 선택적 deterministic drain |
| 양방향 코루틴 수명 | lazy `bidirectional_status_task` 두 개를 `join_bidirectional`로 결합하며 오류 시 취소와 양쪽 종료 후 재개를 강제 |
| HTTP/1 | bounded 요청·응답 framing |
| HTTP/2 | frame 파싱과 HPACK primitive |
| HTTP/3 | QUIC varint, frame/capsule/datagram, caller scratch 기반 static·Huffman QPACK |
| gRPC | 5바이트 메시지 framing과 무할당 인코딩 |
| WebSocket | frame 검증과 무할당 unmask |
| WebTransport | 협상, stream prefix, capsule, quota guard |
| TLS | bounded ClientHello, SNI, ALPN, ECH extension 관찰 |
| 정책·rewrite | 고정 callback, typed verdict, caller-owned 출력의 `borrowed_transform_pipeline` |
| TCP/byte transport | Winsock, WSK, offload가 구현하는 `io::transport_backend`와 callback provider 경계 |
| QUIC | `ntl::net::quic::transport_backend` 규약과 callback provider 경계 |

직접 실행되는 커널 backend에는 WSK TCP listener/client,
`async_transport_stream`, Schannel TLS client/server와 종료, CNG/DER X.509,
zlib/gzip/Brotli codec, 공식 NMR 기반 MsQuic와 HTTP/3/QPACK이 포함됩니다.
각 backend는 native provider, `PASSIVE_LEVEL`, bounded memory, key lifetime,
cancel과 종료 계약을 명시합니다. 브라우저 process 실행과 신뢰 저장소·제품
정책 관리는 사용자 모드 책임으로 유지합니다.

### 커널 Schannel credential 수명

`kernel::schannel`은 모든 native credential을 소유하고 드라이버 런타임의 합류
가능한 PASSIVE_LEVEL 정리 도메인으로 폐기합니다. credential 생성은 계속
`PASSIVE_LEVEL`에서 수행하지만,
credential handle은 `DISPATCH_LEVEL` 이하에서 복사하고 소멸시킬 수 있습니다.
`schannel`을 닫으면 native handle이 무효화되고 해제되며, 그 뒤에 소멸하는
credential handle도 안전한 빈 handle로 남습니다. 따라서 member 선언 순서는
API 계약이 아닙니다. 복사본은 같은 credential을 공유하므로 하나를 해제해도
나머지는 유효하며, 마지막 복사본을 해제할 때 런타임 정리 도메인에서 native
정리를 예약합니다.

```cpp
class tls_service {
  std::vector<ntl::net::kernel::schannel_credentials> identities_;
  ntl::net::kernel::schannel schannel_;

public:
  ntl::status add_identity(
      const ntl::net::kernel::schannel_certificate_store_ref &certificate) {
    auto acquired = schannel_.try_server(certificate);
    if (!acquired)
      return acquired.status();
    identities_.push_back(std::move(*acquired));
    return ntl::status::ok();
  }

  void shutdown() noexcept { (void)schannel_.close(); }
};
```

`close()`는 새 credential을 차단하고, 진행 중인 native credential 사용과
PASSIVE_LEVEL 정리를 기다립니다. 드라이버 런타임은 unload 전에 정리 worker에
합류하며, 소멸자도 같은 credential 종료를 시작합니다.
transport를 먼저 종료하는 순서는 service가 정상적인 protocol 종료 완료를 확인할
때 유용하지만, 메모리 안전성이나 member 선언 순서의 필수 조건은 아닙니다. 아래의
구조화된 transport helper가 결정적인 종료 경로를 제공합니다.

### 구조화된 비동기 transport 수명

요청이나 session 단위의 커널 작업은 `io::with_async_transport`를 사용합니다.
이 함수가 생성부터 callback 합류까지 소유하므로 작업의 성공, 실패, 예외와
관계없이 조기 `co_return`이 정리를 건너뛸 수 없습니다.

```cpp
co_return co_await ntl::net::io::with_async_transport(
    backend, 256 * 1024,
    [&](std::shared_ptr<ntl::net::io::async_transport_stream> stream)
        -> ntl::net::kernel::task<ntl::status> {
      co_return co_await inspect(*stream);
    });
```

TLS 연결에는 더 강한 수명 경계인 `kernel::with_tls_connection`을 사용합니다.
이 함수는 관리되는 transport 안에서 TLS stream을 소유하고, 모든 반환 경로에서
close-notify를 시도한 뒤 transport callback을 합류합니다. 작업 callback은 TLS
stream과 기반 transport를 함께 받습니다. 기반 transport는 raw 양방향 전달로
전환해야 하는 터널 프로토콜에서만 사용합니다.

```cpp
co_return co_await ntl::net::kernel::with_tls_connection(
    backend, 256 * 1024,
    {.maximum_buffered_ciphertext = 256 * 1024},
    [&](std::shared_ptr<ntl::net::kernel::tls_stream> tls,
        std::shared_ptr<ntl::net::io::async_transport_stream>)
        -> ntl::net::kernel::task<ntl::status> {
      const auto handshaken = co_await tls->handshake_client(
          credentials, server_name, protocols, true);
      if (!handshaken.is_ok())
        co_return handshaken;
      co_return co_await inspect(*tls);
    });
```

`kernel::tls_stream`을 직접 생성해도 transport 상태를 함께 보유합니다.
요청 단위 작업에는 결정적인 structured shutdown까지 수행하는
`with_tls_connection()`을 우선 사용하고, 장기 실행 service는 소유권을 갖는 stream
객체를 직접 보관할 수 있습니다.

장기 실행 service가 `async_transport_stream`의 마지막 shared owner를 해제하면
새 작업을 닫고 provider 정리를 runtime PASSIVE domain으로 넘깁니다.
`co_await stream.stop_and_drain()`은 이후 코드가 종료 완료를 확인해야 할 때 쓰는
선택적이고 결정적인 경계입니다. 이 await는 수신 측이 이미 idle인 경우에도
provider의 blocking join 전에 work queue scheduling 경계를 통과하므로,
continuation이 자신을 재개 중인 callback을 기다리는 교착이 생기지 않습니다.

### 양방향 코루틴 결합

커널에서 직접 만든 양방향 터널은 각 방향을 lazy
`bidirectional_status_task`로 표현하고 하나의 structured join으로 소비합니다.

```cpp
co_return co_await ntl::net::kernel::join_bidirectional(
    relay(client, origin),
    relay(origin, client),
    [&]() noexcept {
      client.stop();
      origin.stop();
    },
    &is_expected_disconnect);
```

방향별 task에는 의도적으로 `wait()`와 개별 `operator co_await()`가 없습니다.
`join_bidirectional()`이 양쪽을 모두 시작한 뒤 최초의 비성공 결과에서 취소를
정확히 한 번 실행하고, 두 coroutine frame이 모두 final suspend에 도달한 뒤에만
부모를 재개합니다. 한 방향이 성공하면 반대 방향은 정상적으로 마칠 수 있습니다.
예상 종료 predicate는 비성공 결과를 join 실패로 보고할지만 결정하며 취소를
억제하지 않습니다. 일반 HTTP/TLS 검사 정책 코드는 이 API를 직접 사용할 필요가
없으며, 사용자가 양방향 터널을 구현할 때만 적용되는 수명 경계입니다.

다음 사용자 convenience API 계열은 명시적인 커널 경로를 가집니다.

| 사용자 convenience 계열 | 커널 경로 |
|---|---|
| owning HTTP/1, HTTP/2, HTTP/3 transform | `PASSIVE_LEVEL`에서는 같은 owning API, hot path에는 caller-storage bounded overload, 제품 정책에 따라 선택적 offload |
| gRPC와 WebSocket transform | `PASSIVE_LEVEL`에서는 같은 transform API, hot path에는 bounded framing·payload overload |
| WebTransport session helper | `quic::transport_backend` 위에서 협상, stream prefix, capsule, datagram, quota 코어를 직접 실행 |
| zlib/Brotli decoder·encoder registry | 검증된 직접 kernel codec backend 또는 명시적 `decode_content` / `encode_content` offload |
| Schannel stream, acceptor, 인증서 cache, TLS frontend | 지원되는 직접 kernel provider backend 또는 명시적 `tls_terminate` / `issue_certificate` offload |
| Winsock/WSK와 MsQuic 객체 | 명시적인 user Winsock/MsQuic와 kernel WSK/MsQuic backend 위의 공통 의미 API |

header 수준의 경계도 의도적으로 나뉩니다.

| 사용자 전용 header | 커널용 대체 경로 |
|---|---|
| `io/async_socket`, `io/async_framed_stream` | `io::transport_backend`, `async_byte_stream`, 공통 framing probe 조합 |
| HTTP/1, HTTP/2, HTTP/3 owning·stream transform | protocol framing과 `borrowed_transform_pipeline`; 비동기 owner는 `offload::async_backend`로 작업을 pend |
| `grpc/transform`, `websocket/transform`, `websocket/permessage_deflate` | 같은 framing·rewrite API와 kernel codec backend; hot path는 bounded overload 또는 명시적 offload |
| dynamic `http3/qpack`, inspection proxy, WebTransport session·transform | `kernel::msquic_provider`와 공통 QUIC backend 위에서 직접 실행하거나 제품 정책에 따라 offload |
| content decoder·encoder registry와 stream | `kernel/content_codecs`의 bounded zlib/gzip/Brotli backend 또는 명시적 `decode_content`·`encode_content` offload |
| Schannel TLS stream·acceptor·certificate·frontend header | `kernel/schannel`, `kernel/tls_stream`, `kernel/x509` 직접 backend 또는 명시적 offload |

header guard는 검증된 실행 환경 경계를 나타냅니다. 공통 의미는 shared core에
두고 native 연동은 명시적인 backend가 담당하며, offload는 더 안전하거나
운영상 유리할 때 선택합니다.

이는 같은 의미의 API이지 동일한 binary 구현을 두 주소 공간에 복사한다는
뜻은 아닙니다. direct/offloaded 경로와 제한은 항상 관찰할 수 있습니다.

WFP 사용자·커널 쌍 예제는 이 원칙을 코드로 보여 줍니다. connect redirect,
TCP/UDP content filter, TLS inspection, 브라우저 HTTP/1.1·2·3 검사, gRPC와
WebTransport는 각 쌍마다 의미 정책 하나를 공유합니다. 사용자 모드와 커널
모드는 native transport와 scheduler만 다르게 선택하며 permit/block/drop이나
transform 규칙을 다시 정의하지 않습니다.

## 빌드와 배포 계약

소스 기반 CMake 프로젝트는 직접 gzip/deflate/Brotli가 필요할 때만
`crtsys_add_driver`에 `KERNEL_CONTENT_CODECS`를 전달합니다. 커널 드라이버에서
MsQuic를 사용할 때는 같은 호출에 `KERNEL_MSQUIC`를 전달합니다. 두 옵션은 각각
드라이버용 codec header와 archive, 또는 Windows 10 version 2004 target, 고정된
`crtsys_ntl_msquic_headers` ABI와 `netio.lib` NMR client import를 선택합니다.
사용자 모드 target은 `crtsys_add_ntl_msquic_headers()`를 직접 호출할 수 있습니다.
어느 경로도 DLL이나 커널 provider를 설치하지 않습니다.

Visual Studio/NuGet에서 `NTL WFP`를 선택하면 커널 codec header, `Z_SOLO`,
드라이버용 두 archive가 자동으로 연결됩니다. 사용자 앱에는 별도의 사용자 모드
zlib/Brotli archive가 연결됩니다. offline prebuilt bundle도 두 layout을 모두
포함합니다. NuGet과 offline bundle은 정확히 고정하고 SHA-256으로 검증한
`msquic.h`도 포함해 사용자·커널 consumer에 자동으로 제공합니다. DLL이나 커널
provider는 설치하지 않습니다. 커널 provider를 실제 사용하는 드라이버는
`CrtSysUseNtlKernelMsQuic=true`를 명시하며, 이 선택이 Windows 10 version 2004
이상 계약과 NMR client 호출에 필요한 `netio.lib`를 활성화합니다. package 테스트는 파일 존재만 확인하지
않고 사용자 HTTP/3 backend와 커널 NMR wrapper를 이 ABI로 컴파일하며 실제 codec
registry도 링크합니다.

## direct와 offload는 구분됩니다

[`runtime_descriptor`](../../include/ntl/net/runtime)는 실행 환경,
direct/offloaded 경로, feature bit, hard limit를 담습니다. backend가 기능을
광고하지 않으면 실행하지 않으며, 조용한 fail-open이나 fallback은 없습니다.

```cpp
ntl::net::runtime_descriptor service{
    .domain = ntl::net::execution_domain::user,
    .path = ntl::net::execution_path::offloaded,
    .features = ntl::net::feature_set(
        ntl::net::network_feature::content_transform),
};

auto status = service.require(
    ntl::net::network_feature::content_transform,
    ntl::net::execution_path::offloaded);
```

[`offload::request_header`](../../include/ntl/net/offload/protocol)와 응답은
versioned, pointer-free, 고정 폭 구조입니다. request/flow ID, protocol,
content kind, 방향, 포트, 입출력 제한, timeout, fail-closed 의도와 typed
`permit` / `block` / `drop_flow` verdict를 전달합니다.
NTL RPC reliable notification을 control
plane으로 사용하고 큰 본문은 bounded shared memory data plane을 선택할 수
있습니다. [IPC 문서](./ipc.ko-KR.md)와 WFP TCP/UDP content-filter 예제를 보십시오.

`offload::backend`는 동기식이므로 wait 가능한 `PASSIVE_LEVEL` 경로에서만
사용합니다. 커널 분류·transport 경로는 `offload::async_backend`를 사용합니다.
수락된 input/output storage는 inline일 수도 있는 단 한 번의 completion까지
유지되고, cancel 뒤에도 completion은 정확히 한 번 발생하며, `stop()`과
`drain()`이 driver unload 경합을 닫습니다. content-filter 예제의 reliable NTL
RPC 흐름에 이 backend를 연결하면 classify callback에 protocol parser, codec,
인증서 private key, blocking service call을 넣지 않아도 됩니다.

## 두 환경에서 같은 transform API

```cpp
struct byte_policy {
  std::string_view blocked_text;
};

ntl::status inspect_content(
    void *, const ntl::net::transform_context &,
    ntl::net::inspection::content_view input) noexcept {
  return input ? ntl::status::ok()
               : ntl::status{STATUS_INVALID_PARAMETER};
}

ntl::result<std::size_t> copy_content(
    void *, const ntl::net::transform_context &,
    ntl::net::inspection::content_view input,
    std::span<std::byte> output) noexcept {
  if (output.size() < input.size())
    return ntl::unexpected(STATUS_BUFFER_TOO_SMALL);
  const ntl::status copied =
      input.bytes().copy_to(output.first(input.size()));
  if (!copied.is_ok())
    return ntl::unexpected(copied);
  return ntl::ok(input.size());
}

ntl::result<ntl::net::inspection::verdict> decide_content(
    void *opaque, const ntl::net::transform_context &,
    ntl::net::inspection::content_view input) noexcept {
  const auto &policy = *static_cast<const byte_policy *>(opaque);
  const auto blocked = input.contains(policy.blocked_text);
  if (!blocked)
    return ntl::unexpected(blocked.status());
  return ntl::ok(*blocked ? ntl::net::inspection::verdict::block
                          : ntl::net::inspection::verdict::permit);
}

ntl::result<ntl::net::transform_result> apply_byte_policy(
    const ntl::net::transform_context &context,
    ntl::net::inspection::content_view input,
    std::span<std::byte> caller_owned_output,
    byte_policy &policy) noexcept {
  ntl::net::borrowed_transform_pipeline pipeline;
  pipeline
      .inspect({&inspect_content, &policy})
      .transform({&copy_content, &policy,
                  ntl::net::execution_path::direct})
      .decide({&decide_content, &policy});
  return pipeline.run(context, input, caller_owned_output);
}
```

pipeline은 `std::function`을 저장하지 않고 메모리를 할당하지 않으며 callback
state를 소유하지 않습니다. 따라서 `byte_policy`는 `apply_byte_policy()` 호출이
끝날 때까지 살아 있어야 합니다. 출력 저장소도 호출자가 제한된 크기로 제공하며
공간이 부족하면 명시적인 오류가 됩니다. `content_view`는 조각난 저장소를 볼 수
있지만, 입력 자체는 호출자가 선택한 완전한 단위여야 합니다. 예를 들면 UDP
datagram 하나 또는 TCP framer가 완성한 message 하나입니다. 이 byte pipeline이
TCP를 자동 재조립하거나 HTTP 의미를 해석하지는 않습니다.

HTTP method, path, header, body와 연결 metadata를 기준으로 규칙을 만들 때는
HTTP/1.1, HTTP/2, HTTP/3 adapter 위의 의미 기반
[`http::inspection_policy`](../../include/ntl/net/http/inspection_policy)를 사용합니다.

```cpp
void configure_http_policy(ntl::net::http::inspection_policy &policy) {
  namespace condition = ntl::net::http::condition;
  policy.requests()
      .at_headers()
      .when(condition::method_is("POST"))
      .when(condition::path_is("/inspect"))
      .decide([](const ntl::net::http::inspection_context_view &) {
        return ntl::net::inspection::verdict::block;
      });
}
```

두 API는 의도적으로 추상화 수준이 다릅니다. `net::borrowed_transform_pipeline`은
무할당 byte/content primitive이고, `net::http::inspection_policy`는 HTTP 의미
기반 규칙과 변조 API입니다.

사용자 서비스 정책도 WFP action을 직접 노출하지 않는 typed stage입니다.

```cpp
ntl::net::inspection::verdict decide_with_service(
    std::shared_ptr<ntl::net::offload::backend> service,
    const ntl::net::transform_context &context,
    ntl::net::inspection::content_view input) noexcept {
  ntl::net::offload::inspect_adapter service_policy(
      std::move(service), 2'000);
  ntl::net::borrowed_transform_pipeline pipeline;
  pipeline.decide(service_policy.stage());

  const auto decision = pipeline.run(context, input);
  if (!decision)
    return service_policy.failure_verdict();
  return decision->verdict;
}
```

## QUIC 백엔드

[`ntl::net::quic`](../../include/ntl/net/quic/transport)는 HTTP/3가 사용하는 공통
transport 계약을 정의합니다.
[`borrowed_callback_transport`](../../include/ntl/net/quic/borrowed_callback_transport)는 커널·사용자
provider를 연결하는 무할당 경계입니다. 실제 MsQuic connection 타입은
`ntl::net::http3::msquic_backend`에 있으며, 커널 NMR provider의 획득과 수명은
[`ntl::net::kernel::msquic_provider`](../../include/ntl/net/kernel/msquic)가
관리합니다.

[MsQuic은 Windows kernel mode를 공식 지원](https://microsoft.github.io/msquic/msquicdocs/docs/Platforms.html)하고,
공개 API에는 kernel 전용 status, IRQL annotation과 공통 function-table object
model이 있습니다. 따라서 NTL은 같은 connection·stream 의미 뒤에
사용자·커널 실행 경로를 제공합니다. inbox `msquic.sys`의 문서화되지 않은
import 계약을 안정 ABI라고 가정하지 않습니다.
kernel backend는 문서화된 provider/import 계약 또는 고정된 공식 MsQuic kernel
빌드를 사용해야 합니다. 사용자 서비스 offload는 선택지이지 유일한 kernel
QUIC 경로가 아닙니다.

## 커널 수명 규칙

- network callback의 view는 callback 수명 동안만 유효합니다.
- 데이터를 보존하려면 bounded nonpaged ownership 또는 caller storage가
  필요합니다.
- 큰 bounded scratch는 커널 스택 프레임이 아니라
  [`kernel::workspace_pool`](../../include/ntl/net/kernel/workspace_pool)에 둡니다.
  `try_acquire()`는 nonpaged lookaside list 기반의 move-only RAII lease를
  반환하므로 호출자가 native pool 메모리를 직접 관리하지 않습니다.
- `http::inspection_resource_profile`이 parser 제한과 workspace 제한의 단일
  기준입니다. workspace budget을 0으로 두면 parser 제한으로부터 필요한 크기를
  자동 산정하며, 명시한 크기가 부족하면 traffic을 받기 전에 protocol별 진단으로
  거부합니다.
- 커널 HTTP dispatcher가 PASSIVE_LEVEL 전환과 stack 격리를 내부에서 수행합니다.
  사용자는 HTTP/1, HTTP/2, HTTP/3, gRPC 또는 codec 검사 주변에서
  `expand_stack()`이나 work item을 직접 관리하지 않습니다.
- 커널 HTTP/1·HTTP/2 session은 메모리 소유권을 의도적으로 나눕니다. 큰
  wire/frame buffer는 호출자가 소유한 고정 nonpaged workspace lease에서 얻고,
  활성 lease quota가 소진되면 flow를 fail-close합니다. 크기가 달라지는 semantic
  message, header, body, trailer와 변환된 wire 출력에는 `http::message_memory_ref`를
  전달합니다. 커널 session은 이 resource를 crtsys nonpaged allocator 기반의
  `borrowed_bounded_memory_resource`에 연결하며 전체 할당량과 단일 할당량을 모두
  제한합니다. HTTP/3도 connection마다 같은 계약을 적용합니다. 제한을 넘으면
  `std::bad_alloc`을 `STATUS_INSUFFICIENT_RESOURCES`로 바꾸고 조용히 통과시키지
  않고 해당 stream 또는 flow를 종료합니다. codec은 이와 별도로 byte 수, 할당
  수, 출력 크기와 확장 비율 제한을 적용합니다. semantic allocation의 peak도
  운영 진단에서 조회할 수 있습니다. 사용자 모드는 같은 message API를 유지하고,
  별도 제한이 필요하지 않으면 기본 PMR resource를 사용합니다. 각 exchange가
  끝나면 semantic 상태를 해제하고, 구조화된 transport drain 완료 뒤 활성
  workspace lease가 0인지 검증합니다.
- `http2::prepare_kernel_proxy_session`은 origin 연결을 열기 전에 첫
  downstream 요청을 판정합니다. move-only preflight 결과가 workspace
  lease와 bounded origin-facing frame을 소유합니다. block은 origin에
  접속하지 않고 downstream에 응답하며, permit 결과는
  `run_kernel_proxy_session`이 정확히 한 번만 소비할 수 있습니다.
  브라우저 세션은 local SETTINGS로 Extended CONNECT를 알릴 수 있고,
  adapter는 그 local SETTINGS에 대응하는 ACK만 소비하여 이후 origin
  SETTINGS 교환은 그대로 유지합니다.
- 무할당 parser도 입력과 callback state가 resident이고 native API가 허용하는
  IRQL에서만 실행합니다.
- 할당·상태 보존 작업은 `PASSIVE_LEVEL`에서 실행합니다.
- [`kernel::executor`](../../include/ntl/net/kernel/executor)는 종료와 post의
  경합을 닫고, 수락한 callback을 모두 drain한 뒤 unload합니다.
- `async_transport_stream`은 마지막 owner가 해제되면 runtime PASSIVE cleanup
  domain에서 닫고 drain합니다. 요청·session 단위 작업은 결정적인 완료를 위해
  `with_async_transport`를 사용합니다. 이후 코드가 provider callback의 완전한
  종료를 확인해야 할 때만 명시적인 `stop_and_drain()`을 사용합니다.
- quota, timeout, cancel, fail-open/fail-closed는 명시합니다.

## 예제와 테스트

- [`test/net/kernel-contracts`](../../test/net/kernel-contracts/)는 합성 IOCTL
  테스트 전송으로 같은 gRPC와 transform 계약을 앱과 드라이버에서 실행합니다.
  조각난 전달, timeout, cancel, EOF, reader 경합, 용량 제한, unload drain을
  포함한 커널 coroutine byte-stream 시험도 여기서 담당하므로 예제에는 합성
  트래픽 코드가 들어가지 않습니다.
- [`examples/wfp/kernel/flow-monitor`](../../examples/wfp/kernel/flow-monitor/)에는
  WFP flow/stream 관찰 정책과 telemetry 경로만 있고, runtime fixture가 검증용
  IPv4/IPv6 트래픽을 생성합니다.
- WFP [`user/tcp-content-filter`](../../examples/wfp/user/tcp-content-filter/)와
  [`user/udp-content-filter`](../../examples/wfp/user/udp-content-filter/)는 user policy
  offload를, 대응하는 [`kernel/tcp-content-filter`](../../examples/wfp/kernel/tcp-content-filter/)와
  [`kernel/udp-content-filter`](../../examples/wfp/kernel/udp-content-filter/)는 같은
  bounded framing과 내용 verdict를 드라이버에서 직접 실행하는 경로를 보여줍니다.
- 서로 대응하는 [`user/browser-https-inspection`](../../examples/wfp/user/browser-https-inspection/)과
  [`kernel/browser-https-inspection`](../../examples/wfp/kernel/browser-https-inspection/)은
  HTTP/1.1·HTTP/2·HTTP/3에 같은 `http::inspection_policy`를 적용합니다. 사용자
  서비스는 Winsock·Schannel·MsQuic를, 커널 드라이버는 WSK·kernel Schannel·MsQuic
  NMR backend를 사용합니다. 둘 다 permit/block/drop, header·body rewrite, content
  decoding과 bounded HTML capture를 지원하며 브라우저를 실행하거나 설정을 바꾸지
  않습니다.
- [`kernel/tls-inspection-proxy`](../../examples/wfp/kernel/tls-inspection-proxy/)는
  브라우저 aggregate 없이 redirected TLS 경계를 살펴보는 더 작은 WSK/Schannel
  대응 예제입니다.
- [`kernel/http3-inspection`](../../examples/wfp/kernel/http3-inspection/)는 공식
  MsQuic NMR provider 위에서 실제 kernel QUIC/TLS 1.3, SETTINGS, QPACK과
  IPv4/IPv6 WFP policy를 검증합니다.
- `test/wfp/compile/kernel.cpp`는 커널 통합 헤더를 `/W4 /WX`로 컴파일합니다.
- `protocols.cpp`는 같은 corpus를 사용자 모드에서 실행합니다.
- `fuzz.cpp`는 조각난 HTTP, HPACK/QPACK, WebSocket, gRPC, capsule, codec,
  두 ClientHello parser를 검증합니다.
