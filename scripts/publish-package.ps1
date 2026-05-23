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
# The PackageDir IS the package folder — it must contain index-package.lua
# at its root. The folder name becomes the zip's top-level entry, matching
# what the editor's `registry-download` subcommand expects.

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
