[CmdletBinding()]
param(
    [string]$Query, [string]$Address, [string]$Calls, [string]$CalledBy, [string]$String, [string]$Global,
    [string]$Area, [string]$Module, [string]$Family, [string]$Origin, [string]$Tag,
    [ValidateRange(1, 500)][int]$Limit = 50, [switch]$Open
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$current = Get-Content -LiteralPath (Join-Path $root 'current.json') -Raw | ConvertFrom-Json
$generationId = [string]$current.generation_id
if ($generationId -notmatch '^[0-9a-f]{24}$') { throw "current.json contains an unsafe generation ID." }
$generationsRoot = [IO.Path]::GetFullPath((Join-Path $root 'generations'))
$generation = [IO.Path]::GetFullPath((Join-Path $generationsRoot $generationId))
if (-not $generation.StartsWith($generationsRoot.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) { throw "Current generation escapes generations root." }
$expectedSolution = "generations/$generationId/GhidraCalibur.sln"
$expectedManifest = "generations/$generationId/content_manifest.json"
if ([string]$current.solution -ne $expectedSolution -or [string]$current.manifest -ne $expectedManifest) { throw "current.json does not point to its declared immutable generation." }
$solution = Join-Path $generation 'GhidraCalibur.sln'
$manifest = Join-Path $generation 'content_manifest.json'
$complete = Join-Path $generation '.complete'
if (-not (Test-Path -LiteralPath $solution -PathType Leaf) -or -not (Test-Path -LiteralPath $manifest -PathType Leaf) -or -not (Test-Path -LiteralPath $complete -PathType Leaf)) { throw "Current GhidraCalibur generation is incomplete: $generation" }
if ((Get-Content -LiteralPath $complete -Raw).Trim() -ne $generationId) { throw "Current GhidraCalibur completion marker does not match its generation ID." }
$python = Get-Command py.exe -ErrorAction SilentlyContinue
if ($python) { $pythonArgs = @('-3.11', (Join-Path $PSScriptRoot 'query_ghidra_calibur.py')) } else { $python = Get-Command python.exe -ErrorAction Stop; $pythonArgs = @((Join-Path $PSScriptRoot 'query_ghidra_calibur.py')) }
$cacheRoot = Join-Path $env:LOCALAPPDATA 'GhidraCalibur\cache'
foreach ($pair in @{ '--query'=$Query; '--address'=$Address; '--calls'=$Calls; '--called-by'=$CalledBy; '--string'=$String; '--global'=$Global; '--area'=$Area; '--module'=$Module; '--family'=$Family; '--origin'=$Origin; '--tag'=$Tag }.GetEnumerator()) { if ($pair.Value) { $pythonArgs += @($pair.Key, $pair.Value) } }
$pythonArgs += @('--generation', $generation, '--cache-root', $cacheRoot, '--limit', $Limit)
$json = & $python.Source @pythonArgs
if ($LASTEXITCODE -ne 0) { throw "GhidraCalibur query failed with exit code $LASTEXITCODE." }
$results = $json | ConvertFrom-Json
$results | Format-List address_space,address,name,qualified_name,signature,area,module,family,origin,browse_file,browse_line,tags,callers,callees,globals,strings
if ($Open -and $results -and $results[0].browse_file) {
    $browseFile = [IO.Path]::GetFullPath((Join-Path $generation ([string]$results[0].browse_file)))
    if (-not $browseFile.StartsWith($generation.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase) -or -not (Test-Path -LiteralPath $browseFile -PathType Leaf)) { throw "Search result has an unsafe or unavailable browse file." }
    Start-Process -FilePath $browseFile
}
