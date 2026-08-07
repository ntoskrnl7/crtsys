# NTL 미니필터 I/O Buffer 런타임 테스트

`crtsys_flt_io_buffer_runtime_test` target은 `ntl::flt::swapped_io_buffers`의 end-to-end fixture입니다.

pre-write에서 격리된 replacement를 설치하고, Filter Manager communication port에 연결한 process에 이를 매핑한 뒤 user app에게 application의 원래 buffer를 바꾸지 않고 XOR 변환하도록 요청합니다. pre-read는 replacement output을 설치합니다. post-read는 같은 service에 `IoStatus.Information` 범위만 매핑한 뒤 매핑을 닫고 변환된 byte를 다시 복사합니다. 대상 IRP pre 작업과 성공한 read post 작업은 모든 요청에서 의도적으로 PASSIVE worker에 보류됩니다. 따라서 통과한 실행은 두 deferred completion 경로를 모두 증명합니다. write worker는 작업을 `pending_pre_operation_queue`로 넘기고 service가 매핑을 닫은 뒤에만 재개합니다. read worker는 post-read 소유권을 `pending_post_operation_registry`로 넘기고 같은 close 뒤에만 응답합니다. 따라서 성공한 파일 I/O는 일반적인 pending-registry resume/reply 경로도 증명합니다. 분리된 각 service worker는 별도의 strong instance-context 참조를 소유하고 `queue_instance_work_item`으로 실행됩니다. Filter Manager는 worker routine이 실제로 반환할 때까지 filter rundown을 유지합니다. instance teardown은 늦은 worker 상태를 무효화하거나 callback 코드를 unload하지 않고 어느 registry든 취소할 수 있습니다. Fast I/O는 발생 시 허용하지 않고 IRP 경로에서 다시 시도합니다.

app은 변환의 양쪽을 검증합니다.

1. filter를 load한 상태에서 write 후 read하면 원래 plaintext가 반환됩니다.
2. filter를 unload하면 같은 파일에 정확한 XOR ciphertext가 들어 있습니다.
3. 다시 load한 뒤 ciphertext를 읽으면 원래 plaintext가 다시 반환됩니다.
4. EOF에서 길이가 0이 아닌 read는 전송 byte 0으로 성공하며, 0-byte copy-back이 원래 destination을 역참조하지 않음을 증명합니다.
5. I/O 완료 뒤 매핑 주소는 더 이상 유효한 VAD가 아니며, service disconnect 뒤의 작업은 오래된 process를 사용하거나 변환을 조용히 우회하지 않고 실패합니다.
6. service timeout을 넘겨 응답을 보류하는 user handler는 보류된 pre-WRITE와 post-READ를 모두 취소합니다. 의도적으로 늦은 handler를 해제하기 전에 VAD가 무효여야 합니다. 취소된 WRITE는 별도의 payload를 사용하며 하위 파일을 바꾸면 안 됩니다.
7. 각 방향이 보류된 상태에서 communication client를 disconnect하면 I/O를 취소하고 pending registry를 drain하며 VAD를 무효화하고 service를 정상적으로 다시 연결할 수 있게 합니다. 취소된 WRITE 역시 하위 파일을 바꾸면 안 됩니다.
8. 각 방향이 보류된 상태에서 filter를 unload하면 I/O를 취소하고 VAD를 무효화하며 Filter Manager work-item rundown을 기다리고 이후 load/read cycle을 허용합니다.

기존 미니필터 runtime project의 일부로 빌드합니다.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File scripts\ci\Build-CrtSys.ps1 `
  -Project flt-runtime -Architecture x64 `
  -PlatformToolset v145 -Configuration Debug
```

폐기 가능하고 test signing이 활성화된 VM에서만 설치·테스트하십시오. build는 driver output 옆에 `crtsys_flt_io_buffer_runtime_test.inf`를 복사합니다. WDK test-signing 절차에 따라 test certificate를 만들고 신뢰하며 catalog package를 생성·서명하고 staged INF를 설치하십시오. `crtsys_flt_io_buffer_runtime_test.sys`에 Driver Verifier를 활성화하고 Verifier가 요구하면 reboot한 뒤 실행합니다.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File scripts\ci\Run-FltIoBufferRuntimeTests.ps1 `
  -AppPath test\flt\runtime\build_x64_v145\Debug\crtsys_flt_io_buffer_runtime_test_app.exe `
  -RequireVerifier
```

runner는 시작 전에 elevation, test signing, 설치된 service, 활성 Driver Verifier target을 확인합니다. app은 설치된 filter를 unload/reload하고 첫 plaintext, ciphertext, copy-back, timeout, disconnect, teardown, VAD 수명, load, unload 불일치에서 실패합니다. runner는 app이 테스트 filter를 load한 채 남기지 않았는지도 확인합니다.
