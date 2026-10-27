# validate-reuse.ps1 的私有数据资产检查；复用其冻结树 ReadText/Json/SafePath/Fail。
# DESIGN-T01：新版本黄金 JSON 不能被当成未知源码，也不能成为脚本或未登记数据的豁免。
function Test-DeclaredGoldenJson([string]$Path) {
    $pattern = '^RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testdata/golden/([a-z0-9-]{3,64})/([0-9]+\.[0-9]+\.[0-9]+)/(manifest\.json|(?:inputs|expected)/[a-zA-Z0-9_./-]+\.json)$'
    if ($Path -notmatch $pattern) { return $false }
    $datasetId = $Matches[1]; $version = $Matches[2]; $relative = $Matches[3]
    [void](SafePath $Path)
    $folder = $Path.Substring(0, $Path.Length - $relative.Length)
    $manifest = Json ($folder + 'manifest.json')
    if ($manifest.schemaVersion -ne 'ird-golden-manifest/1' -or
        $manifest.datasetId -ne $datasetId -or $manifest.version -ne $version -or
        $manifest.kind -notin @('analytic-case','reference-impl','contract-fixture','regression','performance-baseline') -or
        -not ($manifest.inputs -is [array]) -or -not $manifest.inputs.Count -or
        -not ($manifest.expected -is [array]) -or -not $manifest.expected.Count -or
        -not ($manifest.integrity -is [array]) -or -not $manifest.integrity.Count) {
        Fail 'GOLDEN' "invalid dataset manifest: $Path"
    }
    if ($relative -eq 'manifest.json') {
        foreach ($asset in @($manifest.inputs) + @($manifest.expected)) {
            if ($asset -match '\.json$') {
                if ($asset -notmatch '^(inputs|expected)/[a-zA-Z0-9_./-]+\.json$') { Fail 'GOLDEN' "invalid JSON asset path: $asset" }
                if (-not (Test-DeclaredGoldenJson ($folder + $asset))) { Fail 'GOLDEN' "invalid declared asset: $asset" }
            }
        }
        return $true
    }
    if ($relative -notin (@($manifest.inputs) + @($manifest.expected))) {
        Fail 'GOLDEN' "unlisted JSON asset: $Path"
    }
    $entries = @($manifest.integrity | Where-Object { $_.path -eq $relative })
    if ($entries.Count -ne 1) { Fail 'GOLDEN' "missing/duplicate integrity entry: $Path" }
    # 与 testkit::GoldenDataset 同口径：文本仅规范 CRLF→LF，保留其他字节/空白。
    $text = (ReadText $Path).Replace("`r`n", "`n")
    $bytes = [Text.Encoding]::UTF8.GetBytes($text)
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $digest = ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-','').ToLowerInvariant() }
    finally { $sha.Dispose() }
    if ($bytes.Length -ne $entries[0].sizeBytes -or $digest -cne $entries[0].sha256) {
        Fail 'GOLDEN' "size/SHA-256 mismatch: $Path"
    }
    [void]($text | ConvertFrom-Json -ErrorAction Stop)
    return $true
}
