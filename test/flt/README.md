# NTL Minifilter Tests

This directory owns minifilter API contract and runtime fixtures that would
make the public samples harder to read.

`compile/operation_callback.cpp` is compiled into the regular CMake driver
test matrix. It verifies operation-tag callback syntax, operation/callback
type rejection, operation-specific parameter access, and generic lambdas with
`auto` parameters without adding test-only statements to
`examples/minifilter-ntl-driver`. The generic form is compile-supported, while
the public sample uses explicit callback-data types because current C++ editor
completion engines do not reliably list members for contextually instantiated
generic-lambda parameters.

`compile/context.cpp` verifies typed file/stream context declarations, the
move-only Filter Manager reference owner, constructor forwarding, and the
registration surface across supported MSVC toolsets. Runtime file-system
behavior stays in the minifilter driver/app fixture rather than being mixed
into the ordinary WDM unit-test driver.

`compile/instance.cpp` verifies the owning instance reference, identity query,
named and altitude attachment overloads, detach surface, and filter-wide
instance enumeration across supported toolsets.

`runtime/` is a standalone driver/app integration fixture. It keeps the
multi-instance, four-context, conditional WhenSafe, draining, and explicit
detach coverage out of the onboarding example while remaining buildable and
loadable as a real minifilter in the VM. Its communication test also owns the
protocol-negative, quota, coroutine, disconnect, and unload-lifetime checks;
the onboarding sample is intentionally kept shorter.
