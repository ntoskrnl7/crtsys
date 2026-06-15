# Design Rationale and Operational Boundaries

[Back to README](../README.md)

This document describes what `crtsys` is meant to be, where it is expected to
be used, and where users should keep normal WDK discipline instead of treating
C++ support as a blanket safety guarantee.

## Position

`crtsys` is a kernel-mode C++ runtime substrate. It is not a full user-mode CRT,
not a complete Win32 compatibility layer, and not a mechanism for making
arbitrary user-mode code safe in kernel mode.

The practical goal is narrower:

```text
Provide the runtime substrate needed by selected MSVC C++ / CRT / STL paths,
map the missing dependencies onto kernel primitives, and keep the supported
surface small enough to document and test.
```

That distinction is important. A broad compatibility layer would imply that
normal user-mode assumptions are safe inside a driver. `crtsys` does not make
that claim. It enables a controlled subset of C++ driver development while the
driver still follows WDK rules for IRQL, stack, pool allocation, pageable code,
unload safety, verifier, HVCI, and target-OS validation.

## Why It Exists

Kernel-mode C++ development can use the language, but much of the normal MSVC
runtime environment is missing. In practice, drivers that want modern C++
often need support for:

- static and dynamic initialization
- selected exception runtime paths
- selected CRT functions required by STL code
- STL synchronization and threading paths used in controlled contexts
- stream and diagnostic support
- RAII wrappers around driver objects and locks

Without a shared runtime layer, each driver project has to recreate the same
low-level glue. `crtsys` centralizes that work and ties it to tests.

## Layering

The intended architecture is:

```text
MSVC C++ / selected CRT / selected STL
        |
crtsys compatibility glue
        |
Ldk Win32 / NTDLL-style API shims
        |
Windows kernel primitives
        |
.sys driver
```

In this model, `Ldk` is used as a kernel-backed API shim for known runtime
dependencies. It is not presented here as a way to run arbitrary user-mode
modules in kernel space.

## Execution Model

`crtsys` is designed primarily for driver control paths:

- driver initialization and unload
- device creation and teardown
- IOCTL and RPC request handling
- worker-thread coordination
- ownership cleanup and error handling
- diagnostics and test code

Those paths are normally `PASSIVE_LEVEL`, with selected `APC_LEVEL` use where
the underlying WDK APIs allow it. This is the baseline design assumption.

Unless a feature or API explicitly documents a broader contract, assume
`PASSIVE_LEVEL`. A C++ abstraction may allocate, wait, throw, unwind, acquire a
blocking lock, touch pageable code, or depend on runtime state. Compiling is
not enough to make it safe at elevated IRQL.

Do not use runtime-backed helpers in DPC, ISR, paging I/O, spin-lock-held, or
other elevated-IRQL hot paths unless the exact API is documented and tested for
that context.

## Synchronization Philosophy

NTL favors ownership clarity and blocking/resource-style coordination for the
main driver control paths. That is why `ERESOURCE`-backed helpers are central:
they match the `PASSIVE_LEVEL` / selected `APC_LEVEL` model where code may
coordinate longer-lived driver state and where blocking is allowed by the
underlying WDK contract.

Spin locks still exist because some kernel code genuinely needs them, but they
are not the default abstraction for the runtime-backed C++ model. Spin locks
belong in short, nonblocking, resident, elevated-IRQL-safe regions. They should
not be used to protect paths that may allocate, wait, throw, format streams,
touch pageable code, or call into arbitrary STL/runtime machinery.

## Feature Usage Guidance

| Area | Recommended use | Avoid |
| --- | --- | --- |
| RAII wrappers | Ownership and cleanup in driver control paths | Hiding WDK lifetime rules completely |
| `ntl::status` | Clearer `NTSTATUS` flow | Treating status wrappers as exception policy |
| `ntl::driver` / `ntl::device` | Initialization, teardown, IOCTL registration | Hot-path dispatch without understanding object lifetime |
| `ntl::resource` | Blocking state coordination at valid IRQL | DPC, ISR, spin-lock-held paths |
| `ntl::spin_lock` | Short nonblocking critical regions | Protecting code that can wait, allocate, or unwind |
| STL containers | Management paths where allocation is valid | DPC, ISR, paging I/O, allocation-sensitive paths |
| STL threading primitives | Controlled worker-thread paths | Fire-and-forget work that can outlive unload |
| Exceptions | Tight, tested control paths | Hot paths, elevated IRQL, WDK callback boundaries |
| iostreams | Diagnostics and tests | Production hot paths and stack-sensitive paths |
| `ntl::expand_stack` | Known deep or exception-heavy paths | Replacing shallow call graph design |

## Exception Policy

`crtsys` includes runtime support for selected C++ exception scenarios. This
support exists to make controlled C++ runtime paths possible; it is not a
recommendation to write driver hot paths in normal user-mode exception style.

Recommended boundaries:

- Avoid exceptions in hot paths, paging I/O, DPC, ISR, and spin-lock-held
  regions.
- Do not let exceptions cross WDK callback boundaries unless that behavior is
  explicitly tested and documented.
- Treat exception-heavy paths as stack-sensitive.
- Destructors that run during unwinding must remain kernel-safe.

## Stack Policy

Kernel stacks are small, and layered C++ runtime support can increase stack
pressure. Keep dispatch paths shallow, avoid large local objects, avoid
recursive designs, and keep stream formatting out of production hot paths.

Use `ntl::expand_stack` for call paths where deeper stack usage is expected and
tested. Treat stack expansion as a controlled tool, not as permission to ignore
stack discipline.

## Memory and HVCI Policy

A final driver binary must still be validated as a driver binary. Users should
test with Driver Verifier, Code Integrity checks, Memory Integrity enabled when
that is a deployment requirement, and the relevant HLK or internal validation
suite.

The safe deployment model for `crtsys` is a static runtime compatibility layer.
Designs that create writable/executable mappings, patch executable memory, load
arbitrary executable images, or treat data files as executable code are outside
this model.

## Non-Goals

`crtsys` does not aim to provide:

- full user-mode CRT compatibility
- full Win32 or NTDLL compatibility
- arbitrary user-mode DLL execution in kernel mode
- automatic safety for all STL features in all driver contexts
- hidden WDK object ownership, IRQL, pageable-code, or pool-allocation rules
- a replacement for Driver Verifier, HLK, VM testing, or crash dump analysis
- compatibility guarantees for untested Visual Studio, SDK, WDK, Windows, or
  architecture combinations

## Documentation Rule

If the IRQL contract of a `crtsys` or NTL API is not documented, users should
assume `PASSIVE_LEVEL`.

When documenting new APIs, prefer an explicit contract such as:

- `PASSIVE_LEVEL only`
- `<= APC_LEVEL`
- `<= DISPATCH_LEVEL`
- `DPC-level only`
- `test/diagnostic only`

The API reference should explain why the contract exists when allocation,
blocking, pageable code, exceptions, stack expansion, or runtime glue are
involved.
