# NTL IPC Shared Memory

[Back to NTL docs](./README.md)

`ntl::ipc` is the transport-neutral data-plane foundation shared by NTL RPC and
the minifilter communication-port transport. It does not create a WDM
device, a KMDF queue, or a Filter Manager port. Those transports own connection,
authorization, cancellation, and region lifetime; `ntl::ipc` defines the
pointer-free memory contract used after a transport has safely registered a
region.

Headers:

- [`include/ntl/ipc/common`](../../include/ntl/ipc/common)
- [`include/ntl/ipc/shared_ring`](../../include/ntl/ipc/shared_ring)
- [`include/ntl/ipc/kernel_region`](../../include/ntl/ipc/kernel_region), driver
  builds only
- [`include/ntl/ipc/all`](../../include/ntl/ipc/all)

## Cross-Bitness Contract

Never send a pointer, `size_t`, iterator, or process-local handle through an IPC
contract. A transport registers the caller's memory and returns a fixed-width
`region_handle`. Operations then refer to a subrange with `buffer_token`:

```cpp
struct buffer_token {
  ntl::ipc::region_handle region; // uint64 ID + uint64 generation
  std::uint64_t offset;
  std::uint64_t length;
};
```

The generation rejects stale tokens after disconnect, unregister, or region-ID
reuse. The receiving transport must also validate access and bounds against its
own region table. A token is not an authorization capability by itself.

This layout is identical for an x86 app and x64 driver. Record types stored in
shared memory must follow the same rule: use fixed-width fields and do not store
native pointers, native handles, `size_t`, `std::string`, or STL containers.

## Fixed-Record Ring

`shared_ring<T, Capacity>` is a bounded single-producer/single-consumer queue.
Use one ring for app-to-driver records and another ring for driver-to-app
records:

```cpp
struct telemetry_record {
  std::uint64_t timestamp;
  std::uint32_t event_id;
  std::uint32_t value;
};

using telemetry_ring = ntl::ipc::shared_ring<telemetry_record, 1024>;

// The allocating side initializes an aligned shared range once.
telemetry_ring producer;
auto status = telemetry_ring::initialize(memory, bytes, producer, generation);

// The other side validates the exact magic/version/record-size/capacity.
telemetry_ring consumer;
status = telemetry_ring::attach(memory, bytes, consumer);

producer.try_write(record); // false applies bounded backpressure when full
consumer.try_read(record);  // false when empty
```

The ring copies trivially copyable records into byte slots. It does not place C++
objects in another process and does not serialize variable-size data. Sequence
publication uses Windows interlocked operations so the consumer never observes
a slot before the producer has finished copying it.

Exactly one producer and one consumer are allowed per ring. Multiple producers
or consumers require separate rings or a higher-level transport policy.

## IOCTL RPC Adapter

The IOCTL RPC transport implements session-bound registration. The app creates
or resumes a session, allocates a page-backed region, and registers it through
the existing session-control IOCTL:

```cpp
ntl::rpc::client client(L"telemetry_rpc");
(void)client.start_session();

auto region = client.register_shared_region(
    telemetry_ring::required_bytes(),
    ntl::ipc::region_access::driver_read_write);

telemetry_ring app_ring;
if (telemetry_ring::initialize(region.data(), region.size(), app_ring) !=
    ntl::ipc::validation_status::success)
  throw std::runtime_error("could not initialize telemetry ring");

auto token = region.token(0, telemetry_ring::required_bytes());
client.invoke(start_telemetry, token);
```

The RPC callback receives only the pointer-free token. It asks the callback's
session to resolve the token with the exact access it needs:

```cpp
server->on(start_telemetry,
  [](const ntl::rpc::call_context& call, ntl::ipc::buffer_token token) {
    auto pinned = call.try_resolve(
        token, ntl::ipc::region_access::driver_write);
    if (!pinned)
      throw ntl::exception(pinned.status(), "invalid telemetry region");

    telemetry_ring driver_ring;
    if (telemetry_ring::attach(pinned->data(), pinned->size(), driver_ring) !=
        ntl::ipc::validation_status::success)
      throw ntl::exception(STATUS_INVALID_PARAMETER, "invalid ring layout");

    driver_ring.try_write(record);
  });
```

`registered_region` unregisters before `VirtualFree`. Explicit `close()`
reports an unregister error. Its destructor never releases an allocation that
the driver may still have pinned; an exceptional best-effort unregister failure
leaves that allocation reserved until process teardown rather than creating a
use-after-free. Session disconnect, explicit close, retention expiry, and server
stop remove every region from future lookup. A callback that already resolved a
token retains its MDL until that callback releases its `pinned_buffer`.

The default quota is 16 regions and 64 MiB per session. Drivers can lower or
raise those bounds with `server_options::max_shared_regions()` and
`max_shared_region_bytes()`.

## Transport Adapters

The IOCTL RPC adapter and the Filter Manager communication-port adapter both
use the same `region_handle`, `buffer_token`, ring layout, and generation checks.
The transport remains responsible for connection, authorization, cancellation,
and region lifetime; the shared-memory layer only exposes a validated local
view after those checks succeed.

RPC remains the control plane and shared memory is the optional data plane for
high-volume traffic. The registered-region and shared-ring paths are verified
against both an x64 app and an x86 app. User-mode code can also create an
`ntl::ipc::shared_buffer_pool` over a registered region:

```cpp
auto region = client.register_shared_region(4096);
auto pool = region.make_buffer_pool(16);
auto lease_result = pool.try_acquire(512);
if (!lease_result)
  throw std::runtime_error("shared lease allocation failed");

auto lease = std::move(*lease_result);
std::memset(lease.data(), 0, lease.size());
client.invoke(write_payload, lease.token());
```

`buffer_lease` is move-only. Releasing it returns the subrange to the pool and
coalesces adjacent free ranges. The pool owns reservations only: keep the
`registered_region` alive while leases or tokens are in use. The driver still
validates each token against its connection-scoped pinned-region registry, so
the pool does not weaken range, generation, access, or unload checks. The same
pool is available from a minifilter `registered_port_region` through
`make_buffer_pool()`.
