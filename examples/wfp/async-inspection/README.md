# WFP async-inspection

[한국어 설명](./README.ko-KR.md)

This driver/controller pair demonstrates an out-of-band ALE authorization
decision:

1. Initial `ALE_AUTH_CONNECT_V4` classification is pended.
2. The classify result is block-and-absorb while ownership is outstanding.
3. A PASSIVE_LEVEL worker waits 100 ms and completes the operation.
4. WFP reauthorizes the connection.
5. The callout returns the filter's fixed permit or block decision.

`pended_operation` guarantees exactly-once completion. A rundown-protected
queue prevents driver unload until all posted decisions finish.

The decision source is deterministic filter context so the fixture isolates
the pend/complete/reauthorize lifetime. A product can connect that lifetime to
its own bounded policy broker and define separate IPv6 and IPsec policy.
