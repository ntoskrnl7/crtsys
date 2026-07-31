# NTL 기본 미니필터 예제

[English](./README.md)

예제 중 가장 작은 완전한 NTL 미니필터입니다. 형식화된 create 전·후 콜백,
정규화된 파일 이름 조회, stream context의 생성·조회·자동 해제, read/write/
cleanup 콜백과 `skip_paging_io` 같은 플래그를 보여줍니다.

`.tmp` 파일이 열리면 이름을 저장하고 non-paging write 횟수를 셉니다. 저장한
이름은 create 시점의 snapshot이므로 실제 정책에서는 이후 rename과 hard
link의 의미를 별도로 정의해야 합니다.

```powershell
cmake -S examples\minifilter\basic -B out\minifilter-basic-x64 -A x64
cmake --build out\minifilter-basic-x64 --config Debug
fltmc load CrtSysMinifilterBasicSample
crtsys_minifilter_basic_sample_app.exe
fltmc unload CrtSysMinifilterBasicSample
```

INF altitude `370030.127`은 개발 전용입니다.
