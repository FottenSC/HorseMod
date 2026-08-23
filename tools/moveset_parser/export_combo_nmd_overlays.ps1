param(
    [string]$AssetRoot = 'C:\Users\prest\Documents\SoulcaliburModding\SCVI Sound Tools\dump\SoulcaliburVI',
    [string]$OutputRoot = (Join-Path $PSScriptRoot '..\..\dump\Battle\nmd\profile_overlays')
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$toolDll = Join-Path $repoRoot 'tools\BlueprintToCpp\BlueprintToCpp\bin\Release\net8.0\Main.dll'
$profileRoot = Join-Path $repoRoot 'dump\Battle\profile'
$characterIds = @(
    '001','002','003','005','006','007','009','00B','00C','00D','00F','011','012',
    '014','015','016','017','023','024','028','030','060','061','062','064','065'
)

New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null
foreach ($cid in $characterIds) {
    $profile = Get-Content -LiteralPath (Join-Path $profileRoot "RP_$cid.json") -Raw |
        ConvertFrom-Json
    $destination = Join-Path $OutputRoot $cid
    New-Item -ItemType Directory -Force -Path $destination | Out-Null
    $manifest = [System.Collections.Generic.List[object]]::new()
    $ordinal = 0
    foreach ($part in $profile.parts) {
        if ([string]::IsNullOrWhiteSpace($part.Part)) { continue }
        $objectPath = [string]$part.Part
        $packageObject = $objectPath.Substring(6)
        $dot = $packageObject.LastIndexOf('.')
        if ($dot -ge 0) { $packageObject = $packageObject.Substring(0, $dot) }
        $packagePath = "Content/$packageObject.uasset"
        $partJson = Join-Path $env:TEMP "sc6_combo_part_$cid`_$ordinal.json"
        dotnet $toolDll --export-package-json $AssetRoot $packagePath $partJson | Out-Host
        $partExports = Get-Content -LiteralPath $partJson -Raw | ConvertFrom-Json
        Remove-Item -LiteralPath $partJson
        foreach ($entry in $partExports) {
            foreach ($mesh in $entry.Properties.DefaultMeshes) {
                if ([string]::IsNullOrWhiteSpace($mesh.NMDPath)) { continue }
                $sourceNmd = Join-Path (Join-Path $AssetRoot 'Content') $mesh.NMDPath
                if (-not (Test-Path -LiteralPath $sourceNmd)) {
                    throw "NMD path from $packagePath is missing: $sourceNmd"
                }
                $fileName = '{0:D3}_{1}' -f $ordinal, [IO.Path]::GetFileName($sourceNmd)
                Copy-Item -LiteralPath $sourceNmd -Destination (Join-Path $destination $fileName) -Force
                $manifest.Add([ordered]@{
                    ordinal = $ordinal
                    partCategory = $part.Type
                    partPackage = $packagePath
                    nmdPath = $mesh.NMDPath
                    localFile = $fileName
                })
                $ordinal++
            }
        }
    }
    $manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $destination 'manifest.json') -Encoding utf8NoBOM
}
