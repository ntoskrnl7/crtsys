# Browser HTTPS inspection traffic fixtures

[Korean](./README.ko-KR.md)

This directory contains deterministic traffic generation and acceptance-only
code for `examples/wfp/user/browser-https-inspection`. It deliberately does not
own WFP policy sessions, configure the driver, or call `DeviceIoControl`.

The product-side executables are separate:

- `crtsys_wfp_browser_https_inspection_controller.exe` observes an existing
  browser and owns the TCP browser policy lifetime.
- `crtsys_wfp_browser_https_inspection_http3_proxy_service.exe` owns the H3
  endpoint and, in `--wfp-managed-http3-proxy` mode, its application-scoped
  bidirectional UDP/443 tuple-translation policy.

The fixture targets are:

- `crtsys_wfp_browser_https_inspection_acceptance`: private loopback H3 origin,
  inspection proxy, traffic, and bounded protocol assertions.
- `crtsys_wfp_browser_https_inspection_managed_client_acceptance`: traffic-only
  managed H3 client.
- `*_msh3_client_acceptance`, `*_raw_msquic_acceptance`, and
  `*_msquic_loopback_acceptance`: transport contract executables.

Build them independently:

```powershell
cmake -S test\wfp\runtime\fixtures\user\browser-https-inspection `
      -B artifacts\test\wfp-browser-https-inspection-acceptance -A x64
cmake --build artifacts\test\wfp-browser-https-inspection-acceptance `
      --config Release
```

The loopback executable accepts only acceptance traffic:

```text
crtsys_wfp_browser_https_inspection_acceptance.exe --controlled-http3-e2e <proxy-port> <origin-port> <log-directory> [duration-seconds]
```

`Start-ManagedHttp3Acceptance.ps1` starts the product H3 proxy service as a
child process, waits for `<log-directory>/service.ready`, runs the managed
traffic client, and creates `<log-directory>/stop.request` for drained
shutdown. The service process, not this fixture, installs and removes WFP
policy.

The shared runtime runner and packaging manifest intentionally live outside
this fixture and are maintained by the runtime acceptance layer.
