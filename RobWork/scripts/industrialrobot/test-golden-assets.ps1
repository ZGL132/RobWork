# DESIGN-T01：黄金资产新分支的真实正反例；不触发无关 C++/历史治理夹具。
param([string]$OutputDirectory = 'build/design-fix-t01/golden-assets')
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'golden-assets.ps1')
$fixtureRoot = [IO.Path]::GetFullPath($OutputDirectory)
[void][IO.Directory]::CreateDirectory($fixtureRoot)
$prefix = 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testdata/golden/test-fixture/1.1.0/'
# 复用入口协议的本地文件读取面；生产冻结树读取仍由 validate-reuse.ps1 提供。
function SafePath([string]$Path) {
    if ($Path -match '(^|/)\.\.(/|$)|[\\:*?\[\]]|^/') { throw 'invalid fixture path' }
    return $Path
}
function ReadText([string]$Path) { return [IO.File]::ReadAllText((Join-Path $fixtureRoot (SafePath $Path))) }
function Json([string]$Path) { return ((ReadText $Path) | ConvertFrom-Json) }
function Fail([string]$Code, [string]$Detail) { throw "REUSE-$Code : $Detail" }
function SaveFixture([string]$Path, [string]$Text) {
    $target = Join-Path $fixtureRoot $Path
    [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target))
    [IO.File]::WriteAllText($target, $Text, [Text.UTF8Encoding]::new($false))
}
$content = '{"schema":"test-inputs/1","value":1}' + "`n"
$raw = [Text.Encoding]::UTF8.GetBytes($content)
$hasher = [Security.Cryptography.SHA256]::Create()
try { $hash = ([BitConverter]::ToString($hasher.ComputeHash($raw))).Replace('-','').ToLowerInvariant() }
finally { $hasher.Dispose() }
$manifest = @{schemaVersion='ird-golden-manifest/1'; datasetId='test-fixture'; version='1.1.0'; kind='analytic-case';
    inputs=@('inputs/input.json'); expected=@('expected/output.json'); integrity=@(
        @{path='inputs/input.json';sizeBytes=$raw.Length;sha256=$hash},
        @{path='expected/output.json';sizeBytes=$raw.Length;sha256=$hash})}
SaveFixture ($prefix+'inputs/input.json') $content
SaveFixture ($prefix+'expected/output.json') $content
$manifestText = $manifest | ConvertTo-Json -Depth 8
function ResetManifest { SaveFixture ($prefix+'manifest.json') $manifestText }
ResetManifest
$script:passed = 0
function Check([string]$Name, [scriptblock]$Action) {
    & $Action
    $script:passed++
    Write-Output "PASS $Name"
}
function ExpectRejected([scriptblock]$Action) {
    $rejected = $false
    try { & $Action | Out-Null } catch { $rejected = $true }
    if (-not $rejected) { throw 'expected rejection' }
}
Check 'registered manifest' { if (-not (Test-DeclaredGoldenJson ($prefix+'manifest.json'))) { throw 'not accepted' } }
Check 'registered input' { if (-not (Test-DeclaredGoldenJson ($prefix+'inputs/input.json'))) { throw 'not accepted' } }
Check 'registered expected' { if (-not (Test-DeclaredGoldenJson ($prefix+'expected/output.json'))) { throw 'not accepted' } }
Check 'CRLF normalization' {
    SaveFixture ($prefix+'inputs/input.json') ($content.Replace("`n","`r`n"))
    if (-not (Test-DeclaredGoldenJson ($prefix+'inputs/input.json'))) { throw 'not accepted' }
    SaveFixture ($prefix+'inputs/input.json') $content
}
Check 'tampered content' {
    SaveFixture ($prefix+'inputs/input.json') $content.Replace(':1}',':2}')
    ExpectRejected { Test-DeclaredGoldenJson ($prefix+'inputs/input.json') }
    SaveFixture ($prefix+'inputs/input.json') $content
}
Check 'unlisted asset' { ExpectRejected { Test-DeclaredGoldenJson ($prefix+'inputs/unlisted.json') } }
Check 'wrong manifest identity' {
    SaveFixture ($prefix+'manifest.json') $manifestText.Replace('test-fixture','wrong-fixture')
    ExpectRejected { Test-DeclaredGoldenJson ($prefix+'manifest.json') }; ResetManifest
}
Check 'unknown manifest schema' {
    SaveFixture ($prefix+'manifest.json') $manifestText.Replace('ird-golden-manifest/1','ird-golden-manifest/99')
    ExpectRejected { Test-DeclaredGoldenJson ($prefix+'manifest.json') }; ResetManifest
}
Check 'wrong size' {
    $altered = $manifestText | ConvertFrom-Json
    $altered.integrity[0].sizeBytes++
    SaveFixture ($prefix+'manifest.json') ($altered | ConvertTo-Json -Depth 8)
    ExpectRejected { Test-DeclaredGoldenJson ($prefix+'inputs/input.json') }; ResetManifest
}
Check 'missing integrity entry' {
    $altered = $manifestText | ConvertFrom-Json
    $altered.integrity = @($altered.integrity[1])
    SaveFixture ($prefix+'manifest.json') ($altered | ConvertTo-Json -Depth 8)
    ExpectRejected { Test-DeclaredGoldenJson ($prefix+'inputs/input.json') }; ResetManifest
}
Check 'manifest self reference' {
    $altered = $manifestText | ConvertFrom-Json
    $altered.inputs = @('manifest.json')
    SaveFixture ($prefix+'manifest.json') ($altered | ConvertTo-Json -Depth 8)
    ExpectRejected { Test-DeclaredGoldenJson ($prefix+'manifest.json') }; ResetManifest
}
foreach ($suffix in @('inputs/input.cpp','inputs/input.json.ps1','inputs/input.mjs','inputs/input.txt')) {
    Check "no source exemption $suffix" { if (Test-DeclaredGoldenJson ($prefix+$suffix)) { throw 'source exempted' } }
}
Check 'outside golden folder' { if (Test-DeclaredGoldenJson 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/other/new.json') { throw 'outside accepted' } }
Check 'path traversal' { ExpectRejected { Test-DeclaredGoldenJson ($prefix+'inputs/../input.json') } }
Write-Output "test-golden-assets: PASS passed=$script:passed skipped=0 failed=0"
