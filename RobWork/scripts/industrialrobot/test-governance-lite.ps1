# 精简治理回归：确认短提交可用、无信息标题仍拒绝、索引审计可选但没有失去拒绝能力。
# 在隔离稀疏 Git 检出中破坏索引，不修改调用者的证据、工作树或分支。
param([string]$RepoRoot = '', [string]$OutputDir = '', [switch]$ValidationBatchOnly, [switch]$SerialMainlineOnly)
$ErrorActionPreference = 'Stop'
if ($ValidationBatchOnly -and $SerialMainlineOnly) { throw 'select one governance suite' }
if (-not $RepoRoot) { $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path }
if (-not $OutputDir) { $OutputDir = Join-Path $RepoRoot '.cache/governance-lite-tests' }
[IO.Directory]::CreateDirectory($OutputDir) | Out-Null
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ('ird-gov-lite-' + [guid]::NewGuid().ToString('N'))
$psExe = (Get-Process -Id $PID).Path
$results = @()
$expectedTests = if ($SerialMainlineOnly) { 12 } elseif ($ValidationBatchOnly) { 23 } else { 21 }

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

# 批次回归复用本文件的子进程/退出码/报告机制，只在临时目录替换昂贵依赖以计数。
# 计数替身不提供复用规则的语义证据；真实 CheckTask 的选择循环另有定向正负例。
function Invoke-BatchProbe([string]$Name, [string]$Body, [int]$Expected, [string]$Pattern, [int]$Calls) {
    $counter = Join-Path $testRoot 'gate-calls.jsonl'
    if (Test-Path -LiteralPath $counter) { Remove-Item -LiteralPath $counter }
    $runner = Join-Path $batchScripts 'batch-runner.ps1'
    [IO.File]::WriteAllText($runner, ('$ErrorActionPreference="Stop"' + "`n" + $Body), [Text.UTF8Encoding]::new($true))
    Invoke-Probe $Name $runner @() $Expected $Pattern
    $actual = if (Test-Path -LiteralPath $counter) { @(Get-Content -LiteralPath $counter).Count } else { 0 }
    if ($actual -ne $Calls) {
        $script:results[-1].passed = $false
        throw "$Name full reuse calls=$actual expected=$Calls"
    }
}

function Test-ValidationBatch {
    $script:batchScripts = Join-Path $testRoot 'RobWork/scripts/industrialrobot'
    $base = Join-Path $testRoot 'RobWork/doc/industrial-robot-design'
    [IO.Directory]::CreateDirectory($batchScripts) | Out-Null
    [IO.Directory]::CreateDirectory((Join-Path $base 'tasks')) | Out-Null
    [IO.Directory]::CreateDirectory((Join-Path $base 'context')) | Out-Null
    [IO.File]::WriteAllText((Join-Path $base 'context/README.md'), '# fixture')
    foreach ($name in @('validate-task.ps1','verify-task.ps1')) {
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination (Join-Path $batchScripts $name)
    }
    $stub = @'
param([string]$RepoRoot, [string[]]$TaskFile, [switch]$RequireMigrationComplete)
if (-not $RepoRoot) { $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path }
@{tasks=@($TaskFile); strict=[bool]$RequireMigrationComplete} | ConvertTo-Json -Compress | Add-Content (Join-Path $RepoRoot 'gate-calls.jsonl')
if ((Test-Path (Join-Path $RepoRoot 'fail-gate')) -or $RequireMigrationComplete) { Write-Output 'fixture gate rejected'; exit 1 }
Write-Output 'fixture full reuse PASS'
exit 0
'@
    [IO.File]::WriteAllText((Join-Path $batchScripts 'validate-reuse.ps1'), $stub, [Text.UTF8Encoding]::new($true))
    $a = Join-Path $base 'tasks/BATCH-T01.json'; $b = Join-Path $base 'tasks/BATCH-T02.json'
    $one = @{taskId='BATCH-T01';unit='foundation-governance';masterWp='WP-A';title='batch fixture';status='ready';branch='codex/fixture';dependsOn=@();requirements=@('H6');designRefs=@('context/README.md');verify=@('Write-Output fixture');allowedFiles=@('fixture');forbiddenFiles=@('forbidden');outputs=@('fixture');acceptance=@('fixture');interUnit=$false;reuse=@{policy='H1-H6';manifest='fixture'}}
    $two = $one.Clone(); $two.taskId='BATCH-T02'
    $one | ConvertTo-Json -Depth 10 | Set-Content $a -Encoding UTF8
    $two | ConvertTo-Json -Depth 10 | Set-Content $b -Encoding UTF8
    $prefix = "Set-Location '$testRoot'; `$root='$testRoot'; `$a='$a'; `$b='$b'; "
    $validate = "& '$batchScripts/validate-task.ps1' -RepoRoot `$root "
    Invoke-BatchProbe 'batch-single-compatible' ($prefix+$validate+'-TaskFile $a') 0 'validate-task: PASS' 1
    Invoke-BatchProbe 'batch-two-tasks-one-gate' ($prefix+$validate+'-TaskFile @($a,$b)') 0 'validate-task: PASS' 1
    Invoke-BatchProbe 'batch-duplicate-path-once' ($prefix+$validate+'-TaskFile @($a,$a)') 0 'validate-task: PASS' 1
    @($one,$two) | ConvertTo-Json -Depth 10 | Set-Content (Join-Path $base 'tasks/array.json') -Encoding UTF8
    Invoke-BatchProbe 'batch-array-contract-one-gate' ($prefix+$validate+"-TaskFile '$base/tasks/array.json'") 0 'validate-task: PASS' 1
    $two.branch='main'; $two | ConvertTo-Json -Depth 10 | Set-Content $b -Encoding UTF8
    Invoke-BatchProbe 'batch-bad-second-blocks' ($prefix+$validate+'-TaskFile @($a,$b)') 1 'invalid branch' 0
    $two.branch='codex/fixture'; $two | ConvertTo-Json -Depth 10 | Set-Content $b -Encoding UTF8
    [IO.File]::WriteAllText((Join-Path $testRoot 'fail-gate'),'fixture')
    Invoke-BatchProbe 'batch-gate-fail-propagates' ($prefix+$validate+'-TaskFile @($a,$b)') 1 'H1-H6 task reuse contract failed' 1
    Remove-Item -LiteralPath (Join-Path $testRoot 'fail-gate')
    $hostName = [IO.Path]::GetFileNameWithoutExtension($psExe)
    $canonical = "$hostName -NoProfile -File RobWork/scripts/industrialrobot/validate-reuse.ps1 -TaskFile RobWork/doc/industrial-robot-design/tasks/BATCH-T01.json"
    $verify = "& '$batchScripts/verify-task.ps1' -RepoRoot `$root -TaskFile `$a"
    $one.verify=@($canonical); $one | ConvertTo-Json -Depth 10 | Set-Content $a -Encoding UTF8
    Invoke-BatchProbe 'verify-identical-leading-reused' ($prefix+$verify) 0 'reusedPreflight=1' 1
    Invoke-BatchProbe 'verify-relative-root-same-input' ($prefix+$verify.Replace('-RepoRoot $root','-RepoRoot .')) 0 'reusedPreflight=1' 1
    $otherHost = if ($hostName -eq 'pwsh') { 'powershell' } else { 'pwsh' }
    $one.verify=@($canonical.Replace("$hostName -NoProfile", "$otherHost -NoProfile")); $one | ConvertTo-Json -Depth 10 | Set-Content $a -Encoding UTF8
    Invoke-BatchProbe 'verify-other-runtime-not-reused' ($prefix+$verify) 0 'reusedPreflight=0' 2
    $one.verify=@($canonical+' -RequireMigrationComplete'); $one | ConvertTo-Json -Depth 10 | Set-Content $a -Encoding UTF8
    Invoke-BatchProbe 'verify-stronger-check-executes' ($prefix+$verify) 1 'verification failed' 2
    $one.verify=@('Write-Output first',$canonical); $one | ConvertTo-Json -Depth 10 | Set-Content $a -Encoding UTF8
    Invoke-BatchProbe 'verify-after-command-executes' ($prefix+$verify) 0 'reusedPreflight=0' 2
    $one.verify=@('Write-Output must-not-run'); $one | ConvertTo-Json -Depth 10 | Set-Content $a -Encoding UTF8
    [IO.File]::WriteAllText((Join-Path $testRoot 'fail-gate'),'fixture')
    Invoke-BatchProbe 'verify-preflight-fail-stops' ($prefix+$verify) 1 'H1-H6 task reuse contract failed' 1
    Remove-Item -LiteralPath (Join-Path $testRoot 'fail-gate')
    Invoke-BatchProbe 'verify-dryrun-still-checks' ($prefix+$verify+' -DryRun') 0 'DRYRUN PASS' 1
    $one.verify=@($canonical.Replace('BATCH-T01','BATCH-T02')); $one | ConvertTo-Json -Depth 10 | Set-Content $a -Encoding UTF8
    Invoke-BatchProbe 'verify-other-task-executes' ($prefix+$verify) 0 'reusedPreflight=0' 2
    $one.verify=@($canonical+'; Write-Output injected-ran'); $one | ConvertTo-Json -Depth 10 | Set-Content $a -Encoding UTF8
    Invoke-BatchProbe 'verify-command-suffix-not-reused' ($prefix+$verify) 0 'injected-ran' 2
    # 定向执行本次修改的 docs 委派分支，不重跑无关资产/索引检查。
    $tokens=$null; $parseErrors=$null
    $docsAst=[Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot 'validate-docs.ps1'),[ref]$tokens,[ref]$parseErrors)
    $route=@($docsAst.EndBlock.Statements | Where-Object { $_.Extent.Text -match '^if \(\$TaskFile\)' })
    if ($parseErrors.Count -or $route.Count -ne 1) { throw 'docs batch route not found' }
    $docsPrefix=$prefix+"`$RepoRoot='$testRoot/RobWork'; `$errors=@(); "
    Invoke-BatchProbe 'docs-default-full-gate' ($docsPrefix+'$TaskFile=@(); '+$route[0].Extent.Text) 0 'fixture full reuse PASS' 1
    Invoke-BatchProbe 'docs-batch-one-gate' ($docsPrefix+'$TaskFile=@($a,$b); '+$route[0].Extent.Text) 0 'validate-task: PASS' 1
    $two.branch='main'; $two | ConvertTo-Json -Depth 10 | Set-Content $b -Encoding UTF8
    Invoke-BatchProbe 'docs-bad-task-not-hidden' ($docsPrefix+'$TaskFile=@($a,$b); '+$route[0].Extent.Text+'; if($errors.Count){throw ($errors -join ";")}') 1 'invalid branch' 0
    $two.branch='codex/fixture'; $two.status='planned'; $two.Remove('reuse')
    $two | ConvertTo-Json -Depth 10 | Set-Content $b -Encoding UTF8
    Invoke-BatchProbe 'docs-legacy-still-full-gate' ($docsPrefix+'$TaskFile=@($b); '+$route[0].Extent.Text) 0 'fixture full reuse PASS' 1
    # 复用真实 CheckTask 和本次修改的选择循环，验证第二份契约不被漏查。
    $gateAst=[Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot 'validate-reuse.ps1'),[ref]$tokens,[ref]$parseErrors)
    $check=@($gateAst.FindAll({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq 'CheckTask'},$false))[0].Extent.Text
    $jsonReader=@($gateAst.FindAll({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq 'Json'},$false))[0].Extent.Text
    $selection=@($gateAst.FindAll({param($n) $n -is [Management.Automation.Language.ForEachStatementAst] -and $n.Variable.VariablePath.UserPath -eq 'selectedTaskFile'},$true))
    if ($parseErrors.Count -or $selection.Count -ne 1) { throw 'reuse batch selection not found' }
    $real=@{taskId='GOV-T21';status='ready';reuse=@{policy='H1-H6';manifest='RobWork/doc/industrial-robot-design/reuse/changes/GOV-T21.json'}}
    $real | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $base 'tasks/real.json') -Encoding UTF8
    @($real,$real) | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $base 'tasks/real-array.json') -Encoding UTF8
    $real.status='planned'; $real | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $base 'tasks/bad-real.json') -Encoding UTF8
    $realPrefix=$prefix+'$RepoRoot=$root; $design="RobWork/doc/industrial-robot-design/"; function Fail($code,$detail){throw "REUSE-$code : $detail"}; function ReadText($path){if($path -like "*/changes/*"){return (@{taskId="GOV-T21";schemaVersion="ird-reuse-change/1";files=@()} | ConvertTo-Json -Compress)}; [IO.File]::ReadAllText((Join-Path $RepoRoot $path))}; '+$jsonReader+"`n"+$check+"`n"
    Invoke-BatchProbe 'reuse-real-checktask-batch' ($realPrefix+'$TaskFile=@("RobWork/doc/industrial-robot-design/tasks/real.json","RobWork/doc/industrial-robot-design/tasks/real.json"); '+$selection[0].Extent.Text+'; Write-Output real-checktask-PASS') 0 'real-checktask-PASS' 0
    Invoke-BatchProbe 'reuse-real-array-contracts' ($realPrefix+'$TaskFile=@("RobWork/doc/industrial-robot-design/tasks/real-array.json"); '+$selection[0].Extent.Text+'; Write-Output real-checktask-PASS') 0 'real-checktask-PASS' 0
    Invoke-BatchProbe 'reuse-real-bad-second-rejected' ($realPrefix+'$TaskFile=@("RobWork/doc/industrial-robot-design/tasks/real.json","RobWork/doc/industrial-robot-design/tasks/bad-real.json"); '+$selection[0].Extent.Text) 1 'REUSE-TASK-STATE' 0
    Invoke-BatchProbe 'reuse-real-outside-root-rejected' ($realPrefix+'$TaskFile=@("../outside.json"); '+$selection[0].Extent.Text) 1 'REUSE-PATH' 0
}

# 主线串行规则只在临时小仓库测试真实脚本，不触发全量复用/产品构建。
# 无 reuse 的夹具只证明分支和范围校验，不冒充生产算法或依赖准入证据。
function Test-SerialMainline {
    $scripts = Join-Path $testRoot 'RobWork/scripts/industrialrobot'
    $tasks = Join-Path $testRoot 'RobWork/doc/industrial-robot-design/tasks'
    [IO.Directory]::CreateDirectory($scripts) | Out-Null
    [IO.Directory]::CreateDirectory($tasks) | Out-Null
    foreach ($name in @('validate-task.ps1','verify-task-scope.ps1')) {
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination (Join-Path $scripts $name)
    }
    $reference = Join-Path $testRoot 'RobWork/doc/industrial-robot-design/fixture.md'
    [IO.File]::WriteAllText($reference, '# fixture')
    $path = Join-Path $tasks 'SERIAL-T01.json'
    $contract = @{taskId='SERIAL-T01';unit='foundation-governance';masterWp='WP-A';title='串行夹具';status='ready';branch='redesign-main';dependsOn=@();requirements=@('NFR-COR-02');designRefs=@('fixture.md');verify=@('Write-Output fixture');allowedFiles=@('fixture.txt');forbiddenFiles=@('forbidden.txt');outputs=@('fixture');acceptance=@('fixture');interUnit=$false}
    foreach ($case in @(@('serial-mainline','redesign-main',0),@('serial-task-branch','codex/fixture',0),@('serial-main-rejected','main',1),@('serial-unknown-rejected','arbitrary-branch',1))) {
        $contract.branch=$case[1]
        $contract | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $path -Encoding UTF8
        $pattern = if ($case[2] -eq 0) { 'validate-task: PASS' } else { 'invalid branch' }
        Invoke-Probe $case[0] (Join-Path $scripts 'validate-task.ps1') @('-RepoRoot',$testRoot,'-TaskFile',$path) $case[2] $pattern
    }
    $contract.branch='redesign-main'
    $contract | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $path -Encoding UTF8
    & git -C $testRoot init --quiet
    if ($LASTEXITCODE) { throw 'serial fixture git init failed' }
    [IO.File]::WriteAllText((Join-Path $testRoot 'fixture.txt'),'before')
    & git -C $testRoot add fixture.txt
    if ($LASTEXITCODE) { throw 'serial fixture git add failed' }
    & git -C $testRoot -c user.name=fixture -c user.email=fixture@example.invalid commit --quiet -m initial
    if ($LASTEXITCODE) { throw 'serial fixture initial commit failed' }
    # GOV-T23-R1-01：必须建立真实主线现场，不能只用契约字段模拟检出分支。
    & git -C $testRoot branch -M redesign-main
    if ($LASTEXITCODE) { throw 'serial fixture mainline setup failed' }
    $start = (& git -C $testRoot rev-parse HEAD).Trim()
    [IO.File]::WriteAllText((Join-Path $testRoot 'fixture.txt'),'after')
    & git -C $testRoot add fixture.txt
    if ($LASTEXITCODE) { throw 'serial fixture second add failed' }
    & git -C $testRoot -c user.name=fixture -c user.email=fixture@example.invalid commit --quiet -m changed
    if ($LASTEXITCODE) { throw 'serial fixture second commit failed' }
    $finish = (& git -C $testRoot rev-parse HEAD).Trim()
    $scope = Join-Path $scripts 'verify-task-scope.ps1'
    $common = @('-RepoRoot',$testRoot,'-TaskFile',$path,'-Head',$finish)
    Invoke-Probe 'serial-base-required' $scope $common 1 'requires explicit -Base'
    Invoke-Probe 'serial-explicit-scope' $scope ($common+@('-Base',$start)) 0 'verify-task-scope: PASS'
    $contract.forbiddenFiles=@('fixture.txt')
    $contract | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $path -Encoding UTF8
    Invoke-Probe 'serial-forbidden-rejected' $scope ($common+@('-Base',$start)) 1 '\[forbidden\]'
    $contract.forbiddenFiles=@('forbidden.txt'); $contract.allowedFiles=@('other.txt')
    $contract | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $path -Encoding UTF8
    Invoke-Probe 'serial-outside-rejected' $scope ($common+@('-Base',$start)) 1 '\[out-of-scope\]'
    # 历史契约保留 codex 分支用于溯源；在实际主线上仍必须显式给出本包基点。
    $contract.branch='codex/fixture'; $contract.allowedFiles=@('fixture.txt')
    $contract | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $path -Encoding UTF8
    Invoke-Probe 'serial-legacy-mainline-base-required' $scope $common 1 'requires explicit -Base'
    Invoke-Probe 'serial-legacy-explicit-scope' $scope ($common+@('-Base',$start)) 0 'changed=1[\s\S]*verify-task-scope: PASS'
    # 冻结 SHA 的 detached 验收也不能用主线与自身比较，冒充零改动通过。
    & git -C $testRoot checkout --quiet --detach $finish
    if ($LASTEXITCODE) { throw 'serial fixture detached setup failed' }
    Invoke-Probe 'serial-detached-mainline-base-required' $scope $common 1 'requires explicit -Base'
    # 真正从主线分出的任务分支仍兼容既有 merge-base 行为，防止修复误禁旧入口。
    & git -C $testRoot checkout --quiet -b codex/fixture $finish
    if ($LASTEXITCODE) { throw 'serial fixture task branch setup failed' }
    & git -C $testRoot branch --force redesign-main $start
    if ($LASTEXITCODE) { throw 'serial fixture branch point setup failed' }
    Invoke-Probe 'serial-task-implicit-scope' $scope $common 0 'changed=1[\s\S]*verify-task-scope: PASS'
}

try {
  if ($SerialMainlineOnly) {
    Test-SerialMainline
  } elseif ($ValidationBatchOnly) {
    Test-ValidationBatch
  } else {
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
  }
} finally {
    $passed = @($results | Where-Object { $_.passed }).Count
    $report = [pscustomobject]@{ suite=$(if($SerialMainlineOnly){'serial-mainline'}elseif($ValidationBatchOnly){'validation-batch'}else{'governance-lite'}); expected=$expectedTests; executed=$results.Count; passed=$passed; failed=($results.Count-$passed); skipped=0; notRun=($expectedTests-$results.Count); complete=($results.Count -eq $expectedTests -and $passed -eq $expectedTests); cases=$results }
    $report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $OutputDir 'governance-lite-report.json') -Encoding UTF8
    # 沿既有治理测试的 JUnit＋JSON 口径，明确本次选中套件，不冒充全套历史回归。
    $xml = [xml]'<testsuite/>'
    $xml.DocumentElement.SetAttribute('name', $report.suite)
    $xml.DocumentElement.SetAttribute('tests', "$($results.Count)")
    $xml.DocumentElement.SetAttribute('failures', "$($report.failed)")
    $xml.DocumentElement.SetAttribute('skipped', '0')
    foreach ($case in $results) {
        $node = $xml.CreateElement('testcase'); $node.SetAttribute('name', $case.name)
        if (-not $case.passed) { $failure=$xml.CreateElement('failure'); $failure.InnerText="See $($case.name).log"; [void]$node.AppendChild($failure) }
        [void]$xml.DocumentElement.AppendChild($node)
    }
    if ($report.notRun) { $xml.DocumentElement.SetAttribute('errors','1'); $node=$xml.CreateElement('testcase'); $node.SetAttribute('name','driver-completion'); $errorNode=$xml.CreateElement('error'); $errorNode.InnerText="notRun=$($report.notRun)"; [void]$node.AppendChild($errorNode); [void]$xml.DocumentElement.AppendChild($node); $xml.DocumentElement.SetAttribute('tests', "$($results.Count+1)") }
    $xml.Save((Join-Path $OutputDir 'governance-lite.junit.xml'))
    # Windows 删除前核对绝对路径属于本次唯一临时目录，绝不沿主树依赖 junction 清理。
    $tempPrefix = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar
    $resolvedTest = [IO.Path]::GetFullPath($testRoot)
    if ($resolvedTest.StartsWith($tempPrefix,[StringComparison]::OrdinalIgnoreCase) -and (Split-Path -Leaf $resolvedTest) -like 'ird-gov-lite-*') {
        if (Test-Path -LiteralPath $resolvedTest) { Remove-Item -LiteralPath $resolvedTest -Recurse -Force }
    }
}
if (-not $report.complete) { exit 1 }
Write-Output "governance-lite: PASS ($passed passed, 0 failed, 0 skipped)"
