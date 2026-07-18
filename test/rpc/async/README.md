# NTL RPC asynchronous transport test

This fixture validates the optional `server_options::asynchronous()` endpoint
mode and the user-mode `client::invoke_async()` request owner. It covers:

- an RPC request outliving the `client` object that started it;
- bounded waits that report timeout without releasing live I/O buffers;
- targeted `CancelIoEx` cancellation;
- typed value and void results;
- safe cancellation and drain when a pending request object leaves scope;
- multiple concurrent overlapped calls on one device handle; and
- a configured pending-call limit rejecting excess work before callback
  invocation;
- a service stop waiting for an in-flight asynchronous callback and pending
  IRP to drain; and
- service restart followed by a fresh contract query and RPC call.

The server executes application callbacks on PASSIVE_LEVEL system workers. A
cancel request prevents a queued callback from starting when possible. Once a
callback is already running, NTL does not terminate kernel execution; it waits
for the callback to return, discards its output, and completes the IRP with
`STATUS_CANCELLED`.

This is a test fixture rather than an onboarding example. Run it only in a
disposable kernel-debugging VM, and repeat it under Driver Verifier before
changing the pending-IRP or cancellation ownership rules.

Pass the installed service name to exercise the in-flight stop and restart
path:

```text
crtsys_rpc_async_app.exe CrtSysRpcAsync
```

The fixture has been validated under standard Driver Verifier with a v143 x64
Debug driver against both a v143 x64 Debug client and a v142 x86 Debug client.
Each client completed three outer load cycles, including an in-flight service
stop and restart in every cycle. The combined run recorded 12 verified driver
loads and 12 unloads with no untagged, untracked, or failed pool allocations.
