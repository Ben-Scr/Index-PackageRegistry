# build-all-packages.ps1
#
# Wrapper around publish-package.ps1 that zips every package under
# packages/ into dist/. Each subfolder of packages/ that has an
# index-package.lua at its root is treated as a package and forwarded
# to publish-package.ps1.
#
# Usage:
#   .\scripts\build-all-packages.ps1
#   .\scripts\build-all-packages.ps1 -EngineRoot C:\path\to\Index
#   .\scripts\build-all-packages.ps1 -EngineRoot C:\path\to\Index -Configuration Release
#   .\scripts\build-all-packages.ps1 -CreateRelease
#
# Without -EngineRoot the script assumes each package already has its
# prebuilt DLLs under Bin/Windows-x64/ (publish-package.ps1 throws if
# they're missing for a layer the manifest declares).
#
# A failure on one package is reported but does not stop the run; a
# summary at the end lists what succeeded and what failed, and the
# script exits non-zero if anything failed.

[CmdletBinding()]
param(
    [string] $PackagesDir = (Join-Path $PSScriptRoot "..\packages"),
    [string] $OutDir = (Join-Path $PSScriptRoot "..\dist"),
    [string] $EngineRoot,
    [string] $Configuration = "Debug",
    [switch] $CreateRelease,
    [string] $RepoSlug = "Ben-Scr/Index-PackageRegistry"
)

$ErrorActionPreference = "Stop"

$publishScript = Join-Path $PSScriptRoot "publish-package.ps1"
if (-not (Test-Path $publishScript)) {
    throw "publish-package.ps1 not found at $publishScript."
}
if (-not (Test-Path $PackagesDir)) {
    throw "Packages directory not found: $PackagesDir"
}
if (-not (Test-Path $OutDir)) {
    New-Item -ItemType Directory -Path $OutDir | Out-Null
}

$pkgRoots = Get-ChildItem -Path $PackagesDir -Directory | Where-Object {
    Test-Path (Join-Path $_.FullName "index-package.lua")
} | Sort-Object Name

if ($pkgRoots.Count -eq 0) {
    Write-Host "No packages found in $PackagesDir (no subfolder with index-package.lua)." -ForegroundColor Yellow
    return
}

Write-Host "Found $($pkgRoots.Count) package(s) to build:" -ForegroundColor Cyan
$pkgRoots | ForEach-Object { Write-Host "  - $($_.Name)" }
Write-Host ""

$succeeded = @()
$failed = @()
foreach ($pkg in $pkgRoots) {
    Write-Host "================================================================" -ForegroundColor DarkCyan
    Write-Host " Building $($pkg.Name)" -ForegroundColor Cyan
    Write-Host "================================================================" -ForegroundColor DarkCyan

    $publishArgs = @{
        PackageDir    = $pkg.FullName
        OutDir        = $OutDir
        Configuration = $Configuration
        RepoSlug      = $RepoSlug
    }
    if ($EngineRoot)   { $publishArgs.EngineRoot = $EngineRoot }
    if ($CreateRelease) { $publishArgs.CreateRelease = $true }

    try {
        & $publishScript @publishArgs
        $succeeded += $pkg.Name
    } catch {
        Write-Host ""
        Write-Host "FAILED: $($pkg.Name) -- $_" -ForegroundColor Red
        $failed += $pkg.Name
    }
    Write-Host ""
}

Write-Host "================================================================" -ForegroundColor DarkCyan
Write-Host " Summary" -ForegroundColor Cyan
Write-Host "================================================================" -ForegroundColor DarkCyan
Write-Host "Output dir: $((Resolve-Path $OutDir).Path)"
Write-Host "Succeeded:  $($succeeded.Count)" -ForegroundColor Green
$succeeded | ForEach-Object { Write-Host "  + $_" -ForegroundColor Green }
if ($failed.Count -gt 0) {
    Write-Host "Failed:     $($failed.Count)" -ForegroundColor Red
    $failed | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
    exit 1
}
