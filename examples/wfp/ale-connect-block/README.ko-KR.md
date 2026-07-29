# WFP ALE 연결 차단 샘플

[English](./README.md)

이 샘플은 `127.0.0.1`의 특정 TCP 포트로 나가는 연결을 잠시 차단합니다.
WFP 규칙을 제거한 뒤에는 똑같은 연결이 다시 성공합니다.

한 문장으로 줄이면 다음과 같습니다.

> 사용자 앱이 “이 포트 연결은 드라이버에게 물어봐라”라는 규칙을
> 설치하고, 커널 드라이버가 `block`으로 답하는 샘플입니다.

## 구성 요소의 역할

| 구성 요소 | 하는 일 |
| --- | --- |
| `crtsys_wfp_ale_connect_block.sys` | WFP가 연결 허용 여부를 물어볼 수 있도록 커널 callout을 등록하고 `block`을 반환합니다. |
| `crtsys_wfp_ale_connect_block_app.exe` | 로컬 TCP 서버를 만들고, 임시 WFP 규칙을 설치하고, 차단과 복구를 직접 확인합니다. |
| Windows Filtering Platform | 앱이 설치한 filter와 일치하는 연결을 찾아 커널 드라이버의 callout을 호출합니다. |

앱과 드라이버는 같은 callout GUID를 공유합니다. 앱은 그 GUID를 가리키는
정책을 설치하고, 드라이버는 그 GUID에 해당하는 실제 콜백을 등록합니다.

## 실행할 때 일어나는 일

```text
1. 앱이 127.0.0.1:<port>에 TCP 서버를 엽니다.
                         │
2. 앱이 dynamic WFP 세션을 엽니다.
                         │
3. provider → sublayer → callout → filter를 한 트랜잭션으로 설치합니다.
                         │
4. 앱이 같은 포트로 connect()를 호출합니다.
                         │
5. filter가 일치하여 WFP가 커널 드라이버를 호출합니다.
                         │
6. 드라이버가 block을 반환합니다.
                         │
7. connect()가 WSAEACCES(10013)로 실패합니다.
                         │
8. 앱이 dynamic 세션을 닫아 규칙 네 개를 모두 제거합니다.
                         │
9. 같은 connect()를 다시 호출하면 성공합니다.
```

여기서 `ALE_AUTH_CONNECT_V4`는 “IPv4 연결을 밖으로 시작해도 되는지
승인하는 지점”입니다. 패킷이 전송된 다음에 잡는 것이 아니라,
`connect()`가 성립하기 전에 허용 여부를 결정합니다.

## 왜 로컬 TCP 서버를 만드는가

외부 인터넷이나 다른 컴퓨터 상태에 의존하지 않기 위해서입니다.
항상 같은 PC 안에서 다음 두 결과를 구분할 수 있습니다.

- WFP 규칙이 있을 때: `10013`으로 차단
- WFP 규칙이 사라진 뒤: 연결 성공

따라서 네트워크 단절이나 방화벽 같은 외부 원인을 WFP 차단 성공으로
잘못 판단하지 않습니다.

## 이 샘플이 증명하는 것

- 사용자 모드 정책과 커널 callout이 실제로 연결됩니다.
- 선택한 연결이 드라이버의 결정 때문에 차단됩니다.
- dynamic session을 닫으면 정책이 자동으로 제거됩니다.
- 드라이버를 정지하고 언로드할 수 있습니다.

## 다음 예제 선택

| 목표 | 예제 |
| --- | --- |
| UDP 패킷 목적지 변경과 재주입 | [`datagram-proxy`](../datagram-proxy/README.ko-KR.md) |
| 사용자 모드 결정을 기다리는 ALE 검사 | [`async-inspection`](../async-inspection/README.ko-KR.md) |
| 선택한 TCP 연결을 로컬 프록시로 전달 | [`connect-redirect`](../connect-redirect/README.ko-KR.md) |
| TCP stream의 토큰 검색과 변경 | [`stream-edit`](../stream-edit/README.ko-KR.md) |

## 실행 결과 읽기

앱은 다섯 단계를 화면에 순서대로 출력합니다. 마지막에 다음 문장이
나오면 성공입니다.

```text
NTL WFP ale-connect-block ok: blocked_error=10013, restored_connect=success
```

`blocked_error=10013`은 규칙이 있을 때 차단됐다는 뜻이고,
`restored_connect=success`는 규칙 제거 후 연결이 정상 복구됐다는
뜻입니다.

## 빌드와 수동 실행

드라이버를 먼저 로드한 뒤 관리자 권한으로 앱을 실행해야 합니다.

```powershell
cmake -S examples\wfp\ale-connect-block `
      -B artifacts\examples\wfp-ale-connect-block -A x64
cmake --build artifacts\examples\wfp-ale-connect-block --config Release

sc.exe create crtsys_wfp_ale_connect_block type= kernel start= demand `
  binPath= C:\path\to\crtsys_wfp_ale_connect_block.sys
sc.exe start crtsys_wfp_ale_connect_block
.\crtsys_wfp_ale_connect_block_app.exe
sc.exe stop crtsys_wfp_ale_connect_block
sc.exe delete crtsys_wfp_ale_connect_block
```

테스트 서명, 반복 실행, Driver Verifier 자동화는
[`test/wfp/runtime/ale-connect-block`](../../../test/wfp/runtime/ale-connect-block)
스크립트를 사용합니다.
