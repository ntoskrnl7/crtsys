# CI driver load tests

The `CMake` workflow has two layers:

1. GitHub-hosted `windows-2022` runners build the test app, build the x64 and
   ARM64 drivers, test-sign the drivers, and upload artifacts. x86 package
   libraries/layout are validated separately because the hosted image does not
   provide x86 WDK kernel libraries.
2. When `workflow_dispatch` is run with `run_driver_load_tests=true`, a
   prepared self-hosted Windows runner downloads those artifacts, loads the
   signed driver, and runs the x64 test app.

This split keeps the build reproducible on GitHub-hosted runners while keeping
kernel driver loading on a machine that was explicitly configured for it.

## Runner requirements

Prepare a Windows x64 machine or VM with:

- GitHub Actions self-hosted runner registered to this repository.
- Runner labels: `self-hosted`, `windows`, `x64`, `crtsys-driver-test`.
- The runner process running elevated, or installed as a service account that
  can create and start kernel driver services.
- Test signing enabled before boot.
- Secure Boot disabled when it prevents enabling test signing.

The self-hosted runner does not need Visual Studio, CMake, or the WDK for the
load-test job. The workflow downloads the already-built artifacts from the same
run.

The load-test job is x64 because kernel drivers must match the test machine's
kernel architecture. Testing ARM64 drivers requires a separate Windows ARM64
self-hosted runner and matching ARM64 test app pipeline. Testing x86 drivers
requires a 32-bit Windows test machine; x86 kernel drivers cannot be loaded on
x64 Windows.

## Enable test signing

Run these commands from an elevated PowerShell prompt on the runner machine:

```powershell
bcdedit /set testsigning on
```

Reboot the machine, then verify:

```powershell
bcdedit /enum '{current}'
```

The output should contain `testsigning Yes` or `testsigning on`.

## Register the runner

Create a new self-hosted runner in GitHub:

```text
Repository -> Settings -> Actions -> Runners -> New self-hosted runner
```

Use the GitHub-provided commands for Windows. When configuring the runner, add
the custom label:

```powershell
.\config.cmd --url https://github.com/ntoskrnl7/crtsys --token <token> --labels crtsys-driver-test
```

Install it as a service if you want it to survive reboots:

```powershell
.\svc install
.\svc start
```

Confirm the runner appears online in GitHub before triggering the load test.

## Run the load test

Open the `CMake` workflow in GitHub Actions, select `Run workflow`, and set:

```text
run_driver_load_tests = true
```

The workflow will:

- Run a preflight check for an online runner with the required labels.
- Build and upload `crtsys-test-driver-x64`.
- Build and upload `crtsys-test-driver-ARM64`.
- Build and upload `crtsys-test-app-x64`.
- Download those artifacts on the self-hosted runner.
- Create a temporary `CrtSysTest` kernel service.
- Start the driver, run `crtsys_test_app.exe`, then stop and delete the service.

If the test fails before loading the driver, check that the runner is elevated
and that `bcdedit /enum '{current}'` reports test signing enabled.
