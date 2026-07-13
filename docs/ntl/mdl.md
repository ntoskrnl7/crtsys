# NTL MDL Helper

[Back to NTL docs](./README.md)

Header: [`include/ntl/mdl`](../../include/ntl/mdl)

`ntl::mdl` owns an MDL allocated by `IoAllocateMdl`. It is useful when a driver
needs an MDL lifetime tied to C++ scope while still calling the normal WDK
memory-management primitives explicitly.

## Example

```cpp
auto mdl = ntl::mdl::allocate(buffer, buffer_size);
if (!mdl) {
  return mdl.status();
}

mdl->build_for_nonpaged_pool();

auto system_address = mdl->system_address();
if (!system_address) {
  return system_address.status();
}

use_mapped_buffer(*system_address, mdl->byte_count());
```

For pageable or user buffers, lock pages first:

```cpp
auto lock_status = mdl->probe_and_lock_pages(KernelMode, IoModifyAccess);
if (!lock_status) {
  return lock_status.status();
}

// The destructor unlocks locked pages before freeing the MDL.
```

## API Summary

- `ntl::mdl::allocate(virtual_address, length, secondary_buffer, charge_quota, irp)`
- `get()`
- `operator->()`
- `release()`
- `reset(native_mdl)`
- `build_for_nonpaged_pool()`
- `probe_and_lock_pages(access_mode, operation)`
- `unlock_pages()`
- `system_address(priority)`
- `byte_count()`
- `byte_offset()`
- `virtual_address()`

`release()` transfers the native `PMDL` to the caller. After `release()`, the
caller must free it with the correct WDK primitive.

## IRQL

Follow the underlying WDK primitive:

- MDL allocation/free and nonpaged-pool description are control-path friendly.
- `MmProbeAndLockPages` has exception and access-mode behavior; use it only in
  paths where that WDK contract is valid.
- Mapping and page locking should be treated as audited control-path work.

## Driver Test Coverage

The driver test suite exercises:

- MDL allocation for nonpaged stack storage
- `MmBuildMdlForNonPagedPool`
- system-address lookup
- move ownership
- release/manual free
- empty-MDL error path
