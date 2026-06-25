param(
    [string]$GhidraRoot = "E:\DevShitPosts\SC6Mods\Ghidra",
    [string]$ProjectPath = "E:\DevShitPosts\SC6Mods\SCVI_Modding",
    [string]$ProjectName = "SCVI_Modding",
    [string]$ProgramName = "SoulcaliburVI.exe",
    [int]$TimeoutSeconds = 5,
    [switch]$KeepWorkCopy,
    [switch]$MetadataOnly
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")
$rootPath = $root.Path
$exportDir = Join-Path $rootPath "exported"
$workRoot = Join-Path $rootPath "work"
$workProjectPath = Join-Path $workRoot $ProjectName
$analyzeHeadless = Join-Path $GhidraRoot "support\analyzeHeadless.bat"
$cppOutput = Join-Path $exportDir "sc6_decompiled.cpp"
$functionIndex = Join-Path $exportDir "functions.csv"
$headlessLog = Join-Path $exportDir "headless_export.log"
$scriptLog = Join-Path $exportDir "cpp_exporter_script.log"
$stdoutLog = Join-Path $exportDir "headless_export.out.log"
$stderrLog = Join-Path $exportDir "headless_export.err.log"

function Remove-TreeWithinRoot {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$AllowedRoot
    )

    $resolved = Resolve-Path -LiteralPath $Path -ErrorAction SilentlyContinue
    if (-not $resolved) {
        return
    }

    $full = $resolved.Path
    $allowed = [System.IO.Path]::GetFullPath($AllowedRoot)
    if (-not $full.StartsWith($allowed, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove path outside allowed root: $full"
    }

    Remove-Item -LiteralPath $full -Recurse -Force
}

if (-not (Test-Path -LiteralPath $analyzeHeadless)) {
    throw "analyzeHeadless.bat not found: $analyzeHeadless"
}

New-Item -ItemType Directory -Force -Path $exportDir | Out-Null
New-Item -ItemType Directory -Force -Path $workRoot | Out-Null

Remove-TreeWithinRoot -Path $workProjectPath -AllowedRoot $workRoot

Write-Host "Copying Ghidra project to temporary work area..."
robocopy $ProjectPath $workProjectPath /E /R:1 /W:1 /NFL /NDL /NP /XF *.lock *.lock~
$robocopyExit = $LASTEXITCODE
if ($robocopyExit -ge 8) {
    throw "robocopy failed with exit code $robocopyExit"
}

Remove-Item -LiteralPath $headlessLog, $scriptLog, $stdoutLog, $stderrLog -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $functionIndex -ErrorAction SilentlyContinue
if (-not $MetadataOnly) {
    Remove-Item -LiteralPath $cppOutput -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath (Join-Path $exportDir "sc6_decompiled.h") -ErrorAction SilentlyContinue
}

if ($MetadataOnly) {
    $postScript = "ExportFunctionIndex.java"
    $postArgs = @($functionIndex)
}
else {
    $postScript = "RunCppExporter.java"
    $postArgs = @($cppOutput, "$TimeoutSeconds", $functionIndex)
}

$arguments = @(
    $workProjectPath,
    $ProjectName,
    "-process", $ProgramName,
    "-readOnly",
    "-noanalysis",
    "-scriptPath", (Join-Path $rootPath "tools"),
    "-postScript", $postScript
) + $postArgs + @(
    "-log", $headlessLog,
    "-scriptlog", $scriptLog
)

try {
    Write-Host "Running Ghidra headless export..."
    $process = Start-Process -FilePath $analyzeHeadless `
        -ArgumentList $arguments `
        -WindowStyle Hidden `
        -PassThru `
        -RedirectStandardOutput $stdoutLog `
        -RedirectStandardError $stderrLog
    $process.WaitForExit()
    $process.Refresh()

    if ($null -ne $process.ExitCode -and $process.ExitCode -ne 0) {
        throw "analyzeHeadless failed with exit code $($process.ExitCode). See $headlessLog"
    }

    if ($MetadataOnly) {
        $successPattern = "Function index finished"
    }
    else {
        $successPattern = "CppExporter finished"
    }

    if (-not (Select-String -Path $headlessLog -Pattern $successPattern -Quiet)) {
        throw "Missing success marker '$successPattern' in $headlessLog"
    }

    Write-Host "Export completed."
    Write-Host "Function index: $functionIndex"
    if (-not $MetadataOnly) {
        Write-Host "CppExporter output: $cppOutput"
    }
}
finally {
    if (-not $KeepWorkCopy) {
        Write-Host "Removing temporary Ghidra project copy..."
        Remove-TreeWithinRoot -Path $workProjectPath -AllowedRoot $workRoot
    }
}
