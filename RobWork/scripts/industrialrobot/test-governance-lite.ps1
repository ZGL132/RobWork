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
    $pass = ($code -eq $Expected -and $output -match $Pattern -and $output -notmatch 'ParserError|Unexpected token')
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
    # 证据路径包含任务/批次/夹具层级；仅此clone开启长路径，不修改用户Git配置。
    & git -C $testRoot config core.longpaths true
    if ($LASTEXITCODE) { throw 'fixture longpaths config failed' }
    & git -C $testRoot sparse-checkout init --cone
    if ($LASTEXITCODE) { throw 'fixture sparse init failed' }
    # 父目录文件由cone模式带入；补齐门禁引用的reuse证据，不检出历史builds截图。
    # 省掉被摘要绑定的输入会使正例提前REUSE-MISSING，无法验证原定缓存边界。
    # 沿用test-reuse-governance的两套上游gtest检出面：catalog已绑定测试入口，
    # 漏带时正例会因REUSE-MISSING提前失败，无法实际验证索引的允许/拒绝行为。
    & git -C $testRoot sparse-checkout set 'RobWork/doc/industrial-robot-design/context' 'RobWork/doc/industrial-robot-design/tasks' 'RobWork/doc/industrial-robot-design/units' 'RobWork/doc/industrial-robot-design/reuse' 'RobWork/doc/industrial-robot-design/traceability/pipeline' 'RobWork/doc/industrial-robot-design/traceability/reuse' 'RobWork/doc/industrial-robot-design/traceability/acceptance' 'RobWork/scripts' 'RobWork/.agents' 'RobWork/RobWork/src' 'RobWork/RobWorkSim/src' 'RobWork/RobWork/gtest' 'RobWork/RobWorkSim/gtest' 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot'
    if ($LASTEXITCODE) { throw 'fixture sparse set failed' }
    & git -C $testRoot checkout --quiet --detach HEAD
    if ($LASTEXITCODE) { throw 'fixture checkout failed' }

    # 同时支持提交前工作树验证与提交后重跑；只覆盖本测试所需的治理文件。
    $overlays = @('AGENTS.md','RobWork/doc/industrial-robot-design/AGENTS.md','RobWork/doc/industrial-robot-design/acceptance-protocol.md','RobWork/RobWorkStudio/src/rwslibs/industrialrobot/AGENTS.md')
    # 本回归由父进程执行，不在clone内执行自身；覆盖自身会让clone的旧矩阵与新版
    # 测试输入失配，先在FRESHNESS处失败而根本没有测到索引/上下文包边界。
    $overlays += @(Get-ChildItem -LiteralPath $PSScriptRoot -Filter '*.ps1' | Where-Object { $_.Name -ne 'test-governance-lite.ps1' } | ForEach-Object { 'RobWork/scripts/industrialrobot/' + $_.Name })
    foreach ($path in $overlays) { Copy-Item -LiteralPath (Join-Path $RepoRoot $path) -Destination (Join-Path $testRoot $path) -Force }
    # PS5.1 对无 BOM 的中文 UTF-8 脚本可能按本机 ANSI 解码；Git 的 LF/CRLF
    # 检出不应决定门禁能否解析。只改变隔离副本换行，保留真实源文件的 BOM 状态，
    # 避免测试自己添加 BOM 后掩盖源文件错误。正例和错误 schema 都核具名结果。
    $b1Source = Join-Path $PSScriptRoot 'validate-b1-batch.ps1'
    $b1Bytes = [IO.File]::ReadAllBytes($b1Source)
    $b1HasBom = ($b1Bytes.Length -ge 3 -and $b1Bytes[0] -eq 239 -and $b1Bytes[1] -eq 187 -and $b1Bytes[2] -eq 191)
    $b1Text = [IO.File]::ReadAllText($b1Source).Replace("`r`n", "`n")
    $b1Manifest = Join-Path $testRoot 'RobWork/doc/industrial-robot-design/traceability/pipeline/b1-host-integration-batch.json'
    $badBatch = Get-Content -LiteralPath $b1Manifest -Raw -Encoding UTF8 | ConvertFrom-Json
    $badBatch.schemaVersion = 'unsupported-batch-schema'
    $badBatchPath = Join-Path $testRoot 'invalid-b1-batch.json'
    $badBatch | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath $badBatchPath -Encoding UTF8
    # PS5.1 的原生 stderr 会按控制台宽度在单词中间换行；允许标记内部换行，
    # 仍要求完整、具体的预期 schema 标记，且 Invoke-Probe 明确拒绝解析错误。
    $b1SchemaPattern = ('ird-pipeline-batch/1'.ToCharArray() | ForEach-Object { [regex]::Escape([string]$_) }) -join '\s*'
    foreach ($lineEnding in @('lf', 'crlf')) {
        $b1Variant = Join-Path $testRoot ("validate-b1-$lineEnding.ps1")
        $b1Content = if ($lineEnding -eq 'crlf') { $b1Text.Replace("`n", "`r`n") } else { $b1Text }
        [IO.File]::WriteAllText($b1Variant, $b1Content, [Text.UTF8Encoding]::new($b1HasBom))
        Invoke-Probe "b1-$lineEnding-valid" $b1Variant @('-RepoRoot',$testRoot,'-BatchFile',$b1Manifest) 0 'validate-b1-batch: PASS'
        Invoke-Probe "b1-$lineEnding-invalid" $b1Variant @('-RepoRoot',$testRoot,'-BatchFile',$badBatchPath) 1 $b1SchemaPattern
    }
    $index = Join-Path $testRoot 'RobWork/doc/industrial-robot-design/traceability/evidence-index.json'
    [IO.File]::WriteAllText($index, '{ invalid-index')
    $docs = Join-Path $testRoot 'RobWork/scripts/industrialrobot/validate-docs.ps1'
    Invoke-Probe 'default-ignores-optional-index' $docs @() 0 'validate-docs: PASS'
    Invoke-Probe 'deep-rejects-invalid-index' $docs @('-DeepEvidence') 1 'evidence-index.json is not valid JSON'

    # context/README §3：可选缓存不等于指定缺包可放行；删掉权威输入的指纹也不能
    # 把陈旧包伪装成fresh。沿用真实生成器/校验器和Git，不复制任何指纹计算算法。
    $contextRepo = Join-Path $testRoot 'context-fixture'
    $contextBase = Join-Path $contextRepo 'RobWork/doc/industrial-robot-design'
    [IO.Directory]::CreateDirectory((Join-Path $contextBase 'tasks/foundation')) | Out-Null
    & git -C $contextRepo init --quiet
    if ($LASTEXITCODE) { throw 'context fixture init failed' }
    & git -C $contextRepo config core.autocrlf false
    if ($LASTEXITCODE) { throw 'context fixture git config failed' }
    foreach ($rel in @('AGENTS.md','RobWork/doc/industrial-robot-design/AGENTS.md','RobWork/RobWorkStudio/src/rwslibs/industrialrobot/AGENTS.md','RobWork/doc/industrial-robot-design/REQUIREMENTS.md','RobWork/doc/industrial-robot-design/context/reading-paths.md','RobWork/doc/industrial-robot-design/context/asset-inventory.json','RobWork/doc/industrial-robot-design/context/README.md')) {
        $fixtureFile = Join-Path $contextRepo $rel
        [IO.Directory]::CreateDirectory((Split-Path -Parent $fixtureFile)) | Out-Null
        [IO.File]::WriteAllText($fixtureFile, "fixture $rel`n", [Text.UTF8Encoding]::new($false))
    }
    $contractPath = Join-Path $contextBase 'tasks/foundation/LITE-T01.json'
    @{ taskId='LITE-T01'; title='上下文包边界'; unit='foundation-governance'; status='ready'; dependsOn=@(); designRefs=@('context/README.md#3') } | ConvertTo-Json | Set-Content -LiteralPath $contractPath -Encoding UTF8
    $contextRoot = Join-Path $contextRepo 'RobWork'
    $validator = Join-Path $PSScriptRoot 'validate-context-pack.ps1'
    $selected = @('-RepoRoot',$contextRoot,'-TaskId','LITE-T01')
    Invoke-Probe 'context-all-no-dir' $validator @('-RepoRoot',$contextRoot) 0 'nothing to validate'
    Invoke-Probe 'context-selected-no-dir' $validator $selected 1 'pack not found'
    Invoke-Probe 'context-build' (Join-Path $PSScriptRoot 'build-context-pack.ps1') $selected 0 'build-context-pack: LITE-T01'
    Invoke-Probe 'context-fresh' $validator $selected 0 'fresh=1'
    $packPath = Join-Path $contextBase 'context/packs/LITE-T01.md'
    $originalPack = [IO.File]::ReadAllText($packPath)
    foreach ($removed in @('RobWork/doc/industrial-robot-design/tasks/foundation/LITE-T01.json','RobWork/doc/industrial-robot-design/context/README.md','AGENTS.md')) {
        $filtered = @($originalPack -split '\r?\n' | Where-Object { $_ -notmatch ('^\|\s*' + [regex]::Escape($removed) + '\s*\|') }) -join "`n"
        [IO.File]::WriteAllText($packPath, $filtered, [Text.UTF8Encoding]::new($false))
        Invoke-Probe ('context-missing-' + [IO.Path]::GetFileName($removed)) $validator $selected 1 'missing required fingerprint'
    }
    [IO.File]::WriteAllText($packPath, $originalPack, [Text.UTF8Encoding]::new($false))
    $designPath = Join-Path $contextBase 'context/README.md'
    $originalDesign = [IO.File]::ReadAllText($designPath)
    [IO.File]::AppendAllText($designPath, "changed`n")
    Invoke-Probe 'context-changed-design' $validator $selected 2 'STALE source'
    [IO.File]::WriteAllText($designPath, $originalDesign, [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText($packPath, ($originalPack + '`n"phase": "implementing"'), [Text.UTF8Encoding]::new($false))
    Invoke-Probe 'context-state-copy' $validator $selected 1 'PIPE authority red line'
} finally {
    $passed = @($results | Where-Object { $_.passed }).Count
    $report = [pscustomobject]@{ expected=21; executed=$results.Count; passed=$passed; failed=($results.Count-$passed); skipped=0; notRun=(21-$results.Count); complete=($results.Count -eq 21 -and $passed -eq 21); cases=$results }
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
