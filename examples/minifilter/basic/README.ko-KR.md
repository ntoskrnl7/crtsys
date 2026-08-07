# NTL 기본 minifilter 예제

[English](./README.md)

예제 모음에서 가장 작은 완전한 NTL minifilter입니다. 다음 기능을 보여줍니다.

- 형식화된 pre-create 및 post-create callback
- 정규화된 파일 이름 조회와 parsing
- stream context 생성·조회·자동 해제
- 형식화된 read, write 및 cleanup callback
- `skip_paging_io` 같은 operation flag

예제는 `.tmp` 파일이 열리는 것을 관찰하고 create가 성공한 뒤 정규화된 이름을
저장하며 stream context에서 non-paging write 횟수를 셉니다. 앱은 임시 파일 하나를
만들고 읽고 이름을 바꾼 뒤 제거합니다.

cache된 이름은 의도적으로 post-create 시점의 snapshot입니다. 실제 정책 코드는
snapshot이 현재 stream 이름으로 계속 유지된다고 가정하지 말고, 이후 rename이나
hard-link operation의 의미를 정의해야 합니다.

## 빌드

저장소 root에서 실행합니다.

```powershell
cmake -S examples\minifilter\basic -B out\minifilter-basic-x64 -A x64
cmake --build out\minifilter-basic-x64 --config Debug
```

target 이름은 `crtsys_minifilter_basic_sample`과
`crtsys_minifilter_basic_sample_app`입니다.

Visual Studio/WDK에서 직접 빌드하려면 `crtsys_minifilter_basic_sample_vs.sln`을
열거나 다음 명령을 실행하세요.

```powershell
msbuild crtsys_minifilter_basic_sample_vs.sln /restore `
        /p:Configuration=Debug /p:Platform=x64
```

INF를 test-signing하고 설치한 다음 실행합니다.

```powershell
fltmc load CrtSysMinifilterBasicSample
crtsys_minifilter_basic_sample_app.exe
fltmc unload CrtSysMinifilterBasicSample
```

INF altitude `370030.127`은 개발 전용입니다. 실제 minifilter를 배포하려면
Microsoft가 할당한 고유 altitude를 받아야 합니다.
