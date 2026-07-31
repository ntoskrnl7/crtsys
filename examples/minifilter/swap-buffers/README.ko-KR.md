# NTL 미니필터 버퍼 교체 예제

[English](./README.md)

`ntl::flt::try_swap_io_buffers`로 쓰기 입력과 읽기 출력을 안전하게 교체합니다.
`.ntlxor` 파일만 XOR 변환하고 `.tmp` 파일은 그대로 통과시킵니다. pre-write는
복사한 replacement만 수정하고, post-read는 `IoStatus.Information`이 가리키는
유효 바이트만 복원해 원래 출력으로 복사합니다. Fast I/O와 APC-level 경로는
안전한 IRP/PASSIVE 경로로 넘깁니다.

```powershell
cmake -S examples\minifilter\swap-buffers -B out\minifilter-swap-buffers-x64 -A x64
cmake --build out\minifilter-swap-buffers-x64 --config Debug
fltmc load CrtSysMinifilterSwapBuffersSample
crtsys_minifilter_swap_buffers_sample_app.exe
fltmc unload CrtSysMinifilterSwapBuffersSample
```

XOR은 암호화가 아닙니다. 이 예제에는 키 관리, 인증, nonce, paging/memory
mapped I/O, rename 정책과 crash recovery가 없습니다. 실제 데이터 보호
제품으로 사용해서는 안 됩니다.
