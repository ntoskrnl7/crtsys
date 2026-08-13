# NTL minifilter 통신 예제

[English](./README.md)

이 예제는 Filter Manager 통신을 파일 I/O 정책에서 분리합니다. 드라이버가 타입이
지정된 NTL 통신 포트를 등록하고, 앱은 다음을 실행합니다.

- 동기 및 비동기 RPC 호출
- 코루틴 기반 호출 래퍼
- 드라이버에서 앱으로 가는 콜백
- best-effort 및 신뢰성 알림
- 타입이 지정된 스트림
- 등록한 버퍼 토큰으로 교환하는 공유 메모리 ring 두 개

이 minifilter는 파일 작업 콜백을 등록하지 않습니다. 콜백과 context의 기초는 형제
[`basic`](../basic) 예제를, 안전한 입력/출력 버퍼 교체는
[`swap-buffers`](../swap-buffers) 예제를 참고하세요.

## 빌드

저장소 루트에서 실행합니다.

```powershell
cmake -S examples\minifilter\communication `
      -B out\minifilter-communication-x64 -A x64
cmake --build out\minifilter-communication-x64 --config Debug
```

Visual Studio/WDK에서 직접 빌드하려면
`crtsys_minifilter_communication_sample_vs.sln`을 열거나 다음을 실행합니다.

```powershell
msbuild crtsys_minifilter_communication_sample_vs.sln /restore `
        /p:Configuration=Debug /p:Platform=x64
```

CMake 대상 이름은 다음과 같습니다.

- `crtsys_minifilter_communication_sample`
- `crtsys_minifilter_communication_sample_app`

## 설치 및 실행

대상 컴퓨터가 요구하는 방식으로 드라이버를 테스트 서명한 뒤 INF를 설치합니다.

```powershell
pnputil /add-driver `
  crtsys_minifilter_communication_sample.inf /install
fltmc load CrtSysMinifilterCommunicationSample
crtsys_minifilter_communication_sample_app.exe
fltmc unload CrtSysMinifilterCommunicationSample
```

통신 포트는 `\CrtSysMinifilterCommunicationSamplePort`입니다. INF는 예제 고도
`370030.128`을 사용하므로, 실제 제품을 배포하기 전에는 할당받은 고도를 선택하세요.

API 모델은 [NTL minifilter 가이드](../../../docs/ntl/minifilter.ko-KR.md), 실패 경로와
프로토콜 범위는 [`test/flt/runtime`](../../../test/flt/runtime)를 참고하세요.
