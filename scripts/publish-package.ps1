# publish-package.ps1
#
# Zips a package source folder, computes its SHA-256, and prints the
# JSON snippet you paste into index.json. Optionally uploads the zip
# as a GitHub Release asset via `gh release create`.
#
# Usage:
#   .\scripts\publish-package.ps1 -PackageDir .\packages\TestPkg.Hello -OutDir .\dist
#   .\scripts\publish-package.ps1 -PackageDir .\packages\TestPkg.Hello -OutDir .\dist -CreateRelease
#
# The PackageDir MUST contain exactly one top-level folder named after the
# package (e.g. .\packages\TestPkg.Hello\TestPkg.Hello\index-package.lua).
# That folder is what becomes the zip's root entry, matching what the
# editor's `registry-download` subcommand expects.

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string] $PackageDir,
    [Parameter(Mandatory = $true)] [string] $OutDir,
    [switch] $CreateRelease,
    [string] $RepoSlug = "Ben-Scr/Index-PackageRegistry"
)

$ErrorActionPreference = "Stop"

$pkgDir = Resolve-Path $PackageDir
$outDir = if (Test-Path $OutDir) { Resolve-Path $OutDir } else { New-Item -ItemType Directory -Path $OutDir | Resolve-Path }

# Locate the single top-level folder containing index-package.lua.
$topFolders = Get-ChildItem -Directory $pkgDir
if ($topFolders.Count -ne 1) {
    throw "Expected exactly one top-level folder under '$pkgDir', found $($topFolders.Count)."
}
$topFolder = $topFolders[0]
$manifestPath = Join-Path $topFolder.FullName "index-package.lua"
if (-not (Test-Path $manifestPath)) {
    throw "No index-package.lua at '$manifestPath'."
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

# Zip <topFolder> so the zip contains <topFolder>/index-package.lua at root.
$zipName = "$pkgName-$pkgVersion.zip"
$zipPath = Join-Path $outDir $zipName
if (Test-Path $zipPath) { Remove-Item $zipPath }
Compress-Archive -Path $topFolder.FullName -DestinationPath $zipPath
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
