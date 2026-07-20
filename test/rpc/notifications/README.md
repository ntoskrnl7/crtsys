# NTL RPC Notification Fixture

This fixture validates typed kernel-to-user notifications over a pending
`METHOD_BUFFERED` IOCTL and a kernel `IO_CSQ` cancel-safe queue.

Coverage includes:

- fixed-width notification selection across an x64 driver and x86/x64 apps;
- `std::string` and `std::vector<std::uint32_t>` payload serialization;
- synchronous and overlapped typed receives;
- timeout, targeted cancellation, and scope cleanup;
- FIFO delivery and the pending receive limit;
- rejection of serialization above `PASSIVE_LEVEL`;
- service stop with pending receive cancellation, handle cleanup, unload,
  restart, and a fresh call;
- no implicit event buffering when no application receive is pending.
- reconnectable sessions with opaque cross-bitness tokens;
- reliable delivery replay until explicit ACK;
- bounded per-session backpressure and capacity release after ACK;
- subscription cancellation and explicit session close;
- typed session state and external persistence hook invocation;
- session close waiting for an active asynchronous session RPC callback.

The VM authority case uses the x64 Debug driver with both the x64 and x86
Debug apps under Driver Verifier. Pass the installed service name to the app
to include stop, unload, restart, and endpoint recovery.
