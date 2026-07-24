# NTL minifilter samples

The minifilter examples are split by responsibility so each project has one
primary lesson and can be built, installed, and debugged independently.

| Sample | Focus |
| --- | --- |
| [`basic`](./basic) | Typed create/read/write/cleanup callbacks, normalized names, stream contexts, and registration lifetime |
| [`communication`](./communication) | Filter Manager ports, typed RPC, callbacks, notifications, streams, and registered shared-memory rings |
| [`swap-buffers`](./swap-buffers) | Safe pre-write input replacement and post-read output transformation/copy-back for `.ntlxor` files |

The recommended learning order is `basic`, `communication`, then
`swap-buffers`. The last sample is intentionally local and synchronous at the
transform boundary. For a user-service transformation with process mappings,
pending I/O, cancellation, disconnect, and teardown coverage, see the
[`test/flt/runtime`](../../test/flt/runtime) fixture and the
[I/O buffer mapping guide](../../docs/ntl/io-buffer-mapping.md).

Each sample has its own CMake project, Visual Studio solution and `.vcxproj`
files, driver, application, INF, service name, instance name, and development
altitude. The altitude values are examples only; a shipping minifilter needs a
Microsoft-assigned altitude.
