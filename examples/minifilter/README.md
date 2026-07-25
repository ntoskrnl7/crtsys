# NTL minifilter samples

The catalog has two deliberate layers:

- **examples** keep one driver idea readable from entry point to app; and
- **runtime fixtures** retain the large compatibility, failure, filesystem,
  WOW64, and Driver Verifier matrices needed to document WDK-sample coverage.

The small examples are complete for the behavior they describe. They are not
silently presented as production versions of the much larger Microsoft
samples.

The repository-wide mechanism and verification mapping is maintained in the
[WDK minifilter sample coverage matrix](../../test/flt/WDK-SAMPLE-COVERAGE.md).

| Sample | WDK counterpart | Primary lesson |
| --- | --- | --- |
| [`basic`](./basic) | NullFilter / PassThrough foundation | typed create/read/write/cleanup callbacks, normalized names, stream contexts, and registration lifetime |
| [`control-device`](./control-device) | CDO | a legacy control device owned by the minifilter lifecycle, typed IOCTL dispatch, and unload veto |
| [`communication`](./communication) | Scanner/MiniSpy communication foundation | Filter Manager ports, typed RPC, callbacks, notifications, streams, and registered shared-memory rings |
| [`operation-log`](./operation-log) | MiniSpy | typed I/O callbacks, per-open state, a bounded record queue, and typed user-mode draining |
| [`swap-buffers`](./swap-buffers) | SwapBuffers | safe pre-write input replacement and post-read output transformation/copy-back for `.ntlxor` files |
| [`volume-metadata`](./volume-metadata) | MetadataManager | per-volume metadata ownership across lock, unlock, snapshot, PnP, shutdown, and teardown paths |

The recommended first pass is `basic`, `control-device`, `communication`,
`operation-log`, `swap-buffers`, then `volume-metadata`.

## Where the full WDK-style implementations live

| Microsoft sample family | Readable entry point | Exhaustive implementation/proof |
| --- | --- | --- |
| CDO | [`control-device`](./control-device) | [`CDO-README.md`](../../test/flt/runtime/CDO-README.md) |
| MiniSpy | [`operation-log`](./operation-log) | [`test/flt/runtime`](../../test/flt/runtime) |
| SwapBuffers | [`swap-buffers`](./swap-buffers) | [`IO-BUFFER-README.md`](../../test/flt/runtime/IO-BUFFER-README.md) |
| MetadataManager | [`volume-metadata`](./volume-metadata) | [`METADATA-README.md`](../../test/flt/runtime/METADATA-README.md) |
| Scanner / AvScan | communication and buffer examples above | [`SCANNER-README.md`](../../test/flt/runtime/SCANNER-README.md) |
| SimRep | typed name APIs in the main guide | [`SIMREP-README.md`](../../test/flt/runtime/SIMREP-README.md) |
| NameChanger | typed name APIs in the main guide | [`NAME-CHANGER-README.md`](../../test/flt/runtime/NAME-CHANGER-README.md) |
| Delete | typed set-information APIs in the main guide | [`DELETE-README.md`](../../test/flt/runtime/DELETE-README.md) |

NameChanger intentionally remains a coverage fixture instead of a misleading
200-line example. Its contract is the combination of create redirection, name
generation and normalization, directory enumeration, query-information,
rename/hard-link destinations, notifications, and name-bearing FSCTL results.
Removing most of those mechanisms would make the code shorter while no longer
demonstrating NameChanger semantics. The linked fixture keeps those
responsibilities split into source files and verifies all of them on NTFS and
ReFS.

Every sample has its own CMake project, hand-written Visual Studio `.sln` and
driver/app `.vcxproj` files, driver, application, INF, service name, instance
name, and development altitude. The altitude values are examples only. A
shipping minifilter needs a Microsoft-assigned altitude.
