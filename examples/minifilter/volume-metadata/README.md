# NTL minifilter volume-metadata sample

This sample expresses the reusable lifecycle from the WDK
`MetadataManager` sample with typed NTL callbacks and ownership.

| WDK mechanism | NTL expression |
| --- | --- |
| nonpaged instance context | `volume_metadata_instance_context<T>` |
| metadata handle and file-object ownership | `volume_metadata_file` |
| volume-open detection | `file::is_volume_open()` |
| lock/unlock/dismount classification | `volume_control_request` |
| PnP classification | `pnp_request` |
| cross-thread snapshot exclusion | `volume_metadata_snapshot_hold` |

## Lifecycle

1. Instance setup attaches only to NTFS and ReFS and opens a metadata file.
2. A volume lock, dismount, or query-remove releases the metadata references.
3. Failed locks and dismounts reopen them on the same volume handle.
4. Unlock or cancel-remove attempts a reopen. If Windows replaced the volume
   instance, the new instance setup becomes the owner instead.
5. Teardown and shutdown close all remaining references.

`volume_metadata_instance_context` cannot select paged pool. This is
intentional: the embedded `ERESOURCE` and `KEVENT` storage must remain
resident. A large pageable product state should be owned separately rather
than changing this context to paged pool.

## Build and run

```powershell
cmake -S examples\minifilter\volume-metadata `
      -B out\minifilter-volume-metadata-x64 -A x64
cmake --build out\minifilter-volume-metadata-x64 --config Debug
```

For a direct Visual Studio/WDK build, open
`crtsys_minifilter_volume_metadata_sample_vs.sln`, or run:

```powershell
msbuild crtsys_minifilter_volume_metadata_sample_vs.sln /restore `
        /p:Configuration=Debug /p:Platform=x64
```

After installing the test-signed driver in a disposable VM:

```powershell
fltmc load CrtSysMinifilterVolumeMetadataSample
crtsys_minifilter_volume_metadata_sample_app.exe --lock E:
fltmc unload CrtSysMinifilterVolumeMetadataSample
```

The app requires an explicit `--lock` argument and must only be used against a
disposable data volume. It does not dismount the volume. The runtime fixture
adds destructive dismount/remount, snapshot, NTFS/ReFS, and Driver Verifier
coverage under [`test/flt/runtime/metadata_*`](../../../test/flt/runtime).

The altitude `370030.132` is development-only.
