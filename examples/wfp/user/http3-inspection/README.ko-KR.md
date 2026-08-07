# 사용자 모드 HTTP/3 검사

[English](./README.md) · [WFP 예제](../../README.ko-KR.md)

이 예제는 제품 실행 경로와 acceptance 트래픽을 분리합니다.

- `crtsys_wfp_http3_inspection_driver.sys`는 애플리케이션 범위의 IPv4/IPv6
  `ALE_AUTH_CONNECT` WFP gate와 제한된 telemetry만 담당합니다.
- `crtsys_wfp_http3_inspection_service.exe`는 동적 WFP 정책을 소유하고,
  사용자 모드에서 실제 MsQuic TLS 1.3, HTTP/3, QPACK, 콘텐츠 압축 해제와
  WebTransport 처리를 수행합니다.
- 제어된 클라이언트, 트래픽 생성, 결과 판정과 `PASS` 표시는
  `test/wfp/runtime/fixtures/user/http3-inspection`에만 있습니다. 제품 실행
  파일에는 포함하지 않습니다.

서비스는 크기가 제한된 HTTP/3 framing, static/dynamic QPACK, blocked stream
재개와 decoder acknowledgement, gzip/deflate/Brotli HTML, 그리고
`X-NTL-Block: 1` 허용·차단 정책을 처리합니다. WebTransport 경로는 Extended
CONNECT, 양방향·단방향 stream, HTTP Datagram, 여러 DATA frame에 걸친 Capsule
재조립, reliable reset mapping, session 활성화 전 차단을 다룹니다. UDP
datagram 하나를 완전한 HTTP 메시지로 간주하지 않습니다.

WFP 정책은 제어 대상 실행 파일, 프로세스, 주소 패밀리, 프로토콜과 선택된
loopback port로 제한됩니다. 서비스는 숫자 증거만 기록하고 acceptance 성공
여부를 판정하지 않습니다. 외부 fixture가 IPv4/IPv6 classify 증가량, 원래
port, 애플리케이션·프로세스 식별자, 동적 정책 제거, 사용할 수 없는 callout의
fail-closed 동작과 이때 origin 접속이 0인지 검증합니다.

## 재사용 가능한 owning server와 connection adapter

제품 실행 파일은 `ntl::net::http3::msquic_backend::runtime`,
`configuration`, `server`를 사용합니다. server가 listener, 수락된 connection,
sink callback과 그 native MsQuic 상태를 소유합니다. runtime 또는 configuration
facade를 먼저 닫아도 이미 수락된 connection은 무효화되지 않습니다.
`server::close()`는 멱등적으로 새 accept를 거부하고 추적되는 정리 작업을
예약하므로, 애플리케이션이 detached worker나 native handle 파괴 순서를 관리할
필요가 없습니다. 이후 코드가 종료 완료를 확인해야 할 때만
`server::drain()`을 사용합니다. 이 service는 최종 검증 수치를 게시하기 전에
그 확인이 필요합니다.

listener 경로에서 프로토콜에 특화된 조립은 sink factory뿐입니다.

```cpp
server.open(
    runtime,
    [configuration](const auto &) { return ntl::ok(configuration); },
    [origin, policy](auto transport, const auto &peer)
        -> ntl::result<std::shared_ptr<ntl::net::quic::backend_sink>> {
      ntl::net::http::inspection_session_metadata session;
      session.tls.server_name = std::string(peer.server_name);
      session.tls.alpn = "h3";
      auto proxy = ntl::net::http3::proxy_connection::create(
          std::move(transport), origin, policy, std::move(session));
      if (!proxy)
        return ntl::unexpected(proxy.status());
      return ntl::ok(std::static_pointer_cast<
                     ntl::net::quic::backend_sink>(*proxy));
    });
```

acceptance 트래픽을 만들고 판정하는 raw controlled MsQuic peer는
`test/wfp/runtime/fixtures/user/http3-inspection` 아래의 테스트 전용 코드입니다.

ordinary service는 `ntl::net::http3::proxy_connection`을 사용합니다. 제품
코드는 `ntl::net::http::inspection_policy`, origin transport와 선택적
telemetry만 제공하며,
adapter가 SETTINGS/control stream, 크기가 제한된 static/dynamic QPACK,
request/trailer 조립, content coding, response framing, GOAWAY와 WebTransport
session routing을 담당합니다. 요청·응답 정책은 HTTP/1.1·HTTP/2 adapter와
동일하게 transform 이후의 semantic message를 `headers`, `body_chunk`,
`message_complete` 순서로 봅니다.

core origin 계약은 `async_origin_transport`입니다. 각 request stream을
독립적으로 submit하므로 느린 origin 하나가 다른 stream을 막지 않으며 응답은
요청 순서와 다르게 완료되어도 됩니다. backend callback과 completion callback은
connection 내부에서 직렬화됩니다. reset, drain, stop, close는 대기 중인
exchange를 취소하고, 경합으로 늦게 도착하거나 중복된 completion은 무시합니다.
completion은 해당 request보다 늦게 끝날 수 있습니다. owning connection이
backend와 origin 상태를 보유하고, `close()`가 새 작업을 거부한 뒤 callback과
child를 drain하므로 호출자가 파괴 순서를 맞출 필요가 없습니다.

`immediate_origin_transport_adapter`는 크기가 제한되고 결과가 결정적인
fixture에서 동기 origin을 감쌉니다. origin 호출이 끝날 때까지 submit callback을
막으므로, HTTP/3 stream 간 동시성을 유지해야 하는 제품 service는 비동기 origin
transport를 제공합니다.

## 빌드 결과

한 번의 configure/build로 driver, 제품 service와 acceptance 도구를 같은 구성
출력 디렉터리에 만듭니다.

```powershell
cmake -S . -B build -A x64 -DBUILD_TESTING=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

주요 target은 다음과 같습니다.

- `crtsys_wfp_http3_inspection_driver`
- `crtsys_wfp_http3_inspection_service`
- `crtsys_wfp_http3_inspection_acceptance`
- `crtsys_wfp_http3_replay_contracts`

replay contract는 설치 없이 실행할 수 있는 transport-independent 검사입니다.
service와 acceptance에는 아키텍처가 맞는 공식 `msquic.dll`이 필요합니다.
CMake header target은 이 DLL을 배포하지 않습니다.

## 제품 service 인터페이스

controller가 한 번에 하나의 제한된 정책과 시나리오를 선택하도록 인터페이스를
명시적으로 구성했습니다.

```text
crtsys_wfp_http3_inspection_service.exe
  <controlled-app.exe> <controlled-pid> <ipv4|ipv6>
  <ordinary|webtransport|webtransport-block|handshake>
  <normal|direct|unavailable> <ipc-directory>
```

disposable VM에서 전체 acceptance를 실행하는 방법과 예상 marker는
[사용자 HTTP/3 fixture 문서](../../../../test/wfp/runtime/fixtures/user/http3-inspection/README.ko-KR.md)를
참조하십시오.

이 예제는 제어된 endpoint와 WFP gate입니다. 임의 원격 UDP/443 흐름의 투명
가로채기를 주장하지 않습니다. ECH, certificate pinning, 임의 client identity
선택과 양방향 투명 UDP NAT는 별도의 제품 기능입니다.
