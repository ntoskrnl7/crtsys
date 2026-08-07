# NTL minifilter swap-buffers 예제

[English](./README.md)

이 예제는 `ntl::flt::try_swap_io_buffers`를 사용한 투명한 입력·출력 교체를
보여줍니다.

- pre-write는 호출자 입력을 replacement page로 복사하고 replacement만 XOR한 뒤
  하위 파일 시스템으로 보냅니다.
- pre-read는 replacement 출력 page를 설치합니다.
- post-read는 `IoStatus.Information`이 보고한 유효 byte만 XOR하고, 복원된 원본
  출력 buffer를 안정화한 뒤 결과를 copy-back합니다.
- 보호 대상 파일의 Fast I/O는 IRP I/O로 다시 시도합니다.
- APC-level callback은 Filter Manager의 PASSIVE-level work item으로 넘깁니다.

READ와 WRITE는 operation 형식으로 방향이 고정된 단방향 요청입니다. 정책 option에
방향 boolean이 없습니다. NTL이 WRITE에서는 입력, READ에서는 출력을 추론하므로
반대 selector를 실수로 표현할 수 없습니다. QUERY_EA나 IOCTL처럼 양방향을
합법적으로 노출할 수 있는 operation은 명시적 `ntl::flt::swap_buffer` selector를
요구합니다.

파일 선택에서는 일반 파일과 실패했거나 안전하지 않은 이름 조회를 구분합니다.
알 수 없는 이름은 IRP/PASSIVE 경로에서 다시 시도하고, 그래도 해석할 수 없으면
fail closed로 처리합니다. 이를 pass-through로 조용히 분류하지 않습니다.

확장자가 `.ntlxor`인 파일만 변환합니다. 동반 앱은 `.tmp` 파일을 사용해
pass-through 경로도 보여줍니다. Paging I/O는 의도적으로 건너뛰므로 이 코드는 API와
소유권 예제이지 완전한 투명 암호화 제품이 아닙니다.

예제는 중요한 소유권 규칙을 따릅니다. replacement MDL로 IOPB를 덮어쓰기 전에 원본
buffer에 `FltLockUserBuffer`를 호출하지 않습니다. read copy-back에서는 Filter
Manager가 원본 parameter를 복원한 뒤 post-operation 경로에서 원본 출력을
안정화합니다. 이 검증은 새 post-operation MDL을 만들지 않습니다. 기존 MDL은
borrow하고, MDL이 없는 user address는 참조된 requestor process에서 SEH로
보호하며 복사합니다.

구현은 execution context 판단에 `ntl::is_passive_level()`을 사용하며 내부 및
deferred completion 도우미에서 `ntl::status`를 반환합니다. native
`PFLT_CALLBACK_DATA`는 NTL이 operation-typed callback data를 재구성하는 얇은
adapter 세 곳에만 나타납니다. Filter Manager status는
`callback_data::set_io_status()`로 갱신하며 native `NTSTATUS` 변환은 NTL의 Filter
Manager trampoline 안에만 남습니다.

## 빌드

저장소 root에서 실행합니다.

```powershell
cmake -S examples\minifilter\swap-buffers `
      -B out\minifilter-swap-buffers-x64 -A x64
cmake --build out\minifilter-swap-buffers-x64 --config Debug
```

target 이름은 `crtsys_minifilter_swap_buffers_sample`과
`crtsys_minifilter_swap_buffers_sample_app`입니다.

Visual Studio/WDK에서 직접 빌드하려면
`crtsys_minifilter_swap_buffers_sample_vs.sln`을 열거나 다음 명령을 실행하세요.

```powershell
msbuild crtsys_minifilter_swap_buffers_sample_vs.sln /restore `
        /p:Configuration=Debug /p:Platform=x64
```

INF를 test-signing하고 설치한 다음 실행합니다.

```powershell
fltmc load CrtSysMinifilterSwapBuffersSample
crtsys_minifilter_swap_buffers_sample_app.exe
fltmc unload CrtSysMinifilterSwapBuffersSample
```

## 의도적인 제한 사항

XOR은 암호화가 아닙니다. 이 예제에는 key 관리, 인증, nonce, on-disk format,
memory-mapped I/O 지원, paging-I/O 정책, rename/hard-link 정책, crash recovery,
cached/noncached 경로 간 조정이 없습니다. 실제 데이터 보호에 적용하려면 이러한
사항을 먼저 설계해야 합니다.

INF altitude `370030.129`는 개발 전용입니다. 배포 전에 Microsoft가 할당한 고유
altitude를 받아야 합니다. 전체 API와 수명 계약은
[I/O buffer mapping 및 swapping](../../../docs/ntl/io-buffer-mapping.ko-KR.md)을
참고하세요.
