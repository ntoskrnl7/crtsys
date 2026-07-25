# WDK Minifilter Sample Coverage

This document maps the reusable Filter Manager mechanisms demonstrated by
`Windows-driver-samples/filesys/miniFilter` to NTL APIs and repository
verification.

Coverage does not mean that NTL copies a Microsoft sample or reproduces every
implementation detail. A mechanism is covered when it has a typed public API,
a compile-time contract, documentation, and an observable loaded-minifilter
test where runtime behavior matters. Filesystem or Windows-version support is
reported separately from NTL support.

## Coverage matrix

| Microsoft sample | Reusable mechanisms | NTL coverage | Primary evidence |
| --- | --- | --- | --- |
| `nullFilter` | registration, start, unload | Covered | `examples/minifilter/basic` and VM load/unload fixtures |
| `passThrough` | typed pre/post callbacks, operation-status callbacks | Covered | operation compile contracts and the general runtime fixture |
| `ctx` | instance, file, stream, stream-handle, and transaction contexts | Covered | context compile contracts plus multi-instance and transaction lifetime tests |
| `cancelSafe` | pending pre-I/O, cancellation, worker completion, teardown | Covered through NTL's pending-queue abstraction | `pending_pre_operation_queue` compile and runtime tests |
| `swapBuffers` | replacement buffers and MDLs for I/O and control paths | Covered | `examples/minifilter/swap-buffers` and the I/O-buffer runtime fixture |
| `scanner` | communication-port scanning and pended policy decisions | Covered | scanner runtime driver/app fixture |
| `avscan` | transaction-aware scanning and data-scan sections | Covered for the reusable lifecycle | scanner composition plus transaction and data-scan runtime tests |
| `change` | transacted dirty tracking with commit/rollback propagation | Covered | transaction enlistment, commit, rollback, and cleanup tests |
| `delete` | delete-on-close, disposition variants, races, stream deletion | Covered | delete runtime driver/app fixture |
| `minispy` | broad operation logging and bounded record delivery | Covered | `examples/minifilter/operation-log` and MiniSpy runtime tests |
| `MetadataManager` | per-volume metadata, locks, dismount, PnP, snapshots | Covered for the reusable volume lifecycle | `examples/minifilter/volume-metadata` and verifier-backed metadata tests |
| `NameChanger` | namespace grafting, name provider, enumeration, notification, query/set/FSCTL rewriting | Covered for the Microsoft-sample surface implemented by NTL | NameChanger runtime fixture on NTFS/ReFS with x64 and WOW64 clients |
| `simrep` | simulated reparse, fallback, destination and tunneled names | Covered | SimRep runtime driver/app fixture |
| `cdo` | legacy control device alongside a minifilter | Covered | `examples/minifilter/control-device` and CDO runtime tests |

The detailed runtime contracts, filesystem-specific results, and commands live
in the README beside each fixture under `test/flt/runtime`.

## Cross-cutting verification gates

The following gates apply where they are relevant to a row:

1. x64 and x86 compile with `/W4 /WX`.
2. Unsafe ownership transitions and raw callback substitutions have negative
   compile assertions.
3. The loaded x64 driver runs on a disposable Windows VM.
4. Fixed-layout user/kernel contracts run with an x86 app against the x64
   driver.
5. State that can outlive an operation callback is tested through cancellation,
   detach, disconnect, or unload.
6. Runtime tests make observable assertions; debug output alone is not treated
   as verification.
7. Filesystem-sensitive behavior is tested on NTFS and ReFS when the underlying
   filesystem implements the operation.

## Scope and limitations

- A covered row claims the reusable mechanism, not source-level equivalence
  with the Microsoft sample.
- TxF-dependent scenarios remain conditional on transaction support in the
  test environment.
- PnP query/cancel/surprise paths that require deterministic removable-device
  hardware remain compile-covered when no repeatable runtime harness exists.
- NameChanger keeps unsupported information classes and filesystem-rejected
  FSCTLs explicit. It does not claim that every filesystem accepts every
  operation.
- Behaviors listed by a Microsoft sample as future work are not counted as NTL
  coverage requirements.

Update this matrix only when the corresponding public API, compile contract, or
runtime evidence changes. Keep detailed counters, guest build numbers, and
procedural test results in the fixture-specific documentation rather than in
this repository-wide summary.
