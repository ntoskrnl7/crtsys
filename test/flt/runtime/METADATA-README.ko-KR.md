# MetadataManager 런타임 fixture

이 driver/app 쌍은 타입이 지정된 NTL API로 Microsoft MetadataManager 미니필터의 재사용 가능한 수명 주기를 검증합니다.

각 NTFS/ReFS instance는 nonpaged `volume_metadata_instance_context` 안에 `volume_metadata_file`을 소유하고 `\System Volume Information\CrtSysMetadataRuntime.md`를 엽니다. 타입이 지정된 create, cleanup, file-system-control, device-control, PnP, shutdown, instance-teardown callback이 해당 파일을 조정합니다.

전문화된 context는 의도적으로 paged pool을 선택할 수 없습니다. 포함된 `ERESOURCE`, `KEVENT` 동기화 저장소가 상주해야 하기 때문입니다.

폐기 가능한 NTFS/ReFS test volume 실행은 다음을 검증합니다.

- 암시적 volume lock 감지, metadata 해제, cleanup 중 reopen
- move-only cross-thread hold를 사용하는 snapshot pre/post update 배제
- file system이 lock을 보기 전 명시적 `FSCTL_LOCK_VOLUME` 해제
- 기존 instance가 `STATUS_INVALID_DEVICE_OBJECT_PARAMETER`를 반환하는 성공적인 unlock
- 대체 instance를 mount하고 그곳에서 metadata를 다시 여는 drive 접근
- 성공적인 `FSCTL_DISMOUNT_VOLUME`, 명시적 filter detach, instance teardown, remount, 두 번째 metadata open
- 모든 context와 handle 해제 뒤의 정상 미니필터 unload

관찰 결과:

```text
metadata_manager=PASS filesystem=ReFS implicit=1 explicit_lock=1 snapshot=1
dismount_path=success_detach_remount remount_opens=2
```

metadata driver를 대상으로 Standard Driver Verifier flag `0x1209BB`로 이 시나리오를 검증하십시오. 생성된 log는 테스트 환경에 구성된 artifact 대상에 보존하십시오.
