# NTL 미니필터 통신 예제

[English](./README.md)

파일 I/O 정책과 Filter Manager 통신을 분리한 예제입니다. 동기·비동기 RPC,
coroutine 호출, 드라이버에서 앱으로의 callback, best-effort/reliable 알림,
형식화된 stream과 등록된 buffer token으로 교환하는 공유 메모리 ring 두 개를
검증합니다. 파일 작업 콜백은 등록하지 않습니다.

```powershell
cmake -S examples\minifilter\communication -B out\minifilter-communication-x64 -A x64
cmake --build out\minifilter-communication-x64 --config Debug
fltmc load CrtSysMinifilterCommunicationSample
crtsys_minifilter_communication_sample_app.exe
fltmc unload CrtSysMinifilterCommunicationSample
```

통신 포트는 `\CrtSysMinifilterCommunicationSamplePort`이고 altitude
`370030.128`은 개발 전용입니다. API 모델은
[NTL 미니필터 가이드](../../../docs/ntl/minifilter.md)를 참고하십시오.
