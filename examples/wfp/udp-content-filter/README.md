# NTL WFP UDP content-filter sample

This sample demonstrates bounded, fail-closed user-mode decisions for complete
outbound UDP datagrams. UDP already preserves datagram boundaries, so this
sample has no application-message framer.

## Enforcement path

1. `DATAGRAM_DATA_V4/V6` callouts receive complete outbound UDP datagrams;
2. the driver validates the UDP header and copies at most 4096 payload bytes;
3. it clones and absorbs the original datagram;
4. a reliable typed NTL RPC notification reaches the user-mode coroutine;
5. `permit` reinjects the clone and `block` discards only that datagram;
6. timeout, disconnect, quota exhaustion, malformed verdict, or allocation
   failure remains fail-closed.

The app sends one allowed datagram and one containing `BLOCKME` over both
IPv4 and IPv6, then proves that removing the session-scoped WFP policy
restores ordinary delivery for both.

Run `crtsys_wfp_udp_content_filter_app.exe --failure-self-test` to verify the
pending limit, timeout, malformed-verdict rejection, late-permit rejection,
and policy restoration.

See [README.ko-KR.md](./README.ko-KR.md) for the Korean walkthrough.
