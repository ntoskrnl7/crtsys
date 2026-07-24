# NTL KMDF samples

Each directory is an independent, buildable KMDF project. The grouping keeps
software-only examples separate from hardware-specific templates:

| Directory | Purpose | Runtime validation |
| --- | --- | --- |
| [`basic`](./basic) | Non-PnP control device, typed requests, contexts, queues, cancellation, and common WDF object helpers | Disposable VM without hardware |
| [`pnp`](./pnp) | Root-enumerated PnP/power callbacks, resources, device interface, idle policy, and typed IOCTL | Disposable VM using the sample INF |
| [`bus`](./bus) | Dynamic PDO plug/remove/eject and a child function driver with typed `QUERY_INTERFACE` | Disposable VM using both INFs |
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
