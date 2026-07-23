# Minifilter Verifier Stress

This fixture repeatedly opens and closes the runtime communication port and
then unloads and reloads the installed minifilter. Run it from an elevated
test environment with Driver Verifier already configured for the target
driver. The fixture does not change verifier settings.

```text
crtsys_flt_verifier_stress_app.exe [iterations] [filter-name] [port-name]
```

The default names match `test/flt/runtime`. The test closes every port handle
before unloading the filter, so it specifically exercises communication-port
teardown and the next load's registration path. A failure is reported at the
first connection, unload, or reload operation.
