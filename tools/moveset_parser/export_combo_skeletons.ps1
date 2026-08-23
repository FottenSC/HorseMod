param(
    [Parameter(Mandatory = $true)]
    [string] $SourceDump,
    [string] $BattleDump = "E:\myMods\dump\Battle"
)

$ErrorActionPreference = "Stop"
$sourceRoot = [System.IO.Path]::GetFullPath($SourceDump)
$battleRoot = [System.IO.Path]::GetFullPath($BattleDump)
$binaryRoot = Join-Path $battleRoot "skeleton_source"
$jsonRoot = Join-Path $battleRoot "skeleton"
$extractor = "E:\myMods\tools\BlueprintToCpp\BlueprintToCpp\bin\Release\net8.0\Main.dll"

$packages = [ordered]@{
    "001" = "SoulcaliburVI/Content/Chara/001/r_all_001_Skeleton.uasset"
    "002" = "SoulcaliburVI/Content/Chara/002/r_all_002_Skeleton.uasset"
    "003" = "SoulcaliburVI/Content/Chara/003/r_all_003_Skeleton.uasset"
    "005" = "SoulcaliburVI/Content/Chara/005/r_all_005_Skeleton.uasset"
    "006" = "SoulcaliburVI/Content/Chara/006/r_all_006_Skeleton.uasset"
    "007" = "SoulcaliburVI/Content/Chara/007/r_all_007_Skeleton.uasset"
    "009" = "SoulcaliburVI/Content/DLC/13/Chara/009/r_all_009_Skeleton.uasset"
    "00b" = "SoulcaliburVI/Content/Chara/00b/r_all_00b_Skeleton.uasset"
    "00c" = "SoulcaliburVI/Content/Chara/00c/r_all_00c_Skeleton.uasset"
    "00d" = "SoulcaliburVI/Content/Chara/00d/r_all_00d_Skeleton.uasset"
    "00f" = "SoulcaliburVI/Content/Chara/00f/r_all_00f_Skeleton.uasset"
    "011" = "SoulcaliburVI/Content/Chara/011/r_all_011_Skeleton.uasset"
    "012" = "SoulcaliburVI/Content/Chara/012/r_all_012_Skeleton.uasset"
    "014" = "SoulcaliburVI/Content/Chara/014/r_all_014_Skeleton.uasset"
    "015" = "SoulcaliburVI/Content/Chara/015/r_all_015_Skeleton.uasset"
    "016" = "SoulcaliburVI/Content/Chara/016/r_all_016_Skeleton.uasset"
    "017" = "SoulcaliburVI/Content/DLC/06/Chara/017/r_all_017_Skeleton.uasset"
    "023" = "SoulcaliburVI/Content/Chara/023/r_all_023_Skeleton.uasset"
    "024" = "SoulcaliburVI/Content/Chara/024/r_all_024_Skeleton.uasset"
    "028" = "SoulcaliburVI/Content/DLC/07/Chara/028/r_all_028_Skeleton.uasset"
    "030" = "SoulcaliburVI/Content/DLC/04/Chara/030/r_all_030_Skeleton.uasset"
    "060" = "SoulcaliburVI/Content/DLC/01/Chara/060/r_all_060_Skeleton.uasset"
    # Haohmaru's dump has no standalone r_all_061_Skeleton package.  This
    # Haohmaru has no standalone r_all skeleton.  Shipped modular meshes are
    # composed below to cover the same compact KHit reference profile.
    "061" = "SoulcaliburVI/Content/DLC/09/Chara/061/nude_r061_m_H_Chest.uasset"
    "062" = "SoulcaliburVI/Content/Chara/062/r_all_062_Skeleton.uasset"
    "064" = "SoulcaliburVI/Content/Chara/064/r_all_064_Skeleton.uasset"
    "065" = "SoulcaliburVI/Content/Chara/065/r_all_065_Skeleton.uasset"
}

New-Item -ItemType Directory -Force -Path $binaryRoot, $jsonRoot | Out-Null

foreach ($entry in $packages.GetEnumerator()) {
    $cid = $entry.Key
    $package = $entry.Value
    $sourceAsset = Join-Path $sourceRoot $package.Replace('/', '\')
    $sourceBulk = [System.IO.Path]::ChangeExtension($sourceAsset, ".uexp")
    if (-not (Test-Path -LiteralPath $sourceAsset) -or
        -not (Test-Path -LiteralPath $sourceBulk)) {
        throw "Missing cooked skeleton pair for ${cid}: $package"
    }

    Copy-Item -LiteralPath $sourceAsset -Destination (Join-Path $binaryRoot "skeleton${cid}.uasset") -Force
    Copy-Item -LiteralPath $sourceBulk -Destination (Join-Path $binaryRoot "skeleton${cid}.uexp") -Force
    & dotnet $extractor --export-skeleton $sourceRoot $package (Join-Path $jsonRoot "skeleton${cid}.json")
    if ($LASTEXITCODE -ne 0) {
        throw "Skeleton export failed for ${cid}: $package"
    }
}

$haohSupplements = [ordered]@{
    "neck" = "SoulcaliburVI/Content/DLC/09/Chara/061/nude_r061_m_H_Neck.uasset"
    "forearm" = "SoulcaliburVI/Content/DLC/09/Chara/061/nude_r061_m_H_ForeArm.uasset"
    "lower" = "SoulcaliburVI/Content/DLC/09/Chara/061/lower_r061_m_H_Hip.uasset"
}
foreach ($entry in $haohSupplements.GetEnumerator()) {
    $part = $entry.Key
    $package = $entry.Value
    $asset = Join-Path $sourceRoot $package.Replace('/', '\')
    $bulk = [System.IO.Path]::ChangeExtension($asset, ".uexp")
    Copy-Item -LiteralPath $asset -Destination (Join-Path $binaryRoot "skeleton061_${part}.uasset") -Force
    Copy-Item -LiteralPath $bulk -Destination (Join-Path $binaryRoot "skeleton061_${part}.uexp") -Force
    & dotnet $extractor --export-skeleton $sourceRoot $package (Join-Path $jsonRoot "skeleton061_${part}.json")
    if ($LASTEXITCODE -ne 0) {
        throw "Skeleton export failed for Haohmaru ${part} supplement: $package"
    }
}

Write-Host "Exported $($packages.Count) static collision-pose skeletons to $jsonRoot"
