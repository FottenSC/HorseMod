param(
    [switch]$SkipExport,
    [switch]$KeepWorkCopy,
    [switch]$KeepRawExport,
    [switch]$KeepLogs,
    [int]$TimeoutSeconds = 5
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")
$rootPath = $root.Path
$exportDir = Join-Path $rootPath "exported"
$rawCpp = Join-Path $exportDir "sc6_decompiled.cpp"
$rawHeader = Join-Path $exportDir "sc6_decompiled.h"

if (-not $SkipExport) {
    & (Join-Path $PSScriptRoot "export_ghidra_calibur.ps1") -TimeoutSeconds $TimeoutSeconds -KeepWorkCopy:$KeepWorkCopy
}
elseif (-not (Test-Path -LiteralPath $rawCpp)) {
    throw "Cannot rebuild browse tree with -SkipExport because $rawCpp is missing. Run without -SkipExport, or use -KeepRawExport on a previous refresh."
}

python (Join-Path $PSScriptRoot "build_browse_tree.py")

if (-not $KeepRawExport) {
    Remove-Item -LiteralPath $rawCpp, $rawHeader -ErrorAction SilentlyContinue
    Write-Host "Removed raw CppExporter files. Use -KeepRawExport to retain them for -SkipExport rebuilds."
}

if (-not $KeepLogs) {
    Remove-Item -Path (Join-Path $exportDir "*.log") -ErrorAction SilentlyContinue
    Write-Host "Removed export logs. Use -KeepLogs to retain them after a successful refresh."
}
