# NTL WFP TCP content-filter sample

This sample demonstrates one bounded, fail-closed user-mode content decision
for inbound TCP application messages. It is intentionally separate from the
UDP sample because TCP is a byte stream and has no datagram-style message
boundary.

## Sample application protocol

The runtime proof uses:

```text
[four-byte big-endian content length][content]
```

The four-byte prefix is **not a TCP header or TCP standard**. It belongs only
to this sample application protocol. Replace
`ntl::net::framing::u32_be_length_prefix` with the framer for the real protocol.

## Enforcement path

1. IPv4 and IPv6 ALE flow-established callouts associate typed state with the
   selected inbound TCP flows;
2. `STREAM_V4/V6` callouts request more bytes until one bounded application
   message is complete;
3. inbound stream data is deferred and copied into owned storage;
4. a reliable typed NTL RPC notification reaches the user-mode coroutine;
5. `permit` continues the deferred stream and enforces exactly that frame;
6. `block`, timeout, malformed framing, disconnect, quota exhaustion, or
   allocation failure drops the whole flow.

The app sends an allowed message with its four-byte prefix split across two
socket writes over IPv4 and IPv6. It then sends `BLOCKME` on both and proves
that each flow is closed. Removing the session-scoped WFP policy restores
ordinary TCP delivery for both.

Run `crtsys_wfp_tcp_content_filter_app.exe --failure-self-test` to verify
malformed-verdict rejection, timeout flow drop, late-permit rejection, and
post-policy restoration.

See [README.ko-KR.md](./README.ko-KR.md) for the Korean walkthrough.
