# MetadataManager Runtime Fixture

This driver/app pair exercises the reusable lifecycle from Microsoft's
MetadataManager minifilter with typed NTL APIs.

Each NTFS/ReFS instance owns a `volume_metadata_file` in a nonpaged
`volume_metadata_instance_context` and opens
`\System Volume Information\CrtSysMetadataRuntime.md`. Typed create, cleanup,
file-system-control, device-control, PnP, shutdown, and instance-teardown
callbacks coordinate that file.

The specialized context intentionally cannot select paged pool: the embedded
`ERESOURCE` and `KEVENT` synchronization storage must remain resident.

The disposable ReFS E: VM test verifies:

- implicit volume-lock detection, metadata release, and cleanup reopen;
- snapshot pre/post update exclusion with a move-only cross-thread hold;
- explicit `FSCTL_LOCK_VOLUME` release before the file system sees the lock;
- successful unlock with the old instance returning
  `STATUS_INVALID_DEVICE_OBJECT_PARAMETER`;
- drive access mounting a replacement instance and reopening metadata there;
- successful `FSCTL_DISMOUNT_VOLUME`, explicit filter detach, instance
  teardown, remount, and a second metadata open;
- clean minifilter unload after all contexts and handles are released.

The observable result is:

```text
metadata_manager=PASS filesystem=ReFS implicit=1 explicit_lock=1 snapshot=1
dismount_path=success_detach_remount remount_opens=2
```

This scenario passes with Standard Driver Verifier flags `0x1209BB` targeting
the metadata driver. The complete fixed runner log is written under
`artifacts/flt-metadata-win11-25h2/refs-verifier-fixed.log`.
