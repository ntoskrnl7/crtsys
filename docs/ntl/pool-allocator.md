# NTL Pool Allocator

[Back to NTL docs](./README.md)

Header: [`include/ntl/pool_allocator`](../../include/ntl/pool_allocator)

`ntl::pool_allocator` lets driver code choose the kernel pool that backs an STL
container or PMR resource. `ntl::pool_ptr` and `ntl::pool_buffer` provide the
same pool/tag model for raw object and byte-buffer ownership. These helpers are
useful when the default `crtsys` heap is too implicit and the code should say
"this data lives in nonpaged pool" or "this control-path container may use paged
pool".

The API deliberately keeps WDK concepts visible:

- `ntl::pool_kind` selects the pool category.
- `ntl::pool_option` selects WDK-style modifiers.
- The pool tag is always explicit when you care about diagnostics.

## Quick Examples

Raw nonpaged allocation:

```cpp
void* buffer = ntl::allocate_pool(128,
                                  ntl::pool_kind::nonpaged,
                                  ntl::pool_option::none,
                                  "BUF1");
if (!buffer) {
  return STATUS_INSUFFICIENT_RESOURCES;
}

ntl::free_pool(buffer, "BUF1");
```

Raw nonpaged allocation with RAII cleanup:

```cpp
auto buffer = ntl::make_pool_buffer(128,
                                    ntl::pool_kind::nonpaged,
                                    ntl::pool_option::none,
                                    "BUF2");
if (!buffer) {
  return STATUS_INSUFFICIENT_RESOURCES;
}
```

Object allocation with construction and destruction:

```cpp
struct packet_state {
  explicit packet_state(int id) : id(id) {}
  int id;
};

auto state = ntl::make_pool<packet_state>("PKTs", 42);
state->id += 1;
```

Nonpaged STL vector:

```cpp
using packet_vector =
    std::vector<packet,
                ntl::nonpaged_pool_allocator<packet, ntl::pool_tag("PKTv")>>;

packet_vector packets;
packets.reserve(32);
```

Paged STL vector for a PASSIVE_LEVEL control path:

```cpp
using name_vector =
    std::vector<std::wstring,
                ntl::paged_pool_allocator<std::wstring, ntl::pool_tag("NAMv")>>;

name_vector names;
names.emplace_back(L"device-0");
```

Cache-aligned nonpaged vector using a WDK-style reversed multi-character tag:

```cpp
using cache_aligned_ints =
    std::vector<int,
                ntl::pool_allocator<int,
                                    ntl::pool_kind::nonpaged,
                                    ntl::pool_option::cache_aligned,
                                    'cLTN'>>;
```

PMR resource:

```cpp
ntl::pmr::pool_resource paged_resource{ntl::pool_kind::paged,
                                       ntl::pool_option::none,
                                       "PMRp"};

std::pmr::vector<int> values{&paged_resource};
values.assign({1, 2, 3});
```

## Pool Kinds

| Kind | Meaning | Typical use |
| --- | --- | --- |
| `ntl::pool_kind::nonpaged` | Nonpaged NX pool | Data that may be touched where paging is not legal |
| `ntl::pool_kind::paged` | Paged pool | PASSIVE_LEVEL control-path data |
| `ntl::pool_kind::nonpaged_execute` | Executable nonpaged pool | Rare compatibility cases that truly require executable pool |

Prefer `nonpaged` or `paged`. Treat `nonpaged_execute` as an explicit WDK-level
choice, not as a default.

## Pool Options

| Option | Meaning |
| --- | --- |
| `ntl::pool_option::none` | No extra modifier |
| `ntl::pool_option::use_quota` | Charge quota when the target WDK API supports it |
| `ntl::pool_option::uninitialized` | Request uninitialized memory on `ExAllocatePool2` targets |
| `ntl::pool_option::session` | Request session pool on `ExAllocatePool2` targets |
| `ntl::pool_option::cache_aligned` | Request cache-aligned allocation |
| `ntl::pool_option::raise_on_failure` | Ask the WDK allocation API to raise on failure |
| `ntl::pool_option::special_pool` | Request special pool on `ExAllocatePool2` targets |

Options are bit flags:

```cpp
auto options = ntl::pool_option::cache_aligned |
               ntl::pool_option::raise_on_failure;
```

Downlevel targets that use `ExAllocatePoolWithTag` cannot represent every
modern `POOL_FLAGS` option. Unsupported combinations fail allocation instead of
silently pretending that the option was honored.

## Pool Tags

For raw helpers and PMR resources, prefer string literals in the order you want
to read in code:

```cpp
auto* p = ntl::allocate_pool(64, "BUF2");
ntl::free_pool(p, "BUF2");
```

For allocator template arguments, C++14-compatible code cannot pass `"BUF2"` as
a non-type template parameter. Use `ntl::pool_tag("BUF2")` or the traditional
WDK reversed multi-character literal:

```cpp
using a = ntl::nonpaged_pool_allocator<int, ntl::pool_tag("INTv")>;
using b = ntl::pool_allocator<int,
                              ntl::pool_kind::nonpaged,
                              ntl::pool_option::none,
                              'vTNI'>;
```

Both forms produce the same kind of raw `ULONG` tag value. The string form is
usually easier to read; the multi-character form is familiar to WDK code that
already uses reversed tags.

## IRQL Guidance

Raw pool allocation follows the underlying WDK primitive:

- Nonpaged pool allocation/free can follow the WDK nonpaged pool contract.
- Paged pool allocation must only happen where pageable allocation is legal.

STL and PMR usage is more restrictive. A container operation can allocate,
destroy elements, compare values, move objects, throw, or call user code.
Treat STL containers using `ntl::pool_allocator` as `PASSIVE_LEVEL` unless the
exact operation, element type, callbacks, and surrounding storage are audited.

Avoid these patterns:

```cpp
// Bad: vector growth can allocate and move elements while the spin lock is held.
std::unique_lock lock(spin_lock);
nonpaged_vector.push_back(value);

// Bad: paged allocation is not DPC-safe.
auto* p = ntl::allocate_pool(64, ntl::pool_kind::paged, "BADp");
```

Good pattern:

```cpp
// Allocate and prepare at PASSIVE_LEVEL.
packet_vector packets;
packets.reserve(64);

// Later, use preallocated resident storage in a separately audited hot path.
```

## API Summary

- `ntl::pool_tag("TAGx")`
- `ntl::pool_tag(a, b, c, d)`
- `ntl::allocate_pool(bytes, kind, options, tag)`
- `ntl::allocate_pool(bytes, kind, options, "TAGx")`
- `ntl::allocate_pool(bytes, kind, "TAGx")`
- `ntl::allocate_pool(bytes, "TAGx")`
- `ntl::free_pool(pointer, tag)`
- `ntl::free_pool(pointer, "TAGx")`
- `ntl::pool_deleter<T>`
- `ntl::pool_ptr<T>`
- `ntl::pool_buffer`
- `ntl::make_pool<T>(kind, options, tag, args...)`
- `ntl::make_pool<T>("TAGx", args...)`
- `ntl::make_pool_buffer(bytes, kind, options, tag)`
- `ntl::make_pool_buffer(bytes, "TAGx")`
- `ntl::pool_allocator<T, Kind, Options, Tag>`
- `ntl::nonpaged_pool_allocator<T, Tag>`
- `ntl::paged_pool_allocator<T, Tag>`
- `ntl::pmr::pool_resource`
- `ntl::pmr::nonpaged_pool_resource()`
- `ntl::pmr::paged_pool_resource()`

## Implementation Notes

Targets that opt into an `NTDDI_VERSION` where `ExAllocatePool2` is part of the
kernel contract use `ExAllocatePool2` / `ExFreePool2`.

Downlevel targets use `ExAllocatePoolWithTag` / `ExFreePoolWithTag` for the
options that the legacy API can represent. This preserves older-kernel load
compatibility while still letting newer targets expose richer pool flags.

The driver test suite exercises:

- raw nonpaged allocation/free with string tags
- raw nonpaged allocation/free with WDK-style multi-character tags
- `ntl::pool_buffer` release/reset cleanup
- `ntl::pool_ptr<T>` construction, move, release, reset, and destruction
- `std::vector` with nonpaged and paged pool allocators
- template tags via both `ntl::pool_tag("TAGx")` and `'xGAT'`
- `std::pmr::vector` with `ntl::pmr::pool_resource`
