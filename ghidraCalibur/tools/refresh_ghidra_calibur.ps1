param(
    [string]$GhidraRoot = "E:\DevShitPosts\SC6Mods\Ghidra",
    [string]$ProjectPath = "E:\DevShitPosts\SC6Mods\SCVI_Modding",
    [string]$ProjectName = "SCVI_Modding",
    [string]$ProgramName = "SoulcaliburVI.exe",
    [string]$McpBaseUrl = "http://127.0.0.1:8089",
    [int]$TimeoutSeconds = 5,
    [switch]$AllowPartial,
    [switch]$KeepStaging,
    [switch]$OpenWorkspace
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version 2.0

$script:Root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
$script:StagingRoot = Join-Path $script:Root ".staging"
$script:GenerationsRoot = Join-Path $script:Root "generations"
$script:CurrentPointer = Join-Path $script:Root "current.json"
$script:LockStream = $null
$script:RunRoot = $null
$script:RunStartedUtc = [DateTime]::UtcNow
$script:Succeeded = $false

function Invoke-NativeChecked {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [string]$Description = "native command",
        [string]$OutputPath
    )

    if ($OutputPath) {
        & $FilePath @Arguments *> $OutputPath
    }
    else {
        & $FilePath @Arguments
    }
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) {
        throw "$Description failed with exit code $exitCode"
    }
}

function Resolve-Python311 {
    $py = Get-Command "py.exe" -ErrorAction SilentlyContinue
    if ($py) {
        & $py.Source -3.11 -c "import sys; assert sys.version_info[:2] == (3, 11); print(sys.executable)" | Out-Null
        if ($LASTEXITCODE -eq 0) {
            return [PSCustomObject]@{ FilePath = $py.Source; Prefix = @("-3.11") }
        }
    }
    $python = Get-Command "python.exe" -ErrorAction SilentlyContinue
    if ($python) {
        & $python.Source -c "import sys; raise SystemExit(0 if sys.version_info[:2] == (3, 11) else 1)"
        if ($LASTEXITCODE -eq 0) {
            return [PSCustomObject]@{ FilePath = $python.Source; Prefix = @() }
        }
    }
    throw "Python 3.11 was not found. Install it or expose it through py.exe/python.exe."
}

function Invoke-McpGet {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [int]$TimeoutSec = 30
    )
    $separator = if ($Path.Contains("?")) { "&" } else { "?" }
    $program = [Uri]::EscapeDataString($ProgramName)
    $uri = $McpBaseUrl.TrimEnd("/") + $Path + $separator + "program=" + $program
    try {
        return Invoke-RestMethod -Method Get -Uri $uri -TimeoutSec $TimeoutSec
    }
    catch {
        throw "MCP request failed closed: GET $uri : $($_.Exception.Message)"
    }
}

function Invoke-McpPost {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][System.Collections.IDictionary]$Body,
        [int]$TimeoutSec = 300
    )
    $program = [Uri]::EscapeDataString($ProgramName)
    $uri = $McpBaseUrl.TrimEnd("/") + $Path + "?program=" + $program
    try {
        return Invoke-RestMethod -Method Post -Uri $uri -TimeoutSec $TimeoutSec `
            -ContentType "application/json" -Body ($Body | ConvertTo-Json -Depth 8)
    }
    catch {
        throw "MCP request failed closed: POST $uri : $($_.Exception.Message)"
    }
}

function Remove-TreeWithin {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$AllowedRoot
    )
    if (-not (Test-Path -LiteralPath $Path)) { return }
    $resolved = (Resolve-Path -LiteralPath $Path).Path
    $allowed = (Resolve-Path -LiteralPath $AllowedRoot).Path.TrimEnd("\")
    $relative = $resolved.Substring($allowed.Length).TrimStart("\")
    if (-not $resolved.StartsWith($allowed + "\", [StringComparison]::OrdinalIgnoreCase) -or
        [string]::IsNullOrWhiteSpace($relative)) {
        throw "Refusing to remove path outside allowed root: $resolved"
    }
    $item = Get-Item -LiteralPath $resolved -Force
    if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Refusing to recursively remove reparse point: $resolved"
    }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}

function Acquire-RefreshLock {
    New-Item -ItemType Directory -Force -Path $script:StagingRoot | Out-Null
    $lockPath = Join-Path $script:StagingRoot "refresh.lock"
    try {
        $script:LockStream = New-Object IO.FileStream(
            $lockPath,
            [IO.FileMode]::OpenOrCreate,
            [IO.FileAccess]::ReadWrite,
            [IO.FileShare]::None
        )
        $payload = [Text.Encoding]::UTF8.GetBytes("pid=$PID started=$([DateTime]::UtcNow.ToString('o'))`n")
        $script:LockStream.SetLength(0)
        $script:LockStream.Write($payload, 0, $payload.Length)
        $script:LockStream.Flush($true)
    }
    catch {
        throw "Another GhidraCalibur refresh appears to be running ($lockPath)."
    }
}

function Get-PreviousManifest {
    if (-not (Test-Path -LiteralPath $script:CurrentPointer)) { return $null }
    try {
        $pointer = Get-Content -LiteralPath $script:CurrentPointer -Raw | ConvertFrom-Json
        $generation = [IO.Path]::GetFileName([string]$pointer.generation_id)
        if ($generation -ne [string]$pointer.generation_id) { return $null }
        $manifest = Join-Path (Join-Path $script:GenerationsRoot $generation) "content_manifest.json"
        if (Test-Path -LiteralPath $manifest) { return $manifest }
    }
    catch {
        Write-Warning "Ignoring unreadable previous current.json: $($_.Exception.Message)"
    }
    return $null
}

function Set-CurrentPointer {
    param([Parameter(Mandatory = $true)][System.Collections.IDictionary]$Value)
    $temporary = "$($script:CurrentPointer).$PID.tmp"
    $backup = "$($script:CurrentPointer).bak"
    [IO.File]::WriteAllText(
        $temporary,
        (($Value | ConvertTo-Json -Depth 6) + "`n"),
        (New-Object Text.UTF8Encoding($false))
    )
    if (Test-Path -LiteralPath $script:CurrentPointer) {
        [IO.File]::Replace($temporary, $script:CurrentPointer, $backup, $true)
        Remove-Item -LiteralPath $backup -Force -ErrorAction SilentlyContinue
    }
    else {
        [IO.File]::Move($temporary, $script:CurrentPointer)
    }
}

function Remove-OldGenerations {
    param([Parameter(Mandatory = $true)][string]$CurrentGeneration)
    if (-not (Test-Path -LiteralPath $script:GenerationsRoot)) { return }
    $complete = Get-ChildItem -LiteralPath $script:GenerationsRoot -Directory | Where-Object {
        Test-Path -LiteralPath (Join-Path $_.FullName ".complete")
    } | Sort-Object LastWriteTimeUtc -Descending
    $keep = New-Object System.Collections.Generic.HashSet[string]([StringComparer]::OrdinalIgnoreCase)
    [void]$keep.Add($CurrentGeneration)
    # Retain the current immutable generation plus no more than two other complete
    # generations.  The current directory's timestamp can change when a user opens
    # an older workspace, so it must not be counted as one of the timestamp picks.
    foreach ($item in ($complete | Where-Object { $_.Name -ne $CurrentGeneration } | Select-Object -First 2)) {
        [void]$keep.Add($item.Name)
    }
    foreach ($item in $complete) {
        if ($keep.Contains($item.Name)) { continue }
        try {
            Remove-TreeWithin -Path $item.FullName -AllowedRoot $script:GenerationsRoot
        }
        catch {
            Write-Warning "Skipping locked or unsafe old generation '$($item.Name)': $($_.Exception.Message)"
        }
    }
}

function Find-MSBuild {
    $command = Get-Command "MSBuild.exe" -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere) {
        $results = @(& $vswhere -latest -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe")
        $exitCode = $LASTEXITCODE
        if ($exitCode -ne 0) {
            throw "vswhere failed with exit code $exitCode while locating MSBuild."
        }
        $result = $results | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
        if ($result) { return [string]$result }
    }

    # Some standalone Build Tools installations expose MSBuild on disk without a
    # vswhere-registered instance. Keep this fallback bounded to conventional
    # Visual Studio/MSBuild layouts and prefer the native 64-bit host.
    $roots = @(
        (Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio"),
        (Join-Path ${env:ProgramFiles} "Microsoft Visual Studio"),
        (Join-Path ${env:ProgramFiles(x86)} "MSBuild")
    ) | Where-Object { Test-Path -LiteralPath $_ -PathType Container }
    $patterns = @(
        "*\*\MSBuild\Current\Bin\amd64\MSBuild.exe",
        "*\*\MSBuild\Current\Bin\MSBuild.exe",
        "*\MSBuild\Current\Bin\amd64\MSBuild.exe",
        "*\MSBuild\Current\Bin\MSBuild.exe"
    )
    foreach ($root in $roots) {
        foreach ($pattern in $patterns) {
            $result = Get-ChildItem -Path (Join-Path $root $pattern) -File -ErrorAction SilentlyContinue |
                Select-Object -First 1
            if ($result) { return $result.FullName }
        }
    }

    throw "MSBuild was not found; Visual Studio C++ build tools are required to validate the generated utility project."
}

function Get-PipelineHash {
    $paths = @(
        (Join-Path $PSScriptRoot "refresh_ghidra_calibur.ps1"),
        (Join-Path $PSScriptRoot "StructuredExporter.java"),
        (Join-Path $PSScriptRoot "build_browse_tree.py"),
        (Join-Path $script:Root "classification_overrides.json"),
        (Join-Path $script:Root "ue_origin_registry.json"),
        (Join-Path $script:Root "re_watchlist.json")
    )
    $lines = foreach ($path in $paths) {
        "$(Split-Path -Leaf $path)=$((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant())"
    }
    # Include an explicit contract label so a contract-version bump cannot reuse a
    # workspace even if a future implementation happens to keep identical files.
    $bytes = [Text.Encoding]::UTF8.GetBytes((("contract=ghidra-calibur-export/v1") + $lines -join "`n"))
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace("-", "").ToLowerInvariant()
    }
    finally { $sha.Dispose() }
}

function Test-CanReuseGeneration {
    param(
        [string]$PreviousManifestPath,
        [Parameter(Mandatory = $true)][string]$CandidateManifestPath
    )
    if (-not $PreviousManifestPath -or -not (Test-Path -LiteralPath $PreviousManifestPath)) { return $false }
    $previous = Get-Content -LiteralPath $PreviousManifestPath -Raw | ConvertFrom-Json
    $candidate = Get-Content -LiteralPath $CandidateManifestPath -Raw | ConvertFrom-Json
    if ($previous.content.schema -ne $candidate.content.schema -or
        $previous.content.pipeline_sha256 -ne $candidate.content.pipeline_sha256 -or
        $previous.content.partial -ne $candidate.content.partial) { return $false }
    foreach ($name in @(
        "program_name", "executable_sha256", "image_base", "language", "compiler_spec", "modification_number",
        "ghidra_version"
    )) {
        if ($previous.content.program.$name -ne $candidate.content.program.$name) { return $false }
    }
    if (($previous.content.export_options | ConvertTo-Json -Compress -Depth 8) -ne
        ($candidate.content.export_options | ConvertTo-Json -Compress -Depth 8)) { return $false }
    if (($previous.content.browse_headers | ConvertTo-Json -Compress -Depth 4) -ne
        ($candidate.content.browse_headers | ConvertTo-Json -Compress -Depth 4)) { return $false }
    foreach ($artifact in @(
        ".ghidra/data/function_metadata.jsonl", ".ghidra/data/types.jsonl", ".ghidra/data/globals.jsonl",
        ".ghidra/data/strings.jsonl", ".ghidra/data/comments.jsonl", ".ghidra/data/calls.jsonl",
        ".ghidra/data/imports.jsonl", ".ghidra/data/exports.jsonl"
    )) {
        if ($previous.content.artifacts.$artifact -ne $candidate.content.artifacts.$artifact) { return $false }
    }
    return $true
}

try {
    Acquire-RefreshLock
    if ($PSVersionTable.PSVersion -lt [Version]"5.1") {
        throw "PowerShell 5.1 or newer is required; found $($PSVersionTable.PSVersion)."
    }
    if ($TimeoutSeconds -lt 1) { throw "TimeoutSeconds must be positive." }
    $python = Resolve-Python311
    $applicationProperties = Join-Path $GhidraRoot "Ghidra\application.properties"
    if (-not (Test-Path -LiteralPath $ProjectPath -PathType Container)) { throw "Ghidra project directory not found: $ProjectPath" }
    if (-not (Test-Path -LiteralPath (Join-Path $ProjectPath "$ProjectName.gpr") -PathType Leaf)) {
        throw "Expected project file was not found: $(Join-Path $ProjectPath "$ProjectName.gpr")"
    }
    $versionLine = Get-Content -LiteralPath $applicationProperties | Where-Object { $_ -like "application.version=*" } | Select-Object -First 1
    if ($versionLine -ne "application.version=12.0.4") {
        throw "Ghidra 12.0.4 is required; found '$versionLine'."
    }

    $driveRoot = [IO.Path]::GetPathRoot($script:Root)
    $driveName = $driveRoot.TrimEnd("\").TrimEnd(":")
    $drive = Get-PSDrive -Name $driveName
    if ($drive.Free -lt 4GB) { throw "At least 4 GiB free space is required for a safe export; available: $([Math]::Round($drive.Free / 1GB, 2)) GiB." }

    $schema = Invoke-McpGet -Path "/mcp/schema" -TimeoutSec 15
    $saveTool = $schema.tools | Where-Object { $_.path -eq "/save_program" }
    $runScriptTool = $schema.tools | Where-Object { $_.path -eq "/run_script" }
    $analysisStatusTool = $schema.tools | Where-Object { $_.path -eq "/analysis_status" }
    $programInfoTool = $schema.tools | Where-Object { $_.path -eq "/get_current_program_info" }
    if (-not $saveTool -or -not $runScriptTool -or -not $analysisStatusTool -or -not $programInfoTool) {
        throw "The connected Ghidra MCP schema must expose /save_program, /run_script, /analysis_status, and /get_current_program_info."
    }
    $programInfo = Invoke-McpGet -Path "/get_current_program_info" -TimeoutSec 15
    if ($programInfo.name -ne $ProgramName -or $programInfo.path -ne "/$ProgramName") {
        throw "MCP current-program mismatch: expected '$ProgramName' at '/$ProgramName', got '$($programInfo.name)' at '$($programInfo.path)'."
    }
    $analysisStatus = Invoke-McpGet -Path "/analysis_status" -TimeoutSec 15
    if ($analysisStatus.name -ne $ProgramName -or $analysisStatus.analyzing) {
        throw "Ghidra auto-analysis must be idle for '$ProgramName' before export."
    }
    $executablePath = [string]$programInfo.executable_path
    if ($executablePath -match "^/[A-Za-z]:/") { $executablePath = $executablePath.Substring(1) }
    $executablePath = $executablePath.Replace("/", "\")
    if (-not (Test-Path -LiteralPath $executablePath -PathType Leaf)) {
        throw "The executable reported by MCP is not locally readable: $executablePath"
    }
    $expectedExecutableSha256 = (Get-FileHash -LiteralPath $executablePath -Algorithm SHA256).Hash.ToLowerInvariant()

    $runId = [DateTime]::UtcNow.ToString("yyyyMMddTHHmmssfffZ") + "-" + $PID
    $script:RunRoot = Join-Path $script:StagingRoot $runId
    $exportDir = Join-Path $script:RunRoot "export"
    $workspaceDir = Join-Path $script:RunRoot "workspace"
    $logDir = Join-Path $script:RunRoot "logs"
    New-Item -ItemType Directory -Force -Path $exportDir, $logDir, $script:GenerationsRoot | Out-Null

    Write-Host "Saving '$ProgramName' through MCP..."
    $save = Invoke-McpGet -Path "/save_program" -TimeoutSec 60
    if (-not $save.success -or $save.program -ne $ProgramName) {
        throw "MCP did not confirm saving the expected program: $($save | ConvertTo-Json -Compress)"
    }
    $exporterPath = Join-Path $PSScriptRoot "StructuredExporter.java"
    $probeResponse = Invoke-McpPost -Path "/run_script" -TimeoutSec 180 -Body ([ordered]@{
        script_path = $exporterPath
        args = "$exportDir $TimeoutSeconds --probe"
        program = $ProgramName
    })
    [IO.File]::WriteAllText(
        (Join-Path $logDir "probe.log"),
        ([string]$probeResponse),
        (New-Object Text.UTF8Encoding($false))
    )
    $probe = Get-Content -LiteralPath (Join-Path $exportDir "probe.json") -Raw | ConvertFrom-Json
    if ($probe.program_name -ne $ProgramName -or $probe.image_base -ne $programInfo.image_base -or
        $probe.executable_sha256 -ne $expectedExecutableSha256 -or $probe.ghidra_version -ne "12.0.4") {
        throw "Live exporter probe identity does not match the expected MCP program."
    }

    Write-Host "Exporting structured Ghidra data from modification $($probe.modification_number)..."
    $exportResponse = Invoke-McpPost -Path "/run_script" -TimeoutSec 10800 -Body ([ordered]@{
        script_path = $exporterPath
        args = "$exportDir $TimeoutSeconds"
        timeout_seconds = 10800
        capture_output = $true
        program = $ProgramName
    })
    [IO.File]::WriteAllText(
        (Join-Path $logDir "exporter.log"),
        ([string]$exportResponse),
        (New-Object Text.UTF8Encoding($false))
    )
    if ([string]$exportResponse -notmatch "Structured export finished") {
        throw "MCP returned without the structured-export success marker. See $logDir"
    }

    $exportInfo = Get-Content -LiteralPath (Join-Path $exportDir "export_info.json") -Raw | ConvertFrom-Json
    if ($exportInfo.program_name -ne $ProgramName -or $exportInfo.image_base -ne $programInfo.image_base) {
        throw "Structured export identity does not match the MCP program."
    }
    if ([string]$exportInfo.executable_sha256 -ne $expectedExecutableSha256) {
        throw "Structured export executable SHA-256 does not match the executable reported by MCP."
    }
    if ([int64]$exportInfo.counts.functions -ne [int64]$programInfo.function_count) {
        throw "Structured-export function count $($exportInfo.counts.functions) differs from MCP count $($programInfo.function_count)."
    }

    $previousManifest = Get-PreviousManifest
    $pipelineSha256 = Get-PipelineHash
    $builderArgs = @($python.Prefix) + @(
        (Join-Path $PSScriptRoot "build_browse_tree.py"),
        "--export-dir", $exportDir,
        "--workspace-dir", $workspaceDir,
        "--overrides", (Join-Path $script:Root "classification_overrides.json"),
        "--origin-registry", (Join-Path $script:Root "ue_origin_registry.json"),
        "--watchlist", (Join-Path $script:Root "re_watchlist.json"),
        "--pipeline-sha256", $pipelineSha256
    )
    if ($previousManifest) { $builderArgs += @("--previous-manifest", $previousManifest) }
    if ($AllowPartial) { $builderArgs += "--allow-partial" }
    $builderStdout = Join-Path $logDir "builder.out.log"
    $builderStderr = Join-Path $logDir "builder.err.log"
    & $python.FilePath @builderArgs 1> $builderStdout 2> $builderStderr
    if ($LASTEXITCODE -ne 0) {
        $detail = Get-Content -LiteralPath $builderStderr -Raw -ErrorAction SilentlyContinue
        throw "Workspace generation failed with exit code $LASTEXITCODE. $detail"
    }
    $builderResult = Get-Content -LiteralPath $builderStdout -Raw | ConvertFrom-Json

    $msbuild = Find-MSBuild
    $msbuildRoot = Join-Path $script:RunRoot "msbuild-output"
    New-Item -ItemType Directory -Force -Path $msbuildRoot | Out-Null
    Invoke-NativeChecked -FilePath $msbuild -Arguments @(
        (Join-Path $workspaceDir "GhidraCalibur.vcxproj"),
        "/t:Build", "/p:Configuration=Debug", "/p:Platform=x64", "/nologo", "/v:minimal",
        ("/p:BaseIntermediateOutputPath=" + (Join-Path $msbuildRoot "int\\")),
        ("/p:OutDir=" + (Join-Path $msbuildRoot "out\\")),
        ("/p:IntDir=" + (Join-Path $msbuildRoot "int\\"))
    ) -Description "generated Visual Studio project validation" -OutputPath (Join-Path $logDir "msbuild.log")

    $runData = Join-Path $workspaceDir ".ghidra\run"
    New-Item -ItemType Directory -Force -Path $runData | Out-Null
    Copy-Item -LiteralPath (Join-Path $exportDir "diagnostics.jsonl") -Destination (Join-Path $runData "diagnostics.jsonl")
    Copy-Item -LiteralPath $logDir -Destination (Join-Path $runData "logs") -Recurse
    $previousWorkspace = if ($previousManifest) { Split-Path -Parent $previousManifest } else { $null }
    $summaryArgs = @((Join-Path $PSScriptRoot "summarize_ghidra_changes.py"), "--candidate", $workspaceDir, "--output", (Join-Path $runData "change_summary.json"))
    if ($previousWorkspace) { $summaryArgs += @("--previous", $previousWorkspace) }
    $summaryResult = & $python.FilePath @summaryArgs
    if ($LASTEXITCODE -ne 0) { throw "Semantic change summary failed with exit code $LASTEXITCODE." }
    Write-Host "Semantic changes: $summaryResult"
    $runReport = [ordered]@{
        started_utc = $script:RunStartedUtc.ToString("o")
        completed_utc = [DateTime]::UtcNow.ToString("o")
        host = $env:COMPUTERNAME
        ghidra_root = $GhidraRoot
        project_path = $ProjectPath
        program_name = $ProgramName
        mcp_base_url = $McpBaseUrl
        program_modification_number = $exportInfo.modification_number
        duration_ms = [int64](([DateTime]::UtcNow - $script:RunStartedUtc).TotalMilliseconds)
        retry_policy = [ordered]@{
            retry_timeout_seconds = $exportInfo.decompiler_options.retry_timeout_seconds
            retryable_statuses = @("timeout", "decompile-error")
            retry_attempts = [int64]$exportInfo.counts.retry_attempts
        }
        diagnostics = "run/diagnostics.jsonl"
        change_summary = "run/change_summary.json"
        logs = "run/logs"
        allow_partial = [bool]$AllowPartial
    }
    [IO.File]::WriteAllText(
        (Join-Path $runData "run_report.json"),
        (($runReport | ConvertTo-Json -Depth 5) + "`n"),
        (New-Object Text.UTF8Encoding($false))
    )
    Remove-Item -LiteralPath (Join-Path $exportDir "bodies.dat") -Force
    [IO.File]::WriteAllText(
        (Join-Path $workspaceDir ".complete"),
        ([string]$builderResult.generation_id + "`n"),
        [Text.Encoding]::ASCII
    )

    $candidateManifest = Join-Path $workspaceDir "content_manifest.json"
    $reusePrevious = Test-CanReuseGeneration -PreviousManifestPath $previousManifest -CandidateManifestPath $candidateManifest
    $generationId = if ($reusePrevious) {
        [string](Get-Content -LiteralPath $previousManifest -Raw | ConvertFrom-Json).generation_id
    }
    else {
        [string]$builderResult.generation_id
    }
    if ([IO.Path]::GetFileName($generationId) -ne $generationId) { throw "Builder returned unsafe generation id." }
    $generationPath = Join-Path $script:GenerationsRoot $generationId
    if ($reusePrevious) {
        Write-Host "Authoritative program state is unchanged; reusing immutable generation $generationId."
    }
    elseif (Test-Path -LiteralPath $generationPath) {
        $existingManifest = Join-Path $generationPath "content_manifest.json"
        if ((Get-FileHash -LiteralPath $existingManifest -Algorithm SHA256).Hash -ne
            (Get-FileHash -LiteralPath (Join-Path $workspaceDir "content_manifest.json") -Algorithm SHA256).Hash) {
            throw "Generation ID collision with different content: $generationId"
        }
        Write-Host "Generation $generationId already exists; reusing the immutable workspace."
    }
    else {
        Move-Item -LiteralPath $workspaceDir -Destination $generationPath
    }

    $solutionPath = Join-Path $generationPath "GhidraCalibur.sln"
    Set-CurrentPointer -Value ([ordered]@{
        generation_id = $generationId
        solution = "generations/$generationId/GhidraCalibur.sln"
        manifest = "generations/$generationId/content_manifest.json"
        partial = [bool]$builderResult.partial
        published_utc = [DateTime]::UtcNow.ToString("o")
    })
    Remove-OldGenerations -CurrentGeneration $generationId
$script:Succeeded = $true

    Write-Host "GhidraCalibur export published successfully."
    Write-Host "Generation: $generationId"
    Write-Host "Solution: $solutionPath"
    if ($OpenWorkspace) {
        Start-Process -FilePath $solutionPath
    }
}
catch {
    Write-Error $_
    exit 1
}
finally {
    if ($script:LockStream) { $script:LockStream.Dispose() }
    if ($script:RunRoot -and (Test-Path -LiteralPath $script:RunRoot) -and $script:Succeeded -and -not $KeepStaging) {
        try { Remove-TreeWithin -Path $script:RunRoot -AllowedRoot $script:StagingRoot }
        catch { Write-Warning "Unable to remove staging directory '$script:RunRoot': $($_.Exception.Message)" }
    }
    elseif ($script:RunRoot -and (Test-Path -LiteralPath $script:RunRoot) -and -not $script:Succeeded) {
        Write-Warning "Failed-run staging was preserved for diagnosis: $script:RunRoot"
    }
}
