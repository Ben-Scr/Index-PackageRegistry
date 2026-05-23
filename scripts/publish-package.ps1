# publish-package.ps1
#
# Publishes an Index package to the cloud registry. The Index editor pulls
# the registry's index.json + the matching release zip, extracts the zip
# into <user-project>/Packages/<Name>/, and references the precompiled
# DLLs the zip ships under Bin/Windows-x64/ — no build step on the user
# machine, Unity-style.
#
# Modes:
#   1) -EngineRoot <path>    Auto-build the package via the engine repo's
#                            premake + msbuild pipeline, then zip.
#   2) (no -EngineRoot)      Manual mode — assumes you've already placed
#                            the prebuilt DLL(s) at <PackageDir>/Bin/
#                            Windows-x64/Pkg.<Name>(.Native).dll yourself.
#
# Usage:
#   .\scripts\publish-package.ps1 -PackageDir .\packages\Index.Cryptography -OutDir .\dist `
#                                  -EngineRoot C:\path\to\Index
#   .\scripts\publish-package.ps1 -PackageDir .\packages\Index.Cryptography -OutDir .\dist -CreateRelease
#
# The PackageDir IS the package folder — it must contain index-package.lua
# at its root. The folder name becomes the zip's top-level entry, matching
# what the editor's `registry-download` subcommand expects.

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string] $PackageDir,
    [Parameter(Mandatory = $true)] [string] $OutDir,
    [string] $EngineRoot,
    [string] $Configuration = "Debug",
    [switch] $CreateRelease,
    [string] $RepoSlug = "Ben-Scr/Index-PackageRegistry"
)

$ErrorActionPreference = "Stop"

$pkgDir = Resolve-Path $PackageDir
$outDir = if (Test-Path $OutDir) { Resolve-Path $OutDir } else { New-Item -ItemType Directory -Path $OutDir | Resolve-Path }

$manifestPath = Join-Path $pkgDir "index-package.lua"
if (-not (Test-Path $manifestPath)) {
    throw "No index-package.lua at '$manifestPath'. PackageDir should be the package folder itself, not a wrapper around it."
}

# Parse name + version out of the manifest (loose regex; same idea as the
# Index-PackageTool's ExtractLuaField helper).
$manifestText = Get-Content $manifestPath -Raw
$nameMatch = [regex]::Match($manifestText, '\bname\s*=\s*"([^"]+)"')
$verMatch  = [regex]::Match($manifestText, '\bversion\s*=\s*"([^"]+)"')
if (-not $nameMatch.Success -or -not $verMatch.Success) {
    throw "Could not parse name/version from $manifestPath."
}
$pkgName = $nameMatch.Groups[1].Value
$pkgVersion = $verMatch.Groups[1].Value

# Detect which layers the manifest declares so the registry entry can carry
# layer badges (the editor's Search tab shows them BEFORE download). Mirrors
# the regex logic in IndexPackageInstaller::ReadManifest.
$layers = @()
$hasNative = $false
$hasCsharp = $false
if ($manifestText -match '(?ms)(?:^|[\s,;{])(?:native|engine_core)\s*=\s*\{') {
    $layers += "native"; $hasNative = $true
}
if ($manifestText -match '(?ms)(?:^|[\s,;{])(?:native_standalone|standalone_cpp)\s*=\s*\{') {
    $layers += "native_standalone"; $hasNative = $true
}
if ($manifestText -match '(?ms)(?:^|[\s,;{])csharp\s*=\s*\{') {
    $layers += "csharp"; $hasCsharp = $true
}

# Optional engine-build step: synthesize the package's prebuilt DLLs by
# briefly hosting it in the engine repo's packages/ folder, regenerating
# premake, building the Pkg.<Name>(.Native) targets, then copying the
# outputs back into <PackageDir>/Bin/Windows-x64/. We restore the engine's
# packages/ state on the way out so the engine repo never carries the
# package long-term.
if ($EngineRoot) {
    $engineRootFull = Resolve-Path $EngineRoot
    Write-Host ""
    Write-Host "=== Engine build via $engineRootFull ===" -ForegroundColor Cyan
    $premake = Join-Path $engineRootFull "vendor\bin\premake5.exe"
    if (-not (Test-Path $premake)) {
        throw "premake5.exe not found at ${premake}. Supply a valid -EngineRoot."
    }
    $stagedDir = Join-Path $engineRootFull "packages\$pkgName"
    if (Test-Path $stagedDir) { Remove-Item $stagedDir -Recurse -Force }
    Copy-Item $pkgDir $stagedDir -Recurse
    try {
        Push-Location $engineRootFull
        try {
            & $premake vs2022 | Out-Null
            if ($LASTEXITCODE -ne 0) { throw "premake regen failed (exit $LASTEXITCODE)." }
        } finally {
            Pop-Location
        }

        # Build whichever Pkg.<Name>(.Native) project files premake produced.
        $generated = Join-Path $engineRootFull "premake\generated"
        $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
        if (-not (Test-Path $vswhere)) { $vswhere = "${env:ProgramFiles}\Microsoft Visual Studio\Installer\vswhere.exe" }
        $msbuild = & $vswhere -latest -prerelease -products * -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe" | Select-Object -First 1
        if (-not $msbuild) { throw "MSBuild not found via vswhere." }

        foreach ($projInfo in @(
            @{ Name = "Pkg.$pkgName"        ; Ext = ".csproj"  },
            @{ Name = "Pkg.$pkgName.Native" ; Ext = ".vcxproj" }
        )) {
            $projFile = Join-Path $generated ("$($projInfo.Name)\$($projInfo.Name)$($projInfo.Ext)")
            if (Test-Path $projFile) {
                Write-Host "Building $($projInfo.Name) ($Configuration)..."
                & $msbuild $projFile "/p:Configuration=$Configuration" "/p:Platform=x64" /nologo /v:minimal /m
                if ($LASTEXITCODE -ne 0) { throw "MSBuild failed for $($projInfo.Name) (exit $LASTEXITCODE)." }
            }
        }

        # Copy resulting DLLs into <PackageDir>/Bin/Windows-x64/. Only the
        # package's own artifacts ship; the user project already has its own
        # Index-ScriptCore.dll under Packages/Index-ScriptCore/, so we must
        # NOT redistribute it (CopyLocalLockFileAssemblies on the per-package
        # csproj leaves Index-ScriptCore.dll in the build output dir, and
        # naively grabbing *.dll would bloat every zip by ~500 KB).
        $binDir = Join-Path $pkgDir "Bin\Windows-x64"
        if (Test-Path $binDir) { Get-ChildItem $binDir -File | Remove-Item -Force }
        New-Item -ItemType Directory -Path $binDir -Force | Out-Null
        $engineBin = Join-Path $engineRootFull "bin\$Configuration-windows-x86_64"
        foreach ($subdir in @("Pkg.$pkgName", "Pkg.$pkgName.Native")) {
            $srcDir = Join-Path $engineBin $subdir
            if (Test-Path $srcDir) {
                $expected = @("$subdir.dll", "$subdir.pdb")
                foreach ($name in $expected) {
                    $src = Join-Path $srcDir $name
                    if (Test-Path $src) {
                        Copy-Item $src -Destination $binDir -Force
                        Write-Host "  $name -> $binDir"
                    }
                }
            }
        }
    } finally {
        # Always clean up the staged copy so the engine repo stays pristine.
        if (Test-Path $stagedDir) { Remove-Item $stagedDir -Recurse -Force }
    }
}

# Verify the prebuilt DLLs are present for the layers the manifest declares.
# This catches "publisher forgot to build" before we publish a broken zip.
$binDir = Join-Path $pkgDir "Bin\Windows-x64"
$missing = @()
if ($hasCsharp) {
    $expected = Join-Path $binDir "Pkg.$pkgName.dll"
    if (-not (Test-Path $expected)) { $missing += $expected }
}
if ($hasNative) {
    $expected = Join-Path $binDir "Pkg.$pkgName.Native.dll"
    if (-not (Test-Path $expected)) { $missing += $expected }
}
if ($missing.Count -gt 0) {
    Write-Host ""
    Write-Host "Missing prebuilt DLL(s):" -ForegroundColor Red
    $missing | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }
    Write-Host ""
    Write-Host "Either build the package and place the DLLs at the paths above," -ForegroundColor Yellow
    Write-Host "or re-run with -EngineRoot followed by the path to the engine repo to auto-build." -ForegroundColor Yellow
    throw "Cannot publish a package without its prebuilt DLLs."
}

# Zip the package folder so the resulting zip has <pkgFolderName>/index-package.lua at its root.
# We build entries by hand instead of using PowerShell's Compress-Archive OR
# .NET's ZipFile.CreateFromDirectory — both produce backslash-separated entry
# names on Windows, which violates the ZIP spec (PKWARE APPNOTE.TXT §4.4.17.1
# requires forward slashes) and trips up cross-platform unzippers.
$zipName = "$pkgName-$pkgVersion.zip"
$zipPath = Join-Path $outDir $zipName
if (Test-Path $zipPath) { Remove-Item $zipPath }
Add-Type -Assembly System.IO.Compression
Add-Type -Assembly System.IO.Compression.FileSystem
$baseName = Split-Path $pkgDir -Leaf
$pkgDirFull = (Resolve-Path $pkgDir).Path
$archive = [System.IO.Compression.ZipFile]::Open($zipPath, [System.IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($file in Get-ChildItem -Path $pkgDir -Recurse -File) {
        $rel = $file.FullName.Substring($pkgDirFull.Length).TrimStart('\','/').Replace('\','/')
        $entryName = "$baseName/$rel"
        $entry = $archive.CreateEntry($entryName, [System.IO.Compression.CompressionLevel]::Optimal)
        $entryStream = $entry.Open()
        try {
            $fileStream = [System.IO.File]::OpenRead($file.FullName)
            try { $fileStream.CopyTo($entryStream) } finally { $fileStream.Dispose() }
        } finally { $entryStream.Dispose() }
    }
} finally {
    $archive.Dispose()
}
$sha = (Get-FileHash $zipPath -Algorithm SHA256).Hash.ToLower()
$size = (Get-Item $zipPath).Length

Write-Host ""
Write-Host "Packaged: $zipPath" -ForegroundColor Green
Write-Host "SHA-256:  $sha"
Write-Host "Size:     $size bytes"

$tag = "$pkgName-$pkgVersion"
$downloadUrl = "https://github.com/$RepoSlug/releases/download/$tag/$zipName"

$entry = [pscustomobject]@{
    name        = $pkgName
    version     = $pkgVersion
    description = ""
    downloadUrl = $downloadUrl
    sha256      = $sha
    sizeBytes   = $size
    layers      = $layers
    dependencies = @()
}

Write-Host ""
Write-Host "index.json entry:" -ForegroundColor Cyan
$entry | ConvertTo-Json -Depth 4

if ($CreateRelease) {
    if (-not (Get-Command gh -ErrorAction SilentlyContinue)) {
        throw "gh CLI not found. Install from https://cli.github.com to use -CreateRelease."
    }
    Write-Host ""
    Write-Host "Creating GitHub Release $tag on $RepoSlug..." -ForegroundColor Yellow
    & gh release create $tag $zipPath --repo $RepoSlug --title $tag --notes "Auto-published by publish-package.ps1"
}
