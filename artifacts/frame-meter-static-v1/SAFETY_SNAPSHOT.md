# Safety snapshot restoration

`frame-meter-static-v1.json` was 145,340,404 bytes, which exceeds GitHub's
100 MiB per-file limit. The safety branch therefore stores its exact gzip
representation as `frame-meter-static-v1.json.gz`.

- Original SHA-256: `CF67BB6E281060539FB908158023D05DA62AD70B47B7A062DD2185C1BF59EE0F`
- Gzip SHA-256: `913EDD4F5C38EE8EBD8A92D19819A999DBCDB25D1DEE163310CFCCCAB8CF053A`
- Original bytes: `145340404`
- Gzip bytes: `2304363`

Restore with PowerShell:

```powershell
$source = 'frame-meter-static-v1.json.gz'
$target = 'frame-meter-static-v1.json'
$input = [IO.File]::OpenRead($source)
try {
    $gzip = [IO.Compression.GZipStream]::new(
        $input, [IO.Compression.CompressionMode]::Decompress)
    try {
        $output = [IO.File]::Create($target)
        try { $gzip.CopyTo($output) } finally { $output.Dispose() }
    } finally { $gzip.Dispose() }
} finally { $input.Dispose() }

Get-FileHash -Algorithm SHA256 $target
```

The restored hash must match the original SHA-256 above.
