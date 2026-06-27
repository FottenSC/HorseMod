param(
    [Parameter(Mandatory = $false)]
    [string]$PackageZip,

    [Parameter(Mandatory = $true)]
    [string]$LocalVersion,

    [Parameter(Mandatory = $true)]
    [string]$RepoRoot,

    [string]$EnvPath = "",
    [string]$RepositoryUrl = "https://thunderstore.io",
    [string]$Namespace = "Fotten",
    [string]$PackageName = "HorseMod",
    [string]$Community = "soulcalibur-vi",
    [string]$Category = "tools",
    [switch]$CheckOnly
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Net.Http

function Write-Release {
    param([string]$Message)
    Write-Host "[release.thunderstore] $Message"
}

function ConvertTo-SemVerParts {
    param([string]$Version)

    if ($Version -notmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$') {
        throw "Version '$Version' must be strict Major.Minor.Patch SemVer."
    }

    [pscustomobject]@{
        Major = [int64]$Matches[1]
        Minor = [int64]$Matches[2]
        Patch = [int64]$Matches[3]
    }
}

function Compare-SemVer {
    param(
        [string]$Left,
        [string]$Right
    )

    $a = ConvertTo-SemVerParts $Left
    $b = ConvertTo-SemVerParts $Right

    foreach ($part in @("Major", "Minor", "Patch")) {
        if ($a.$part -gt $b.$part) { return 1 }
        if ($a.$part -lt $b.$part) { return -1 }
    }
    return 0
}

function Invoke-JsonRequest {
    param(
        [string]$Method,
        [string]$Url,
        [object]$Body = $null,
        [string]$BearerToken = "",
        [int[]]$ExpectedStatusCodes = @(200)
    )

    $client = [System.Net.Http.HttpClient]::new()
    try {
        $request = [System.Net.Http.HttpRequestMessage]::new([System.Net.Http.HttpMethod]::new($Method), $Url)
        if ($BearerToken) {
            $request.Headers.Authorization = [System.Net.Http.Headers.AuthenticationHeaderValue]::new("Bearer", $BearerToken)
        }
        if ($null -ne $Body) {
            $json = $Body | ConvertTo-Json -Depth 20 -Compress
            $request.Content = [System.Net.Http.StringContent]::new($json, [System.Text.Encoding]::UTF8, "application/json")
        }

        $response = $client.SendAsync($request).GetAwaiter().GetResult()
        $text = $response.Content.ReadAsStringAsync().GetAwaiter().GetResult()
        $status = [int]$response.StatusCode

        if ($ExpectedStatusCodes -notcontains $status) {
            throw "Unexpected HTTP $status from $Method $Url`: $text"
        }

        $jsonBody = $null
        if (-not [string]::IsNullOrWhiteSpace($text)) {
            $jsonBody = $text | ConvertFrom-Json
        }

        [pscustomobject]@{
            StatusCode = $status
            Json = $jsonBody
            Text = $text
        }
    }
    finally {
        $client.Dispose()
    }
}

function Get-PublishedVersion {
    $url = "$RepositoryUrl/api/experimental/package/$Namespace/$PackageName/"
    $response = Invoke-JsonRequest -Method "GET" -Url $url -ExpectedStatusCodes @(200, 404)
    if ($response.StatusCode -eq 404) {
        return $null
    }
    if ($null -eq $response.Json.latest -or [string]::IsNullOrWhiteSpace($response.Json.latest.version_number)) {
        throw "Thunderstore package response did not include latest.version_number."
    }
    return [string]$response.Json.latest.version_number
}

function Assert-LocalVersionCanPublish {
    ConvertTo-SemVerParts $LocalVersion | Out-Null

    $publishedVersion = Get-PublishedVersion
    if ([string]::IsNullOrWhiteSpace($publishedVersion)) {
        Write-Release "published package not found; treating $LocalVersion as first publish"
        return
    }

    $comparison = Compare-SemVer -Left $LocalVersion -Right $publishedVersion
    if ($comparison -le 0) {
        throw "Local VERSION $LocalVersion must be higher than published Thunderstore version $publishedVersion."
    }

    Write-Release "version gate OK: local $LocalVersion > published $publishedVersion"
}

function Assert-EnvFileIsSafe {
    param([string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) {
        throw ".env file not found at $Path"
    }

    $envName = Split-Path -Leaf $Path
    if ($envName -ne ".env") {
        throw "Expected the Thunderstore token file to be named .env, got '$envName'."
    }

    $insideGit = $false
    & git -C $RepoRoot rev-parse --is-inside-work-tree *> $null
    if ($LASTEXITCODE -eq 0) {
        $insideGit = $true
    }

    if ($insideGit) {
        & git -C $RepoRoot check-ignore -q -- ".env"
        if ($LASTEXITCODE -ne 0) {
            throw ".env exists but is not ignored by git; refusing to read secrets."
        }

        & git -C $RepoRoot ls-files --error-unmatch -- ".env" *> $null
        if ($LASTEXITCODE -eq 0) {
            throw ".env is already tracked by git; remove it from the index before publishing."
        }
    }
}

function Read-DotEnv {
    param([string]$Path)

    $values = @{}
    foreach ($rawLine in Get-Content -LiteralPath $Path) {
        $line = $rawLine.Trim()
        if ($line -eq "" -or $line.StartsWith("#")) {
            continue
        }
        if ($line.StartsWith("export ")) {
            $line = $line.Substring(7).Trim()
        }
        if ($line -notmatch '^([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.*)$') {
            continue
        }

        $key = $Matches[1]
        $value = $Matches[2].Trim()
        if (($value.StartsWith('"') -and $value.EndsWith('"')) -or ($value.StartsWith("'") -and $value.EndsWith("'"))) {
            $value = $value.Substring(1, $value.Length - 2)
        }
        $values[$key] = $value
    }
    return $values
}

function Get-ThunderstoreToken {
    param([string]$Path)

    Assert-EnvFileIsSafe -Path $Path
    $dotenv = Read-DotEnv -Path $Path
    $tokenKeys = @(
        "THUNDERSTORE_BEARER_TOKEN",
        "THUNDERSTORE_API_TOKEN",
        "THUNDERSTORE_TOKEN",
        "TCLI_AUTH_TOKEN"
    )

    foreach ($key in $tokenKeys) {
        if ($dotenv.ContainsKey($key) -and -not [string]::IsNullOrWhiteSpace($dotenv[$key])) {
            Write-Release "loaded Thunderstore bearer token from .env key $key"
            return [string]$dotenv[$key]
        }
    }

    throw "Missing Thunderstore token in .env. Add THUNDERSTORE_BEARER_TOKEN=tss_... or TCLI_AUTH_TOKEN=tss_..."
}

function Assert-ZipManifestMatchesVersion {
    param([string]$ZipPath)

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [System.IO.Compression.ZipFile]::OpenRead($ZipPath)
    try {
        $entry = $zip.Entries | Where-Object { $_.FullName -eq "manifest.json" } | Select-Object -First 1
        if ($null -eq $entry) {
            throw "Package zip does not contain manifest.json at the root."
        }

        $reader = [System.IO.StreamReader]::new($entry.Open(), [System.Text.Encoding]::UTF8)
        try {
            $manifest = $reader.ReadToEnd() | ConvertFrom-Json
        }
        finally {
            $reader.Dispose()
        }

        if ([string]$manifest.version_number -ne $LocalVersion) {
            throw "manifest.json version_number '$($manifest.version_number)' does not match VERSION '$LocalVersion'."
        }
    }
    finally {
        $zip.Dispose()
    }
}

function Read-FileChunk {
    param(
        [string]$Path,
        [int64]$Offset,
        [int64]$Length
    )

    if ($Length -gt [int]::MaxValue) {
        throw "Upload chunk length $Length is larger than this script supports."
    }

    $buffer = New-Object byte[] ([int]$Length)
    $stream = [System.IO.File]::Open($Path, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::Read)
    try {
        [void]$stream.Seek($Offset, [System.IO.SeekOrigin]::Begin)
        $read = 0
        while ($read -lt $Length) {
            $n = $stream.Read($buffer, $read, [int]($Length - $read))
            if ($n -le 0) {
                break
            }
            $read += $n
        }
        if ($read -ne $Length) {
            throw "Read $read bytes from $Path, expected $Length."
        }
        return $buffer
    }
    finally {
        $stream.Dispose()
    }
}

function Upload-Part {
    param(
        [string]$ZipPath,
        [object]$Part
    )

    $bytes = Read-FileChunk -Path $ZipPath -Offset ([int64]$Part.offset) -Length ([int64]$Part.length)
    $md5 = [System.Security.Cryptography.MD5]::Create()
    try {
        $hash = $md5.ComputeHash($bytes)
    }
    finally {
        $md5.Dispose()
    }

    $client = [System.Net.Http.HttpClient]::new()
    try {
        $request = [System.Net.Http.HttpRequestMessage]::new([System.Net.Http.HttpMethod]::Put, [string]$Part.url)
        $request.Content = [System.Net.Http.ByteArrayContent]::new($bytes)
        $request.Content.Headers.ContentMD5 = $hash
        $request.Content.Headers.ContentLength = [int64]$Part.length

        $response = $client.SendAsync($request).GetAwaiter().GetResult()
        $text = $response.Content.ReadAsStringAsync().GetAwaiter().GetResult()
        if (-not $response.IsSuccessStatusCode) {
            throw "Chunk $($Part.part_number) upload failed with HTTP $([int]$response.StatusCode): $text"
        }
        if ($null -eq $response.Headers.ETag) {
            throw "Chunk $($Part.part_number) upload response did not include an ETag."
        }

        [pscustomobject]@{
            ETag = $response.Headers.ETag.Tag
            PartNumber = [int]$Part.part_number
        }
    }
    finally {
        $client.Dispose()
    }
}

function Publish-Package {
    param(
        [string]$ZipPath,
        [string]$BearerToken
    )

    $file = Get-Item -LiteralPath $ZipPath
    $uploadUuid = $null

    try {
        $initBody = @{
            filename = $file.Name
            file_size_bytes = [int64]$file.Length
        }
        $init = Invoke-JsonRequest `
            -Method "POST" `
            -Url "$RepositoryUrl/api/experimental/usermedia/initiate-upload/" `
            -BearerToken $BearerToken `
            -Body $initBody `
            -ExpectedStatusCodes @(201)

        $uploadUuid = [string]$init.Json.user_media.uuid
        $uploadUrls = @($init.Json.upload_urls)
        if ([string]::IsNullOrWhiteSpace($uploadUuid) -or $uploadUrls.Count -eq 0) {
            throw "Thunderstore initiate-upload response did not include upload UUID and URLs."
        }

        Write-Release "uploading $($file.Name) in $($uploadUrls.Count) chunk(s)"
        $parts = @()
        foreach ($part in $uploadUrls) {
            $parts += Upload-Part -ZipPath $ZipPath -Part $part
        }

        [array]$parts = $parts | Sort-Object PartNumber
        [void](Invoke-JsonRequest `
            -Method "POST" `
            -Url "$RepositoryUrl/api/experimental/usermedia/$uploadUuid/finish-upload/" `
            -BearerToken $BearerToken `
            -Body @{ parts = $parts } `
            -ExpectedStatusCodes @(200))

        $submitBody = @{
            author_name = $Namespace
            communities = @($Community)
            community_categories = @{
                $Community = @($Category)
            }
            categories = @()
            has_nsfw_content = $false
            upload_uuid = $uploadUuid
        }
        $submit = Invoke-JsonRequest `
            -Method "POST" `
            -Url "$RepositoryUrl/api/experimental/submission/submit/" `
            -BearerToken $BearerToken `
            -Body $submitBody `
            -ExpectedStatusCodes @(200)

        $downloadUrl = [string]$submit.Json.package_version.download_url
        if ([string]::IsNullOrWhiteSpace($downloadUrl)) {
            throw "Thunderstore submit response did not include package_version.download_url."
        }

        Write-Release "published $Namespace-$PackageName-$LocalVersion"
        Write-Release "download: $downloadUrl"
    }
    catch {
        if (-not [string]::IsNullOrWhiteSpace($uploadUuid)) {
            try {
                [void](Invoke-JsonRequest `
                    -Method "POST" `
                    -Url "$RepositoryUrl/api/experimental/usermedia/$uploadUuid/abort-upload/" `
                    -BearerToken $BearerToken `
                    -ExpectedStatusCodes @(200, 204))
            }
            catch {
                Write-Release "warning: failed to abort incomplete upload $uploadUuid"
            }
        }
        throw
    }
}

try {
    if ([string]::IsNullOrWhiteSpace($EnvPath)) {
        $EnvPath = Join-Path $RepoRoot ".env"
    }

    $RepositoryUrl = $RepositoryUrl.TrimEnd("/")

    Assert-LocalVersionCanPublish
    if ($CheckOnly) {
        exit 0
    }

    if ([string]::IsNullOrWhiteSpace($PackageZip)) {
        throw "PackageZip is required unless -CheckOnly is set."
    }
    if (-not (Test-Path -LiteralPath $PackageZip)) {
        throw "Package zip not found at $PackageZip"
    }

    Assert-ZipManifestMatchesVersion -ZipPath $PackageZip
    $token = Get-ThunderstoreToken -Path $EnvPath
    Publish-Package -ZipPath $PackageZip -BearerToken $token
}
catch {
    Write-Release "ERROR: $($_.Exception.Message)"
    exit 1
}
