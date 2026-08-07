# WFP runtime fixtures

This directory contains controlled traffic generators and deterministic
origins used by WFP runtime acceptance.  They are intentionally not part of
the corresponding example controller or policy service.

The boundary is strict:

- `examples/wfp/<runtime>/<sample>/driver` owns the WFP callout data path.
- The example controller or policy service owns WFP policy lifetime, driver
  configuration, redirected listeners when they are part of the product data
  path, and operational telemetry.
- `test/wfp/runtime/fixtures/<runtime>/<sample>` owns controlled clients,
  controlled origins, malformed traffic, load generation, proof assertions,
  and PASS/FAIL markers.
- A fixture may start the sibling controller/service and use a bounded
  ready/stop protocol, but it must not install WFP policy or configure the
  driver itself.

The live browser inspection controller is different from a managed acceptance
fixture.  It accepts the path of an already running browser for application
scoping; it never starts, terminates, reconfigures, or changes the browser.
Managed HTTP clients and origins stay here so deterministic HTTP/1.1, HTTP/2,
and HTTP/3 coverage does not obscure the real browser example.
