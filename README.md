# crtsys vcpkg registry

This branch is the Git registry for published crtsys vcpkg ports. It is kept
separate from the source branch so vcpkg can consume the standard `ports/` and
`versions/` registry layout directly.

Add the registry to a consuming repository's `vcpkg-configuration.json`. The
current stable baseline is `bde54df486d558f12e274a5c2c12e92a6e095d46`:

```json
{
  "registries": [
    {
      "kind": "git",
      "repository": "https://github.com/ntoskrnl7/crtsys",
      "reference": "vcpkg-registry",
      "baseline": "bde54df486d558f12e274a5c2c12e92a6e095d46",
      "packages": ["crtsys"]
    }
  ]
}
```

Then add `crtsys` to `vcpkg.json` and select a Windows static-CRT triplet such
as `x64-windows-static`.
