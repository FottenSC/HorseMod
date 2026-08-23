param(
    [string]$AssetRoot = 'C:\Users\prest\Documents\SoulcaliburModding\SCVI Sound Tools\dump\SoulcaliburVI',
    [string]$OutputRoot = (Join-Path $PSScriptRoot '..\..\dump\Battle\profile')
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$toolProject = Join-Path $repoRoot 'tools\BlueprintToCpp\BlueprintToCpp\Main.csproj'
$toolDll = Join-Path $repoRoot 'tools\BlueprintToCpp\BlueprintToCpp\bin\Release\net8.0\Main.dll'
$characterIds = @(
    '001','002','003','005','006','007','009','00B','00C','00D','00F','011','012',
    '014','015','016','017','023','024','028','030','060','061','062','064','065'
)

dotnet build $toolProject -c Release | Out-Host
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null

foreach ($cid in $characterIds) {
    $asset = Get-ChildItem -LiteralPath $AssetRoot -Filter "RP_$cid.uasset" -File -Recurse |
        Where-Object { $_.FullName -match '[\\/]Chara[\\/]RegularProfile[\\/]' } |
        Select-Object -First 1
    if ($null -eq $asset) {
        throw "RegularProfile RP_$cid.uasset was not found below $AssetRoot"
    }
    $relative = $asset.FullName.Substring($AssetRoot.Length).TrimStart('\').Replace('\', '/')
    $destBase = Join-Path $OutputRoot "RP_$($cid.ToLowerInvariant())"
    Copy-Item -LiteralPath $asset.FullName -Destination "$destBase.uasset" -Force
    $uexp = [IO.Path]::ChangeExtension($asset.FullName, '.uexp')
    if (-not (Test-Path -LiteralPath $uexp)) {
        throw "Companion file is missing: $uexp"
    }
    Copy-Item -LiteralPath $uexp -Destination "$destBase.uexp" -Force
    dotnet $toolDll --export-regular-profile $AssetRoot $relative "$destBase.json"
}
