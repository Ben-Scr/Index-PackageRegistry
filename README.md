# Index-PackageRegistry

The cloud registry of packages that show up in the Index editor's Package Manager.

The editor fetches [`index.json`](./index.json) at startup, then offers an Install button for any entry the user doesn't already have. Installing downloads the package zip from a GitHub Release on this repo, verifies its SHA-256, extracts it into `<project>/Packages/<Name>/`, and adds the package to the project's allow-list.

No backend. The whole thing is a static JSON file pointing at GitHub Releases.

## Repo layout

```
Index-PackageRegistry/
├── index.json              ← the registry the editor reads
├── README.md               ← this file
├── packages/               ← source of packages maintained in-repo (optional)
│   └── TestPkg.Hello/
│       ├── index-package.lua
│       └── Source/
└── scripts/
    └── publish-package.ps1 ← zip + sha + (optional) gh release create
```

Packages hosted elsewhere can also be listed — `downloadUrl` can point at any HTTPS zip.

## `index.json` schema

```jsonc
{
  "schemaVersion": 1,
  "generatedAt": "2026-05-23T00:00:00Z",
  "packages": [
    {
      "name": "TestPkg.Hello",           // required, must match the zip's top folder
      "version": "0.1.0",                // required, semver
      "description": "...",              // optional
      "downloadUrl": "https://...",      // required, HTTPS zip
      "sha256": "<lowercase-hex>",       // required, 64 chars
      "sizeBytes": 1234,                 // optional, for the UI
      "minEngineVersion": "0.6.0",       // optional
      "dependencies": ["Index.Math2D"],  // optional, warn-only in v1
      "homepage": "https://...",         // optional
      "license": "MIT"                   // optional
    }
  ]
}
```

## Zip layout (publisher contract)

The zip MUST contain exactly **one top-level folder** named after the package, and `index-package.lua` MUST be directly inside it:

```
TestPkg.Hello-0.1.0.zip
└── TestPkg.Hello/
    ├── index-package.lua
    └── Source/
```

`scripts/publish-package.ps1` produces zips in this shape automatically.

## Publishing a new package version

```powershell
# Zip + sha + print the index.json snippet (no upload):
.\scripts\publish-package.ps1 -PackageDir .\packages\TestPkg.Hello -OutDir .\dist

# Same plus create the GitHub Release via gh CLI:
.\scripts\publish-package.ps1 -PackageDir .\packages\TestPkg.Hello -OutDir .\dist -CreateRelease
```

Then paste the printed snippet into `index.json` and `git push`. The editor's
**Refresh registry** button picks up the new version on the next click.
