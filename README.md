# crtsys vcpkg registry

This branch is the Git registry for published crtsys vcpkg ports. It is kept
separate from the source branch so vcpkg can consume the standard `ports/` and
`versions/` registry layout directly.

Add the registry to a consuming repository's `vcpkg-configuration.json`. The
current stable baseline is `e512c60be887e571fae3d8e17e791d91a0004820`:

```json
{
  "registries": [
    {
      "kind": "git",
      "repository": "https://github.com/ntoskrnl7/crtsys",
      "reference": "vcpkg-registry",
      "baseline": "e512c60be887e571fae3d8e17e791d91a0004820",
      "packages": ["crtsys"]
    }
  ]
}
```

Then add `crtsys` to `vcpkg.json` and select a Windows static-CRT triplet such
as `x64-windows-static`.
