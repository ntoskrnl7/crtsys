# NTL minifilter 통신 예제

[English](./README.md)

이 예제는 Filter Manager 통신을 파일 I/O 정책과 분리합니다. 드라이버가 NTL
형식화 communication port를 등록하고 앱이 다음 기능을 검증합니다.

- 동기 및 비동기 RPC 호출
- coroutine 기반 호출 wrapper
- 드라이버에서 애플리케이션으로 보내는 callback
- best-effort 및 reliable 알림
- 형식화된 stream
- 등록된 buffer token으로 교환하는 공유 메모리 ring 두 개

minifilter는 file-operation callback을 등록하지 않습니다. callback과 context의
기본 사용법은 sibling [`basic`](../basic) 예제를, 안전한 입력/출력 buffer 교체는
[`swap-buffers`](../swap-buffers) 예제를 참고하세요.

## 빌드

저장소 root에서 실행합니다.

```powershell
cmake -S examples\minifilter\communication `
      -B out\minifilter-communication-x64 -A x64
cmake --build out\minifilter-communication-x64 --config Debug
```

Visual Studio/WDK에서 직접 빌드하려면
`crtsys_minifilter_communication_sample_vs.sln`을 열거나 다음 명령을 실행하세요.

```powershell
msbuild crtsys_minifilter_communication_sample_vs.sln /restore `
        /p:Configuration=Debug /p:Platform=x64
```

CMake target 이름은 다음과 같습니다.

- `crtsys_minifilter_communication_sample`
- `crtsys_minifilter_communication_sample_app`

## 설치 및 실행

대상 장비의 요구 사항에 맞춰 드라이버를 test-signing한 다음 INF를 설치합니다.

```powershell
pnputil /add-driver `
  crtsys_minifilter_communication_sample.inf /install
fltmc load CrtSysMinifilterCommunicationSample
crtsys_minifilter_communication_sample_app.exe
fltmc unload CrtSysMinifilterCommunicationSample
```

communication port는 `\CrtSysMinifilterCommunicationSamplePort`입니다. INF의
altitude `370030.128`은 예제용이므로 실제 제품을 배포하기 전에 할당받은 altitude를
사용하세요.

API 모델은 [NTL minifilter 가이드](../../../docs/ntl/minifilter.ko-KR.md)를,
실패 경로 및 protocol coverage는
[`test/flt/runtime`](../../../test/flt/runtime)을 참고하세요.
