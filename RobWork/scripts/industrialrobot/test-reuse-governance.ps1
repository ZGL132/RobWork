# 在共享对象的隔离 Git 检出中执行真实门禁；核对具名错误，不修改调用方工作区。
param([string]$RepoRoot = '', [string]$OutputDir = '')
$ErrorActionPreference = 'Stop'
if (-not $RepoRoot) { $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path }
if (-not $OutputDir) { $OutputDir = Join-Path $RepoRoot '.cache/reuse-tests' }
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
[IO.Directory]::CreateDirectory($OutputDir) | Out-Null
$sandbox = Join-Path ([IO.Path]::GetTempPath()) ('ird-reuse-'+[guid]::NewGuid().ToString('N'))
$ps = (Get-Process -Id $PID).Path
$validatorSource = Join-Path $PSScriptRoot 'validate-reuse.ps1'
$validator = Join-Path $sandbox '.reuse-validator.ps1'
$design = 'RobWork/doc/industrial-robot-design/'
$reuse = $design+'reuse/'
$product = 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/'
$script:results = @(); $script:restore = @{}
# 首次写入记录原字节；每例结束恢复，避免负例相互污染。
function Put([string]$path,[string]$text) {
    $full = Join-Path $sandbox $path
    if (-not $script:restore.ContainsKey($full)) {
        $old = $null; if (Test-Path -LiteralPath $full) { $old = [IO.File]::ReadAllBytes($full) }
        $script:restore[$full] = $old
    }
    [IO.Directory]::CreateDirectory((Split-Path -Parent $full)) | Out-Null
    [IO.File]::WriteAllText($full,$text,[Text.UTF8Encoding]::new($false))
}
function GetJson([string]$p) { return ([IO.File]::ReadAllText((Join-Path $sandbox $p)) | ConvertFrom-Json) }
function PutJson([string]$p,$j) { Put $p ($j | ConvertTo-Json -Depth 30) }
function Hash([string]$s) {
    $sha=[Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes(($s -replace "`r`n","`n").TrimEnd("`n")+"`n")))).Replace('-','').ToLowerInvariant() } finally { $sha.Dispose() }
}
function Declare([string]$name,[string]$source,[string]$mode='business') {
    $p=$product+'kinematics/src/'+$name+'.cpp'; Put $p $source
    $m=GetJson ($reuse+'changes/GOV-T21.json')
    $m.files += [pscustomobject]@{path=$p;sha256=(Hash $source);mode=$mode;boundary='fixture only';verify='fixture';capabilities=@('KIN-FK')}
    PutJson ($reuse+'changes/GOV-T21.json') $m
    $t=GetJson ($design+'tasks/GOV-T21.json'); $t.allowedFiles += $p; $t.forbiddenFiles=@(); PutJson ($design+'tasks/GOV-T21.json') $t
}
function Case([string]$name,[string]$expected,[scriptblock]$edit,[string[]]$extra=@()) {
    $script:restore=@{}
    try {
        & $edit
        # 负例的 stderr 是被测输出；PS5.1 不应先把它当作测试驱动自身的终结错误。
        $savedPreference = $ErrorActionPreference
        try { $ErrorActionPreference = 'Continue'; $output = (& $ps -NoProfile -File $validator -RepoRoot $sandbox @extra 2>&1 | ForEach-Object { "$_" } | Out-String -Width 32767); $code=$LASTEXITCODE } finally { $ErrorActionPreference = $savedPreference }
        $ok=($code -eq 0 -and $expected -eq 'PASS') -or ($code -ne 0 -and $expected -ne 'PASS' -and $output.Contains($expected))
        $script:results += [pscustomobject]@{name=$name;passed=$ok;exitCode=$code;expected=$expected;output=$output}
        [IO.File]::WriteAllText((Join-Path $OutputDir ($name+'.log')),$output)
        Write-Output "$name : $ok (exit=$code expected=$expected)"
    } finally {
        foreach ($full in $script:restore.Keys) {
            if ($null -eq $script:restore[$full]) { if (Test-Path -LiteralPath $full) { Remove-Item -LiteralPath $full -Force } }
            else { [IO.File]::WriteAllBytes($full,$script:restore[$full]) }
        }
    }
}
try {
    & git clone --shared --quiet --no-checkout $RepoRoot $sandbox
    if ($LASTEXITCODE) { throw 'fixture clone failed' }
    & git -C $sandbox config core.autocrlf true
    & git -C $sandbox config core.safecrlf false
    & git -C $sandbox sparse-checkout init --cone
    & git -C $sandbox sparse-checkout set 'RobWork/RobWork/src' 'RobWork/RobWorkSim/src' $product ($design+'reuse') ($design+'tasks')
    & git -C $sandbox checkout --quiet --detach HEAD
    if ($LASTEXITCODE) { throw 'fixture checkout failed' }
    Copy-Item -LiteralPath $validatorSource -Destination $validator
    # 工作树治理输入复制进夹具；冻结提交/CI下也执行相同测试。
    foreach ($relative in @('reuse','tasks')) {
        $from=Join-Path $RepoRoot ($design+$relative); $to=Join-Path $sandbox ($design+$relative)
        [IO.Directory]::CreateDirectory($to)|Out-Null
        Copy-Item -Path (Join-Path $from '*') -Destination $to -Recurse -Force
    }
    Copy-Item -LiteralPath (Join-Path $RepoRoot ($product+'CMakeLists.txt')) -Destination (Join-Path $sandbox ($product+'CMakeLists.txt')) -Force
    Case '01-clean' 'PASS' {}
    Case '02-fixed-baseline' 'REUSE-BASELINE' { $j=GetJson ($reuse+'catalog.json'); $j.baselineCommit='HEAD'; PutJson ($reuse+'catalog.json') $j }
    Case '03-all-units' 'REUSE-UNITS' { $j=GetJson ($reuse+'catalog.json'); $j.units=@('kinematics'); PutJson ($reuse+'catalog.json') $j }
    Case '04-missing-source' 'REUSE-MISSING' { $j=GetJson ($reuse+'catalog.json'); $j.capabilities[0].upstream[0].path='RobWork/RobWork/src/rw/missing.hpp'; PutJson ($reuse+'catalog.json') $j }
    Case '05-stale-source' 'REUSE-UPSTREAM-HASH' { $j=GetJson ($reuse+'catalog.json'); $j.capabilities[0].upstream[0].sha256='0000'; PutJson ($reuse+'catalog.json') $j }
    Case '06-missing-symbol' 'REUSE-UPSTREAM-SYMBOL' { $j=GetJson ($reuse+'catalog.json'); $j.capabilities[0].upstream[0].symbols=@('NoSuchSymbol123'); PutJson ($reuse+'catalog.json') $j }
    Case '07-inventory-omission' 'REUSE-INVENTORY' { $j=GetJson ($reuse+'inventory.json'); $j.files=@($j.files | Select-Object -Skip 1); PutJson ($reuse+'inventory.json') $j }
    Case '08-forged-blob' 'REUSE-INVENTORY' { $j=GetJson ($reuse+'inventory.json'); $j.files[0].baselineBlob='0000'; PutJson ($reuse+'inventory.json') $j }
    Case '09-inventory-duplicate' 'REUSE-INVENTORY' { $j=GetJson ($reuse+'inventory.json'); $j.files += $j.files[0]; PutJson ($reuse+'inventory.json') $j }
    Case '10-inventory-unit' 'REUSE-INVENTORY' { $j=GetJson ($reuse+'inventory.json'); $j.files[0].unit='wrong'; PutJson ($reuse+'inventory.json') $j }
    Case '11-new-source' 'REUSE-UNDECLARED' { Put ($product+'kinematics/src/Undeclared.cpp') '// new source' }
    Case '12-stale-change-hash' 'REUSE-UNDECLARED' { Put ($product+'CMakeLists.txt') ([IO.File]::ReadAllText((Join-Path $sandbox ($product+'CMakeLists.txt')))+"`n# changed content`n") }
    Case '13-core-even-business' 'REUSE-DUPLICATE' { Declare 'Duplicate' 'struct Vec3 { double x; };' }
    Case '14-cpp-not-governance' 'REUSE-MODE' { Declare 'WrongMode' '// fixture' 'governance' }
    Case '15-missing-approval' 'REUSE-PATH' { Declare 'Unapproved' '// fixture' 'extension' }
    Case '16-unknown-capability' 'REUSE-CAPABILITY' { $j=GetJson ($reuse+'changes/GOV-T21.json'); $j.files[0].capabilities=@('NONE'); PutJson ($reuse+'changes/GOV-T21.json') $j }
    Case '17-path-traversal' 'REUSE-PATH' { $j=GetJson ($reuse+'changes/GOV-T21.json'); $j.files[0].path='../outside.cpp'; PutJson ($reuse+'changes/GOV-T21.json') $j }
    Case '18-missing-task-proof' 'REUSE-TASK' { $j=GetJson ($design+'tasks/GOV-T21.json'); $j.PSObject.Properties.Remove('reuse'); PutJson ($design+'tasks/GOV-T21.json') $j }
    Case '19-task-scope' 'REUSE-SCOPE' { $j=GetJson ($design+'tasks/GOV-T21.json'); $j.allowedFiles=@('none'); PutJson ($design+'tasks/GOV-T21.json') $j }
    Case '20-forbidden-scope' 'REUSE-SCOPE' { $j=GetJson ($design+'tasks/GOV-T21.json'); $j.forbiddenFiles=@($product+'CMakeLists.txt'); PutJson ($design+'tasks/GOV-T21.json') $j }
    Case '21-stage-not-approved' 'REUSE-STAGE' { $j=GetJson ($design+'tasks/REUSE-T01.json'); $j.status='ready'; $j | Add-Member reuse ([pscustomobject]@{policy='H1-H6';manifest='no-manifest'}) -Force; PutJson ($design+'tasks/REUSE-T01.json') $j }
    Case '22-final-debt-rejected' 'REUSE-MIGRATION' {} @('-RequireMigrationComplete')
    Case '23-governance-not-algorithm-approval' 'REUSE-GOV-SCOPE' { Declare 'Business' '//业务格式转换 fixture' }
    Case '24-upstream-readonly' 'REUSE-UPSTREAM-READONLY' { Put 'RobWork/RobWork/src/rw/Undeclared.cpp' '// forbidden upstream' }
    Case '25-missing-record-file' 'REUSE-MISSING' { $j=GetJson ($reuse+'changes/GOV-T21.json'); $j.taskFile=$design+'tasks/Absent.json'; PutJson ($reuse+'changes/GOV-T21.json') $j }
    Case '26-deletion-undeclared' 'REUSE-UNDECLARED' {
        $p=Join-Path $sandbox ($product+'kinematics/src/Fk.cpp'); $script:restore[$p]=[IO.File]::ReadAllBytes($p); Remove-Item -LiteralPath $p
    }
    Case '27-forged-debt-closure' 'REUSE-INVENTORY' { $j=GetJson ($reuse+'inventory.json'); $j.files[0].classification='existing-adapter'; PutJson ($reuse+'inventory.json') $j }
    Case '28-arbitrary-task-ready' 'REUSE-TASK' { PutJson ($design+'tasks/OTHER-T99.json') ([pscustomobject]@{taskId='OTHER-T99';status='ready'}) }
    Case '29-planned-not-executable' 'REUSE-TASK-STATE' {} @('-TaskFile',($design+'tasks/REUSE-T01.json'))
    Case '30-forged-build-target' 'REUSE-UPSTREAM-TARGET' { $j=GetJson ($reuse+'catalog.json'); $j.capabilities[0].upstream[0].target='nonexistent'; PutJson ($reuse+'catalog.json') $j }
    # CI/验收读取冻结Git对象，不能仅测试工作树；提交仅存在于将销毁的夹具中。
    & git -C $sandbox add -- $reuse ($design+'tasks/GOV-T21.json') ($design+'tasks/REUSE-T01.json') ($design+'tasks/REUSE-T02.json') ($design+'tasks/REUSE-T03.json') ($design+'tasks/REUSE-T04.json') ($design+'tasks/REUSE-T05.json') ($design+'tasks/REUSE-T06.json') ($product+'CMakeLists.txt')
    if ($LASTEXITCODE) { throw 'fixture stage failed' }
    & git -C $sandbox -c user.name=ReuseFixture -c user.email=fixture@example.invalid commit --quiet -m 'fixture frozen tree'
    if ($LASTEXITCODE) { throw 'fixture commit failed' }
    Case '31-frozen-head' 'PASS' {} @('-Head','HEAD')
    Case '32-frozen-head-ignores-dirty-tree' 'PASS' { Put ($reuse+'catalog.json') '{}' } @('-Head','HEAD')
} finally {
    $failed=@($script:results | Where-Object { -not $_.passed }).Count
    $report=[ordered]@{schemaVersion='ird-reuse-tests/1';validatorSha256=(Get-FileHash -LiteralPath $validatorSource -Algorithm SHA256).Hash;passed=($script:results.Count-$failed);skipped=0;failed=$failed;tests=$script:results}
    $report | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $OutputDir 'reuse-test-report.json') -Encoding UTF8
    $xml=[xml]'<testsuite name="reuse-governance"/>'
    $xml.DocumentElement.SetAttribute('tests',"$($script:results.Count)"); $xml.DocumentElement.SetAttribute('failures',"$failed"); $xml.DocumentElement.SetAttribute('skipped','0')
    foreach ($r in $script:results) {
        $c=$xml.CreateElement('testcase'); $c.SetAttribute('name',$r.name)
        if (-not $r.passed) { $f=$xml.CreateElement('failure'); $f.InnerText=$r.output; [void]$c.AppendChild($f) }
        [void]$xml.DocumentElement.AppendChild($c)
    }
    $xml.Save((Join-Path $OutputDir 'reuse-governance.junit.xml'))
    # 只清理本次创建的随机临时目录，先验证绝对路径位于系统临时根。
    $tempRoot=[IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\','/')+[IO.Path]::DirectorySeparatorChar
    if ([IO.Path]::GetFullPath($sandbox).StartsWith($tempRoot,[StringComparison]::OrdinalIgnoreCase) -and (Split-Path -Leaf $sandbox) -like 'ird-reuse-*') { Remove-Item -LiteralPath $sandbox -Recurse -Force }
}
if ($failed -or $script:results.Count -lt 32) { throw "reuse tests failed/incomplete: $failed / $($script:results.Count)" }
Write-Output "reuse selftests: passed=$($script:results.Count) skipped=0 failed=0"
