# NTL KMDF samples

Each directory is an independent, buildable KMDF project. The grouping keeps
software-only examples separate from hardware-specific templates:

| Directory | Purpose | Runtime validation |
| --- | --- | --- |
| [`basic`](./basic) | Non-PnP control device, typed requests, contexts, queues, cancellation, and common WDF object helpers | Disposable VM without hardware |
| [`echo`](./echo) | Queue synchronization, delayed timer completion, cancel/unmark races, and queue recovery | Disposable VM using the sample INF |
| [`pnp`](./pnp) | Root-enumerated PnP/power callbacks, resources, device interface, idle policy, and typed IOCTL | Disposable VM using the sample INF |
| [`bus`](./bus) | Dynamic PDO plug/remove/eject and a child function driver with typed `QUERY_INTERFACE` | Disposable VM using both INFs |
| [`filter-stack`](./filter-stack) | Root function target, upper-filter forwarding, typed completion, and observable stack composition | Disposable VM using the combined INF |
| [`reference`](./reference) | Production-oriented software-device starting point with a versioned ABI, per-file sessions, PnP/power state, delayed completion, and cancellation | Disposable VM and the full Driver Verifier acceptance gate |
| [`dma`](./dma) | Packet-DMA, scatter/gather, common-buffer, interrupt, and DPC integration template | Matching DMA hardware required |
| [`usb`](./usb) | USB device/interface/pipe and continuous-reader template | Matching USB hardware required |
| [`wmi`](./wmi) | MOF-backed WMI query/set/method providers and event delivery | Disposable VM using the sample INF |

All projects support standalone CMake builds. Each public example also carries
a Visual Studio solution/project so package restore and Driver Settings can be
used directly. Repository project conventions are checked by
`scripts/ci/Test-CrtSysExampleProjects.ps1`.

Stress and verifier fixtures are intentionally not examples. They remain under
[`test/kmdf`](../../test/kmdf), including the concurrent
[`verifier-stress`](../../test/kmdf/verifier-stress) driver/app pair.
The [WDK sample coverage matrix](../../test/kmdf/WDK-SAMPLE-COVERAGE.md)
defines the supported common surface, while the
[software-only runtime suite](../../test/kmdf/runtime) composes the public
examples into one install/restart/remove VM gate.
