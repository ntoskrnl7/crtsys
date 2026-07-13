# NTL Lookaside List

[Back to NTL docs](./README.md)

Header: [`include/ntl/lookaside_list`](../../include/ntl/lookaside_list)

`ntl::lookaside_list<T>` wraps WDK `LOOKASIDE_LIST_EX` for fixed-size kernel
objects. Use it when a driver repeatedly allocates and frees many objects of
the same type and wants the normal WDK lookaside cache behavior with C++ object
construction and cleanup helpers.

This is intentionally still a kernel API. The pool kind, pool tag, and lookaside
lifetime remain visible.

## Quick Examples

Nonpaged object cache:

```cpp
struct packet_context {
  explicit packet_context(unsigned id) : id(id) {}
  unsigned id;
};

using packet_cache =
    ntl::lookaside_list<packet_context,
                        ntl::pool_kind::nonpaged,
                        ntl::pool_option::none,
                        ntl::pool_tag("PKTl")>;

packet_cache packets;

auto packet = packets.make(42);
packet->id += 1;

auto packet_result = packets.try_make(43);
if (!packet_result) {
  return packet_result.status();
}
```

Raw allocation and explicit construction:

```cpp
auto* packet = packets.allocate();
if (!packet) {
  return STATUS_INSUFFICIENT_RESOURCES;
}

new (packet) packet_context{7};

packet->~packet_context();
packets.free(packet);
```

Paged control-path cache:

```cpp
using name_cache =
    ntl::lookaside_list<name_record,
                        ntl::pool_kind::paged,
                        ntl::pool_option::none,
                        ntl::pool_tag("NAMl")>;

name_cache names;
auto name = names.make(L"control-path-only");
```

Cache-aligned nonpaged cache:

```cpp
using aligned_cache =
    ntl::lookaside_list<cache_line_state,
                        ntl::pool_kind::nonpaged,
                        ntl::pool_option::cache_aligned,
                        'lCAC'>;
```

## API Summary

- `ntl::lookaside_list<T, Kind, Options, Tag>`
- `lookaside_list(depth)`
- `allocate()`
- `free(pointer)`
- `make(args...)`
- `try_make(args...)`
- `destroy(pointer)`
- `flush()`
- `native_handle()`

`make(args...)` returns a move-only RAII pointer. The lookaside list must outlive
all pointers created from it because the pointer deleter returns objects to that
specific list.

`try_make(args...)` returns `ntl::result<pointer>` instead of throwing. Use it
when a driver initialization path wants to preserve `NTSTATUS` flow.

The WDK recommends passing `0` for `depth`. If you pass an explicit nonzero
depth, it must be within the WDK `EX_MAXIMUM_LOOKASIDE_DEPTH_*` range. Small
values such as `4` or `32` are not valid explicit depths.

## Options

`ntl::lookaside_list` supports these `ntl::pool_option` values:

- `ntl::pool_option::none`
- `ntl::pool_option::cache_aligned`
- `ntl::pool_option::raise_on_failure`

Other `pool_option` values are rejected at compile time. The wrapper does not
pretend to support flags that `ExInitializeLookasideListEx` cannot represent.

## IRQL Guidance

The underlying WDK lookaside allocation/free primitives are documented for
`<= DISPATCH_LEVEL`, but object construction, destruction, exception handling,
and arbitrary C++ code are not automatically DPC-safe.

Use this rule of thumb:

- Raw nonpaged `allocate()` / `free()` can follow the WDK lookaside contract.
- `make()` / `destroy()` should be treated as `PASSIVE_LEVEL` unless the object
  type and constructor/destructor are separately audited.
- Paged lookaside lists must only be used where paged memory is legal.

Do not keep RAII objects alive past the `lookaside_list` that created them.

## Tested Coverage

The driver test suite exercises:

- raw nonpaged allocate/construct/destroy/free
- RAII object creation, `try_make`, move, reset, and destruction
- paged lookaside list construction
- cache-aligned nonpaged lookaside list construction
- explicit `flush()`
