# NTL RPC Streaming Fixture

This fixture validates session-bound typed streaming in both directions.

Coverage includes:

- typed STL-bearing app-to-driver and driver-to-app chunks;
- an x64 driver wire contract usable by x64 and x86 apps;
- bounded per-session download queues and `STATUS_DEVICE_BUSY` backpressure;
- explicit ACK and queue-capacity recovery;
- read timeout, targeted `CancelIoEx`, and scope cleanup;
- cooperative cancellation of a running upload callback;
- disconnect followed by token reconnect and replay of an unacknowledged
  record;
- successful and failed terminal records with a WDK `NTSTATUS` value;
- terminal-state rejection, terminal reconnect replay, and reopen after close;
- persistence restore ordering when a storage hook returns terminal and chunk
  records in reverse sequence;
- bounded multi-chunk upload and download batches, including a terminal record;
- cancellation of a pending empty batch receive and invalid batch bounds;
- any-channel batch selection where a later critical record precedes an
  earlier background record without changing FIFO inside either channel;
- malformed, truncated, and oversized upload rejection;
- client/server expansion of ordinary and authorized stream contract macros.

Streaming uses ordinary limited RPC calls for uploads and reliable
notification records for downloads. A timeout does not free a live I/O
request. The app must continue waiting, cancel it, or destroy its owner.
Closing a stream requires all delivered records to be acknowledged and all
pending reads and writes to be cancelled or drained first.
