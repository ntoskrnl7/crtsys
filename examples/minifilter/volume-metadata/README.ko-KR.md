# NTL minifilter volume-metadata 예제

[English](./README.md)

이 예제는 WDK `MetadataManager` 예제에서 재사용할 수 있는 lifecycle을 형식화된
NTL callback과 소유권으로 표현합니다.

| WDK 기능 | NTL 표현 |
| --- | --- |
| 비페이지 인스턴스 컨텍스트 | `volume_metadata_instance_context<T>` |
| metadata handle 및 file-object 소유권 | `volume_metadata_file` |
| volume-open 감지 | `file::is_volume_open()` |
| lock/unlock/dismount 분류 | `volume_control_request` |
| PnP 분류 | `pnp_request` |
| thread 간 snapshot 배타 제어 | `volume_metadata_snapshot_hold` |

## Lifecycle

1. instance setup은 NTFS와 ReFS에만 연결하고 metadata 파일을 엽니다.
2. volume lock, dismount 또는 query-remove가 발생하면 metadata 참조를 해제합니다.
3. lock이나 dismount가 실패하면 같은 volume handle에서 다시 엽니다.
4. unlock 또는 cancel-remove 시 다시 열기를 시도합니다. Windows가 volume
   instance를 교체했다면 새 instance setup이 대신 소유자가 됩니다.
5. teardown과 shutdown에서 남은 참조를 모두 닫습니다.

`volume_metadata_instance_context`에서는 paged pool을 선택할 수 없습니다. 내부
`ERESOURCE`와 `KEVENT` storage가 resident 상태를 유지해야 하기 때문입니다. 큰
pageable 제품 상태는 이 context를 paged pool로 바꾸지 말고 별도로 소유해야 합니다.

## 빌드 및 실행

```powershell
cmake -S examples\minifilter\volume-metadata `
      -B out\minifilter-volume-metadata-x64 -A x64
cmake --build out\minifilter-volume-metadata-x64 --config Debug
```

Visual Studio/WDK에서 직접 빌드하려면
`crtsys_minifilter_volume_metadata_sample_vs.sln`을 열거나 다음 명령을 실행하세요.

```powershell
msbuild crtsys_minifilter_volume_metadata_sample_vs.sln /restore `
        /p:Configuration=Debug /p:Platform=x64
```

폐기 가능한 VM에 test-signed 드라이버를 설치한 다음 실행합니다.

```powershell
fltmc load CrtSysMinifilterVolumeMetadataSample
crtsys_minifilter_volume_metadata_sample_app.exe --lock E:
fltmc unload CrtSysMinifilterVolumeMetadataSample
```

앱에는 명시적인 `--lock` 인자가 필요하며 폐기 가능한 data volume에만 사용해야
합니다. 앱 자체는 volume을 dismount하지 않습니다. runtime fixture는
[`test/flt/runtime/metadata_*`](../../../test/flt/runtime)에서 destructive
dismount/remount, snapshot, NTFS/ReFS 및 Driver Verifier coverage를 추가합니다.

altitude `370030.132`는 개발 전용입니다.
