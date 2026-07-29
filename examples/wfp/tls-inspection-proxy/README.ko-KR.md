# WFP TLS 검사 프록시

[English](./README.md)

이 예제는 전송 계층과 브라우저 UI에 종속되지 않는 TLS 검사 기반을 보여줍니다.
선택한 outbound IPv4 TCP 연결을 작은 WFP callout이 로컬 프록시로 보내고,
사용자 모드 Schannel이 TLS를 종료한 뒤 bounded HTTP/1.1 평문을 검사합니다.
원래 목적지에는 별도로 인증서를 검증하는 두 번째 TLS 연결을 만듭니다.

기본 실행은 loopback origin을 사용하며 다음을 검증합니다.

1. 드라이버는 선택한 목적지 포트만 redirect합니다.
2. 사용자 모드는 원래 목적지와 WFP redirect record를 복구합니다.
3. bounded ClientHello probe는 암호문을 버리지 않고 SNI를 얻습니다.
4. SNI에 맞는 단기 호스트 인증서를 bounded cache에서 선택합니다.
5. `tls_framed_stream`이 완전한 bounded HTTP/1.1 메시지를 만듭니다.
6. `ntl::net::inspection`은 `ALLOW`를 허용하고 `BLOCKME` flow를 차단합니다.
7. 동적 WFP session이 닫히면 같은 연결이 다시 직접 전달됩니다.

메시지 경계는 검증된 `Content-Length` 또는 마지막 `chunked` framing에서
얻습니다. TLS, 인증서, HTTP framing, 평문 정책은 사용자 모드에 있고 커널은
선택한 연결이 반드시 프록시 경로를 거치게 하는 일만 담당합니다.

브라우저를 계속 실행하면서 임시 신뢰와 HTML 로그를 관리하는 흐름은
[`browser-https-inspection`](../browser-https-inspection/README.ko-KR.md)에
있습니다.

## 인증서 경계

결정적 테스트를 독립적으로 실행하기 위해 프로세스 내부에 임시 private test
CA를 만듭니다. 제어된 client는 `certificate_authority_policy`의 private chain
engine으로 발급된 leaf를 검증합니다. 앱은 Windows 신뢰 저장소를 변경하지
않으며, 발급한 leaf key와 임시 CA key container는 종료할 때 제거합니다.

NTL이 제품용 검사 root CA를 몰래 생성하거나 설치하지는 않습니다. 실제
제품은 관리자가 승인한 issuer를 주입하고, 신뢰 배포·키 보호와 회전·감사·
고지·우회 및 실패 정책을 명시적으로 정해야 합니다.

## 빌드와 실행

```powershell
cmake -S examples\wfp\tls-inspection-proxy `
      -B artifacts\examples\wfp-tls-inspection-proxy -A x64
cmake --build artifacts\examples\wfp-tls-inspection-proxy `
      --config Release
```

`crtsys_wfp_tls_inspection_proxy.sys`를 테스트 서명하여 로드한 뒤 관리자
PowerShell에서 실행합니다.

```powershell
.\crtsys_wfp_tls_inspection_proxy_app.exe
```

성공 표시는 `NTL WFP TLS inspection-proxy ok:`입니다.

## 제어된 실제 HTTPS 검증

브라우저를 실행하지 않고 호출자가 지정한 DNS 호스트 하나를 검사하는 선택적
경로도 있습니다.

```powershell
.\crtsys_wfp_tls_inspection_proxy_app.exe `
    --inspect-host $env:NTL_WFP_TEST_HOST
```

호출자가 조직에서 허용한 host를 선택해야 하며 예제에 특정 공개 사이트를
내장하지 않습니다. bounded DNS 결과를 하나씩 시도하고, 각 redirect filter는
이 실행 파일, 현재 IPv4 후보 하나, TCP, 443번 포트에만 적용됩니다.

원격 Schannel 연결은 Windows 시스템 chain과 host name을 정상적으로
검증합니다. 관리형 HTTPS issuer가 접근 가능한 revocation 정보를 제공하지
않는 환경은 `--allow-unavailable-revocation`을 명시하여 시험할 수 있습니다.
이 모드는 root를 제외한 chain의 revocation을 검사하되 정보 부재와 offline만
허용합니다. 신뢰되지 않은 chain, host name 불일치, 만료 및 실제 revoked
결과는 계속 실패합니다. 인터넷 의존 실행 도구는
[`test/wfp/runtime/https-live`](../../../test/wfp/runtime/https-live)에
있습니다.

## 프로토콜 경계

사용 가능한 SNI가 없으면 인증서를 추측하지 않고 실패합니다. 이 예제는 작고
결정적인 HTTP/1.1 기반을 의도합니다. 브라우저 예제는 여기에 IPv6, 협상된
WebSocket `permessage-deflate`, bounded gzip/deflate/Brotli decoder,
완전한 HTTP/2/HPACK multiplexed relay와 QUIC 우회 방지 정책을 더합니다.
별도 [`http3-inspection`](../http3-inspection/README.ko-KR.md) 예제는 복호화된
QUIC/QPACK provider 계약을 보여줍니다. 투명 HTTP/3 제품 경로는 여전히 QUIC
terminator와 동적 QPACK 구현을 제공해야 합니다. 확인된 ECH에서 inner
ClientHello를 복구하지 못한 경우, pinning 거부, 사용할 mutual-TLS identity가
없는 경우에는 명시적으로 fail closed 합니다. 확장 타입 `0xfe0d`가 보이는
것만으로는 GREASE ECH와 구분할 수 없으므로 실제 ECH라고 단정하지 않습니다.
