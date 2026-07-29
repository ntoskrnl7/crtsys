# NTL 미니필터 볼륨 메타데이터 예제

[English](./README.md)

WDK `MetadataManager` 예제의 수명 주기를 형식화된 NTL 콜백과 소유권으로
표현합니다. NTFS/ReFS instance에만 연결해 metadata 파일을 열고, 볼륨 lock,
dismount 또는 query-remove 때 참조를 해제합니다. 실패한 작업, unlock 또는
cancel-remove 뒤에는 적절한 instance에서 다시 엽니다.

`volume_metadata_instance_context`는 내부 `ERESOURCE`와 `KEVENT` 때문에
nonpaged pool만 사용합니다.

```powershell
cmake -S examples\minifilter\volume-metadata -B out\minifilter-volume-metadata-x64 -A x64
cmake --build out\minifilter-volume-metadata-x64 --config Debug
fltmc load CrtSysMinifilterVolumeMetadataSample
crtsys_minifilter_volume_metadata_sample_app.exe --lock E:
fltmc unload CrtSysMinifilterVolumeMetadataSample
```

`--lock`에는 폐기 가능한 데이터 볼륨만 지정하십시오. 앱 자체는 볼륨을
dismount하지 않습니다. altitude `370030.132`는 개발 전용입니다.
