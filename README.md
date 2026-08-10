# crtsys vcpkg registry

This branch is the Git registry for published crtsys vcpkg ports. It is kept
separate from the source branch so vcpkg can consume the standard `ports/` and
`versions/` registry layout directly.

Add the registry to a consuming repository's `vcpkg-configuration.json`, using
a commit from this branch as the baseline:

```json
{
  "registries": [
    {
      "kind": "git",
      "repository": "https://github.com/ntoskrnl7/crtsys",
      "reference": "vcpkg-registry",
      "baseline": "<vcpkg-registry-commit>",
      "packages": ["crtsys"]
    }
  ]
}
```

Then add `crtsys` to `vcpkg.json` and select a Windows static-CRT triplet such
as `x64-windows-static`.
