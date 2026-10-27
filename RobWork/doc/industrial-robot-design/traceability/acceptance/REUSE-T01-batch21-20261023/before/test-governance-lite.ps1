# 精简治理回归：确认短提交可用、无信息标题仍拒绝、索引审计可选但没有失去拒绝能力。
# 在隔离稀疏 Git 检出中破坏索引，不修改调用者的证据、工作树或分支。
param([string]$RepoRoot = '', [string]$OutputDir = '')
$ErrorActionPreference = 'Stop'
if (-not $RepoRoot) { $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path }
if (-not $OutputDir) { $OutputDir = Join-Path $RepoRoot '.cache/governance-lite-tests' }
[IO.Directory]::CreateDirectory($OutputDir) | Out-Null
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ('ird-gov-lite-' + [guid]::NewGuid().ToString('N'))
$psExe = (Get-Process -Id $PID).Path
$results = @()

# 每例检查真实子进程退出码及具名输出，避免“命令没执行”也被当成负例通过。
function Invoke-Probe([string]$Name, [string]$Script, [string[]]$Arguments, [int]$Expected, [string]$Pattern) {
    $previous = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $output = (& $psExe -NoProfile -File $Script @Arguments 2>&1 | Out-String)
        $code = $LASTEXITCODE
    } finally { $ErrorActionPreference = $previous }
    [IO.File]::WriteAllText((Join-Path $OutputDir "$Name.log"), $output)
    $pass = ($code -eq $Expected -and $output -match $Pattern)
    $script:results += [pscustomobject]@{ name=$Name; passed=$pass; exitCode=$code; expectedExitCode=$Expected }
    if (-not $pass) { throw "$Name failed: exit=$code expected=$Expected; see $OutputDir" }
    Write-Output "PASS $Name"
}

try {
    $lint = Join-Path $PSScriptRoot 'commit-lint.ps1'
    $cases = @(
        @{name='short-doc'; text='[docs] 精简普通任务入口'; code=0; pattern='commit-lint: PASS'},
        @{name='free-body'; text=("[governance] 取消固定提交段落`n`n验证：正负例已执行。"); code=0; pattern='commit-lint: PASS'},
        @{name='no-prefix'; text='精简普通任务入口'; code=1; pattern='标题缺少合法前缀'},
        @{name='generic-title'; text='[docs] 更新文件'; code=1; pattern='标题无信息量'},
        @{name='empty-title'; text='[docs]'; code=1; pattern='标题前缀后为空'},
        @{name='unknown-prefix'; text='[unknown] 精简入口'; code=1; pattern='标题缺少合法前缀'}
    )
    foreach ($case in $cases) {
        Invoke-Probe $case.name $lint @('-RepoRoot',$RepoRoot,'-Message',$case.text) $case.code $case.pattern
    }

    # 只检出门禁实际需要的树；共享对象但独立索引和文件，避免复制现有 build/vcpkg。
    & git clone --shared --quiet --no-checkout $RepoRoot $testRoot
    if ($LASTEXITCODE) { throw 'fixture clone failed' }
    & git -C $testRoot sparse-checkout init --cone
    if ($LASTEXITCODE) { throw 'fixture sparse init failed' }
    # 父目录文件由 cone 模式自动带入；只取 traceability/pipeline，避免检出历史
    # builds 中的长路径截图，也让本测试与“普通任务不加载历史库”的实际流程一致。
    & git -C $testRoot sparse-checkout set 'RobWork/doc/industrial-robot-design/context' 'RobWork/doc/industrial-robot-design/tasks' 'RobWork/doc/industrial-robot-design/units' 'RobWork/doc/industrial-robot-design/reuse' 'RobWork/doc/industrial-robot-design/traceability/pipeline' 'RobWork/doc/industrial-robot-design/traceability/acceptance' 'RobWork/scripts' 'RobWork/.agents' 'RobWork/RobWork/src' 'RobWork/RobWorkSim/src' 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot'
    if ($LASTEXITCODE) { throw 'fixture sparse set failed' }
    & git -C $testRoot checkout --quiet --detach HEAD
    if ($LASTEXITCODE) { throw 'fixture checkout failed' }

    # 同时支持提交前工作树验证与提交后重跑；只覆盖本测试所需的治理文件。
    $overlays = @('AGENTS.md','RobWork/doc/industrial-robot-design/AGENTS.md','RobWork/doc/industrial-robot-design/acceptance-protocol.md','RobWork/RobWorkStudio/src/rwslibs/industrialrobot/AGENTS.md')
    $overlays += @(Get-ChildItem -LiteralPath $PSScriptRoot -Filter '*.ps1' | ForEach-Object { 'RobWork/scripts/industrialrobot/' + $_.Name })
    foreach ($path in $overlays) { Copy-Item -LiteralPath (Join-Path $RepoRoot $path) -Destination (Join-Path $testRoot $path) -Force }
    $index = Join-Path $testRoot 'RobWork/doc/industrial-robot-design/traceability/evidence-index.json'
    [IO.File]::WriteAllText($index, '{ invalid-index')
    $docs = Join-Path $testRoot 'RobWork/scripts/industrialrobot/validate-docs.ps1'
    Invoke-Probe 'default-ignores-optional-index' $docs @() 0 'validate-docs: PASS'
    Invoke-Probe 'deep-rejects-invalid-index' $docs @('-DeepEvidence') 1 'evidence-index.json is not valid JSON'
} finally {
    $passed = @($results | Where-Object { $_.passed }).Count
    $report = [pscustomobject]@{ expected=8; executed=$results.Count; passed=$passed; failed=($results.Count-$passed); skipped=0; complete=($results.Count -eq 8 -and $passed -eq 8); cases=$results }
    $report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $OutputDir 'governance-lite-report.json') -Encoding UTF8
    # Windows 删除前核对绝对路径属于本次唯一临时目录，绝不沿主树依赖 junction 清理。
    $tempPrefix = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar
    $resolvedTest = [IO.Path]::GetFullPath($testRoot)
    if ($resolvedTest.StartsWith($tempPrefix,[StringComparison]::OrdinalIgnoreCase) -and (Split-Path -Leaf $resolvedTest) -like 'ird-gov-lite-*') {
        if (Test-Path -LiteralPath $resolvedTest) { Remove-Item -LiteralPath $resolvedTest -Recurse -Force }
    }
}
if (-not $report.complete) { exit 1 }
Write-Output "governance-lite: PASS ($passed passed, 0 failed, 0 skipped)"
