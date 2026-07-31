# NTL WFP specialized observation

This runtime fixture covers WFP layers that normal loopback proxy examples do
not naturally exercise:

- `ALE_ENDPOINT_CLOSURE_V4/V6`;
- inbound and outbound Ethernet MAC-frame layers;
- ingress and egress Hyper-V vSwitch Ethernet layers;
- schema discovery for Microsoft-internal fast-transport layers; and
- schema discovery for the user-mode IPv4 and IPv6 IPsec policy layers.

The driver registers six valid third-party kernel callouts and exposes
allocation-free counters. The controller verifies that every layer schema
exists, installs endpoint, MAC, and vSwitch inspection filters in one dynamic
transaction, creates IPv4 and IPv6 TCP endpoints, and requires both
endpoint-closure callbacks. Fast-transport layers are introspection-only
because Windows reserves them for internal use. IPsec V4/V6 are user-mode
quick-mode policy layers, not kernel callout layers.

The controller reports MAC and vSwitch layers as observed only when the machine
actually produces matching traffic. A zero MAC or vSwitch count is therefore a
capability-not-exercised result, not a false runtime pass; vSwitch coverage
requires a configured Hyper-V switch in the acceptance environment.
