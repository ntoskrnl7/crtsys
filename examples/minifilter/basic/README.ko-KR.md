# NTL 기본 minifilter 예제

[English](./README.md)

이 예제 모음에서 가장 작은 완전한 NTL minifilter입니다. 다음을 보여 줍니다.

- 타입이 지정된 pre-create 및 post-create 콜백
- 정규화된 파일 이름 조회 및 파싱
- stream context의 생성·조회·자동 해제
- 타입이 지정된 read, write, cleanup 콜백
- `skip_paging_io` 같은 작업 플래그

이 예제는 `.tmp` 파일 열기를 감시하고, create가 성공한 뒤 정규화된 이름을
캡처하며, stream context에서 non-paging write 횟수를 셉니다. 앱은 임시 파일 하나를
생성, 읽기, 이름 변경, 삭제합니다.

캐시한 이름은 의도적으로 post-create 시점의 스냅샷입니다. 실제 정책 코드는 이
스냅샷이 계속 stream의 현재 이름이라고 가정하지 말고, 이후의 이름 변경이나
hard-link 작업이 무엇을 뜻하는지 정의해야 합니다.

## 빌드

저장소 루트에서 실행합니다.

```powershell
cmake -S examples\minifilter\basic -B out\minifilter-basic-x64 -A x64
cmake --build out\minifilter-basic-x64 --config Debug
```

대상은 `crtsys_minifilter_basic_sample` 및
`crtsys_minifilter_basic_sample_app`입니다.

Visual Studio/WDK에서 직접 빌드하려면
`crtsys_minifilter_basic_sample_vs.sln`을 열거나 다음을 실행합니다.

```powershell
msbuild crtsys_minifilter_basic_sample_vs.sln /restore `
        /p:Configuration=Debug /p:Platform=x64
```

테스트 서명 후 INF를 설치한 다음 실행합니다.

```powershell
fltmc load CrtSysMinifilterBasicSample
crtsys_minifilter_basic_sample_app.exe
fltmc unload CrtSysMinifilterBasicSample
```

INF 고도 `370030.127`은 개발용입니다. 실제 minifilter를 배포하기 전에 Microsoft가
할당한 고유 고도를 받으세요.
