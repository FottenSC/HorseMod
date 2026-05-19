#requires -Version 5.1
<#
  build-cloudflare.ps1
  ---------------------------------------------------------------------
  Builds the SC6 moveset webui and packs the result into one .zip that
  can be drag-and-dropped straight into Cloudflare Pages:

      Cloudflare dashboard -> Workers & Pages -> Create -> Pages
      -> "Upload assets" -> drop sc6-moveset-webui.zip

  Run it with:   npm run pack:cf
            or:  powershell -ExecutionPolicy Bypass -File build-cloudflare.ps1
#>
$ErrorActionPreference = "Stop"

# Always operate from the webui folder (this script's own directory),
# regardless of where it was invoked from.
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $root
$dist = Join-Path $root "dist"
$zip  = Join-Path $root "sc6-moveset-webui.zip"

Write-Host ""
Write-Host "[1/3] Building (tsc --noEmit + vite build)..." -ForegroundColor Cyan
npm run build
if ($LASTEXITCODE -ne 0) {
    Write-Host "Build failed - aborting (no zip written)." -ForegroundColor Red
    exit 1
}
if (-not (Test-Path $dist)) {
    Write-Host "Build reported success but dist/ is missing - aborting." -ForegroundColor Red
    exit 1
}

# public/data is gitignored and generated locally (npm run export-data,
# which needs the SC6 dump). Vite copies it into dist/ - make sure it
# actually made it in, or the deployed site will have no moveset data.
$charsDir = Join-Path $dist "data\chars"
if (-not (Test-Path $charsDir)) {
    Write-Host "[2/3] WARNING: dist\data\chars is missing." -ForegroundColor Yellow
    Write-Host "      The deployed site will have NO moveset data." -ForegroundColor Yellow
    Write-Host "      Run 'npm run export-data' first, then re-run this script." -ForegroundColor Yellow
} else {
    $charCount = (Get-ChildItem $charsDir -Filter *.json -ErrorAction SilentlyContinue).Count
    Write-Host "[2/3] Bundled $charCount character data files." -ForegroundColor Cyan
}

Write-Host "[3/3] Zipping dist/ -> $(Split-Path -Leaf $zip)" -ForegroundColor Cyan
if (Test-Path $zip) { Remove-Item $zip -Force }
# Build the archive by hand with System.IO.Compression so every entry
# name uses FORWARD slashes ("assets/app.js"). Windows PowerShell's
# Compress-Archive writes backslash separators, which some extractors and
# static hosts mis-read as a flat filename - that silently breaks the
# deployed site. Entry paths are relative to dist/, so index.html /
# assets / data / _redirects all sit at the archive root (what
# Cloudflare needs - the site root at the top of the zip).
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$distFull = (Resolve-Path $dist).Path.TrimEnd('\')
$fileCount = 0
$fs = [System.IO.File]::Open($zip, [System.IO.FileMode]::Create)
try {
    $archive = New-Object System.IO.Compression.ZipArchive(
        $fs, [System.IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($f in Get-ChildItem -LiteralPath $dist -Recurse -File) {
            $rel = $f.FullName.Substring($distFull.Length + 1).Replace('\', '/')
            $entry = $archive.CreateEntry(
                $rel, [System.IO.Compression.CompressionLevel]::Optimal)
            $dst = $entry.Open()
            try {
                $src = [System.IO.File]::OpenRead($f.FullName)
                try { $src.CopyTo($dst) } finally { $src.Dispose() }
            } finally { $dst.Dispose() }
            $fileCount++
        }
    } finally { $archive.Dispose() }
} finally { $fs.Dispose() }
Write-Host "      Packed $fileCount files (forward-slash paths)." -ForegroundColor DarkGray

$sizeMB = "{0:N1}" -f ((Get-Item $zip).Length / 1MB)
Write-Host ""
Write-Host "Done." -ForegroundColor Green
Write-Host "  $zip  ($sizeMB MB)" -ForegroundColor Green
Write-Host ""
Write-Host "Upload it:  Cloudflare dashboard -> Workers & Pages -> Create" -ForegroundColor Green
Write-Host "            -> Pages -> 'Upload assets' -> drop the zip." -ForegroundColor Green
