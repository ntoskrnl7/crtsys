# NTL minifilter swap-buffers sample

This sample demonstrates transparent input and output replacement with
`ntl::flt::try_swap_io_buffers`:

- pre-write copies the caller's input into replacement pages, XORs only the
  replacement, and sends it to the lower file system;
- pre-read installs replacement output pages;
- post-read XORs only the valid bytes reported by `IoStatus.Information`,
  stabilizes the restored original output buffer, and copies the result back;
- Fast I/O is retried as IRP I/O for protected files; and
- APC-level callbacks are deferred to Filter Manager PASSIVE-level work items.

READ and WRITE are operation-typed, single-direction requests. Their policy
options carry no direction booleans: NTL infers input for WRITE and output for
READ, and the opposite selector cannot be expressed accidentally. Operations
that can legitimately expose both directions, such as QUERY_EA or IOCTL,
instead require an explicit `ntl::flt::swap_buffer` selector.

File selection distinguishes an ordinary file from a failed/unsafe name query.
An unknown name is retried through the IRP/PASSIVE path and fails closed if it
still cannot be resolved; it is never silently classified as pass-through.

Only files whose extension is `.ntlxor` are transformed. A `.tmp` file is used
by the companion application to demonstrate the pass-through path. Paging I/O
is intentionally skipped, so this is an API and ownership example, not a
complete transparent-encryption product.

The sample follows the critical ownership rule: it never calls
`FltLockUserBuffer` on an original buffer before overwriting the IOPB with a
replacement MDL. For read copy-back, the original output is stabilized in the
post-operation path after Filter Manager has restored the original parameters.

The implementation uses `ntl::is_passive_level()` for execution-context
decisions and returns `ntl::status` from its internal and deferred completion
helpers. Native `PFLT_CALLBACK_DATA` appears only in three thin adapters where
NTL reconstructs operation-typed callback data; Filter Manager status updates
use `callback_data::set_io_status()`. Conversion to native `NTSTATUS` stays
inside NTL's Filter Manager trampoline.

## Build

From the repository root:

```powershell
cmake -S examples\minifilter\swap-buffers `
      -B out\minifilter-swap-buffers-x64 -A x64
cmake --build out\minifilter-swap-buffers-x64 --config Debug
```

The targets are `crtsys_minifilter_swap_buffers_sample` and
`crtsys_minifilter_swap_buffers_sample_app`.

For a direct Visual Studio/WDK build, open
`crtsys_minifilter_swap_buffers_sample_vs.sln`, or run:

```powershell
msbuild crtsys_minifilter_swap_buffers_sample_vs.sln /restore `
        /p:Configuration=Debug /p:Platform=x64
```

After test-signing and installing the INF:

```powershell
fltmc load CrtSysMinifilterSwapBuffersSample
crtsys_minifilter_swap_buffers_sample_app.exe
fltmc unload CrtSysMinifilterSwapBuffersSample
```

## Deliberate limitations

XOR is not encryption. This sample has no key management, authentication,
nonce, on-disk format, memory-mapped I/O support, paging-I/O policy,
rename/hard-link policy, crash recovery, or coordination between cached and
noncached paths. Those concerns must be designed before adapting the example
for real data protection.

The INF altitude `370030.129` is development-only. Obtain a unique
Microsoft-assigned altitude before distribution. See
[I/O buffer mapping and swapping](../../../docs/ntl/io-buffer-mapping.md) for
the full API and lifetime contract.
