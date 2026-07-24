# NTL basic minifilter sample

This is the smallest complete NTL minifilter in the sample set. It demonstrates:

- typed pre-create and post-create callbacks;
- normalized file-name queries and parsing;
- creating, retrieving, and automatically releasing a stream context;
- typed read, write, and cleanup callbacks; and
- operation flags such as `skip_paging_io`.

The sample watches `.tmp` opens, captures a normalized name after a successful
create, and counts non-paging writes in a stream context. The application
creates, reads, renames, and removes one temporary file.

The cached name is deliberately a post-create snapshot. Real policy code must
define what a later rename or hard-link operation means instead of assuming the
snapshot remains the stream's current name.

## Build

From the repository root:

```powershell
cmake -S examples\minifilter\basic -B out\minifilter-basic-x64 -A x64
cmake --build out\minifilter-basic-x64 --config Debug
```

The targets are `crtsys_minifilter_basic_sample` and
`crtsys_minifilter_basic_sample_app`.

For a direct Visual Studio/WDK build, open
`crtsys_minifilter_basic_sample_vs.sln`, or run:

```powershell
msbuild crtsys_minifilter_basic_sample_vs.sln /restore `
        /p:Configuration=Debug /p:Platform=x64
```

After test-signing and installing the INF:

```powershell
fltmc load CrtSysMinifilterBasicSample
crtsys_minifilter_basic_sample_app.exe
fltmc unload CrtSysMinifilterBasicSample
```

The INF altitude `370030.127` is only for development. Obtain a unique
Microsoft-assigned altitude before distributing a real minifilter.
