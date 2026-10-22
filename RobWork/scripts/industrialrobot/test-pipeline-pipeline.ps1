# 流水线治理脚本回归测试（PIPE v1.6 修复的测试先行入口）。
#
# 本脚本只创建系统临时目录和其中的伪 .git 锁文件，绝不改写当前仓库的真实 state.json、
# 分支或锁。它覆盖两项曾在审查中出现的失效路径：
#   ① 旧 tick 在锁被接管后不得释放接管者的锁；
#   ② 验收失败进入返工实施态时，必须保留可定位的失败证据记录。
#
# 用法：pwsh -File RobWork/scripts/industrialrobot/test-pipeline-pipeline.ps1
param([string]$RepoRoot = "")

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if (-not $RepoRoot) {
  $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path
}

$lockScript = Join-Path $RepoRoot "RobWork/scripts/industrialrobot/pipeline-lock.ps1"
$stateScript = Join-Path $RepoRoot "RobWork/scripts/industrialrobot/validate-state.ps1"
# 原生状态校验必须在当前宿主对应的解释器执行，PS5.1不可悄悄借PS7通过。
$script:statePowerShell = (Get-Process -Id $PID).Path
$sourceState = Join-Path $RepoRoot "RobWork/doc/industrial-robot-design/traceability/pipeline/state.json"

function Assert-Equal([object]$Expected, [object]$Actual, [string]$Message) {
  if ("$Expected" -ne "$Actual") { throw "$Message：expected '$Expected'，actual '$Actual'" }
}

function Assert-ExitCode([int]$Expected, [int]$Actual, [string]$Message) {
  if ($Expected -ne $Actual) { throw "$Message：expected exit $Expected，actual $Actual" }
}

function Invoke-Lock([string]$Action, [string]$Token = "") {
  # 使用 hashtable splat 传递命名参数；数组形式会把 "-Action" 当作位置参数，
  # 使 ValidateSet 收到字面量而不是动作值，掩盖真正的回归断言。
  $invokeArgs = @{ Action = $Action; RepoRoot = $script:lockRepo }
  if ($Token) { $invokeArgs.Token = $Token }
  if ($Action -eq "acquire") { $invokeArgs.LeaseMinutes = 1 }
  $output = @(& $script:lockScript @invokeArgs 2>&1)
  return @{ Output = $output; ExitCode = $LASTEXITCODE }
}

function Get-LockMutexName([string]$RepoPath) {
  # 与生产脚本约定相同：完整仓库路径的 SHA-256 使同一仓库跨 PowerShell 进程共享 mutex，
  # 不同仓库则绝不会互相阻塞。
  $normalized = (Resolve-Path $RepoPath).Path.ToUpperInvariant()
  $sha = [System.Security.Cryptography.SHA256]::Create()
  try {
    $digest = $sha.ComputeHash([System.Text.Encoding]::UTF8.GetBytes($normalized))
    return "Global\ird-pipeline-tick-" + (($digest | ForEach-Object { $_.ToString("x2") }) -join "")
  }
  finally { $sha.Dispose() }
}

function Assert-LockActionWaitsOnMutex([string]$Action, [string]$Token, [string]$ExpectedState) {
  # 直接占用生产约定的 mutex 后，让另一后台 PowerShell 执行实际锁动作。这样测试的不是
  # 人工模拟的函数调用，而是 status/renew/release 经过“读锁→核对→修改”完整入口时确实
  # 被临界区阻塞；释放 mutex 后再验证该动作的成功 JSON，防止子进程悄然失败也被误判为通过。
  $mutex = [System.Threading.Mutex]::new($false, (Get-LockMutexName $script:lockRepo))
  $mutexTaken = $false
  $job = $null
  try {
    $mutexTaken = $mutex.WaitOne([TimeSpan]::FromSeconds(2))
    if (-not $mutexTaken) { throw "测试 mutex 获取超时：$Action" }
    $job = Start-Job -ScriptBlock {
      param($lockScriptPath, $repoPath, $requestedAction, $requestedToken)
      $invokeArgs = @{ Action = $requestedAction; RepoRoot = $repoPath }
      if ($requestedToken) { $invokeArgs.Token = $requestedToken }
      if ($requestedAction -eq "renew") { $invokeArgs.LeaseMinutes = 1 }
      & $lockScriptPath @invokeArgs
    } -ArgumentList $script:lockScript, $script:lockRepo, $Action, $Token
    Start-Sleep -Milliseconds 700
    if ($job.State -eq "Completed") { throw "流水线锁动作未等待同仓库 mutex：$Action" }
  }
  finally {
    if ($mutexTaken) { $mutex.ReleaseMutex() }
    $mutex.Dispose()
  }
  Wait-Job $job -Timeout 5 | Out-Null
  if ($job.State -ne "Completed") { throw "mutex 释放后 $Action 未完成" }
  $output = @(Receive-Job $job 2>&1)
  Remove-Job $job -Force
  if (("$output") -notmatch ('"ok":true,"state":"' + $ExpectedState + '"')) {
    throw "mutex 释放后 $Action 未返回预期成功状态 '$ExpectedState'：$output"
  }
}

function Invoke-StateValidation([string]$StatePath) {
  # 负例必须保留校验器的 exit 1，而不能让宿主的 Stop 策略提前终止测试。尤其 Windows
  # PowerShell 5.1 会把 pwsh 的 stderr 包装为 NativeCommandError，所以把该调用的错误流
  # 限定在函数内；返回对象只暴露测试真正需要的退出码与诊断输出。
  $oldPreference = $ErrorActionPreference
  try {
    $ErrorActionPreference = "Continue"
    $output = @(& $script:statePowerShell -NoProfile -File $script:stateScript -RepoRoot $script:stateRepo -StateFile $StatePath 2>&1)
    return [pscustomobject]@{ ExitCode = $LASTEXITCODE; Output = $output }
  }
  finally { $ErrorActionPreference = $oldPreference }
}

$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("ird-pipeline-regression-" + [guid]::NewGuid().ToString("N"))
try {
  # 用隔离真实Git仓库验证管理目录解析；不再用伪.git目录掩盖worktree差异。
  $script:lockRepo = Join-Path $tempRoot "lock-repo"
  New-Item -ItemType Directory -Force (Join-Path $script:lockRepo ".git") | Out-Null
  & git -C $script:lockRepo init --quiet
  Assert-ExitCode 0 $LASTEXITCODE "隔离Git仓库初始化"
  $lockPath = Join-Path $script:lockRepo ".git/ird-pipeline-tick.lock"

  # 初始无锁时也必须由 status 进入同一 mutex；否则它与 acquire/renew/release 的临界范围
  # 会分叉，审计查询就可能观察到半写入内容。
  Assert-LockActionWaitsOnMutex "status" "" "none"

  $first = Invoke-Lock "acquire"
  Assert-ExitCode 0 $first.ExitCode "首次 acquire 必须成功"
  $oldLock = ($first.Output | Select-Object -Last 1 | ConvertFrom-Json)

  # renew 是 P0 风险的写入路径之一：token 核对与 Set-Content 必须一起受 mutex 保护。
  Assert-LockActionWaitsOnMutex "renew" $oldLock.tickId "renewed"
  $oldLock = Get-Content $lockPath -Raw -Encoding UTF8 | ConvertFrom-Json

  # 人为将 A 的租约置为过去，精确模拟 A 崩溃后 B 接管陈锁的场景。
  $oldLock.leaseExpiresAt = [DateTime]::Now.AddMinutes(-1).ToString("yyyy-MM-ddTHH:mm:sszzz")
  [System.IO.File]::WriteAllText($lockPath, ($oldLock | ConvertTo-Json -Compress), [System.Text.UTF8Encoding]::new($false))

  $second = Invoke-Lock "acquire"
  Assert-ExitCode 0 $second.ExitCode "陈锁接管必须成功"
  $newLock = ($second.Output | Select-Object -Last 1 | ConvertFrom-Json)

  $oldRelease = Invoke-Lock "release" $oldLock.tickId
  Assert-ExitCode 4 $oldRelease.ExitCode "旧 token 释放接管后的锁必须被拒绝"
  $stillHeld = Get-Content $lockPath -Raw -Encoding UTF8 | ConvertFrom-Json
  Assert-Equal $newLock.tickId $stillHeld.tickId "旧 token 不得删除或替换接管者的锁"

  # release 是另一条高风险写入路径：token 核对与 Remove-Item 同样不能分离。
  Assert-LockActionWaitsOnMutex "release" $newLock.tickId "released"
  if (Test-Path $lockPath) { throw "接管者 release 后锁文件仍存在" }

  # linked worktree 的 .git 是指向文件，不能将其当目录拼接锁路径。
  # 在隔离真实仓库中走完整获取/拒绝旧 token/释放入口，防 exit0 却无JSON的假成功。
  & git -C $script:lockRepo init --quiet
  Assert-ExitCode 0 $LASTEXITCODE "隔离Git仓库初始化"
  & git -C $script:lockRepo -c user.name=IRD-fixture -c user.email=ird-fixture@example.invalid -c commit.gpgsign=false commit --quiet --allow-empty -m fixture
  Assert-ExitCode 0 $LASTEXITCODE "隔离Git基点提交"
  $primaryRepo = $script:lockRepo
  $script:lockRepo = Join-Path $tempRoot "linked-worktree"
  & git -C $primaryRepo worktree add --quiet --detach $script:lockRepo HEAD
  Assert-ExitCode 0 $LASTEXITCODE "隔离linked worktree创建"
  $linked = Invoke-Lock "acquire"
  Assert-ExitCode 0 $linked.ExitCode "linked worktree acquire"
  if (-not $linked.Output -or "$($linked.Output)" -notmatch '"tickId"') { throw "linked worktree acquire必须返回真实token，不得仅exit0" }
  $linkedToken = ($linked.Output | Select-Object -Last 1 | ConvertFrom-Json).tickId
  $linkedReject = Invoke-Lock "release" "not-owner"
  Assert-ExitCode 4 $linkedReject.ExitCode "linked worktree错误token拒绝"
  $linkedRelease = Invoke-Lock "release" $linkedToken
  Assert-ExitCode 0 $linkedRelease.ExitCode "linked worktree合法token释放"
  $script:lockRepo = $primaryRepo

  # 仅复用真实样例的policy/schema；任务状态在隔离根内固定，避免TK-T01在
  # 主项目已done后把合法返工例错误拒绝。此夹具只测失败证据，不证明队列可领取。
  $script:stateRepo = Join-Path $tempRoot "state-repo"
  $fixtureTasks = Join-Path $script:stateRepo "RobWork/doc/industrial-robot-design/tasks"
  New-Item -ItemType Directory -Force $fixtureTasks | Out-Null
  @{taskId='TK-T01';status='ready';branch='wp02-t01'} | ConvertTo-Json | Set-Content (Join-Path $fixtureTasks 'TK-T01.json') -Encoding UTF8
  $state = Get-Content $sourceState -Raw -Encoding UTF8 | ConvertFrom-Json
  $now = [DateTime]::Now
  $state.history = @()
  $state.queue = @()
  $state.docRefs.PSObject.Properties.Remove('b1BatchManifest')
  $state.phase = "implementing"
  $state.currentTask = "TK-T01"
  $state.branch = "wp02-t01"
  $state.base = ("a" * 40)
  $state.headSha = $null
  $state.acceptedHead = $null
  $state.acceptanceRecord = $null
  $state.run = [pscustomobject]@{
    kind = "implement"; runId = [guid]::NewGuid().ToString(); workerId = [guid]::NewGuid().ToString()
    task = "TK-T01"; startedAt = $now.AddMinutes(-1).ToString("yyyy-MM-ddTHH:mm:sszzz")
    leaseExpiresAt = $now.AddMinutes(30).ToString("yyyy-MM-ddTHH:mm:sszzz")
  }
  $state.attempts.implement = 0
  $state.attempts.fix = 1
  $state.attempts.accept = 1
  $state.heartbeat.updatedAt = $now.ToString("yyyy-MM-ddTHH:mm:sszzz")
  $state.tickToken = [guid]::NewGuid().ToString()

  $statePath = Join-Path $tempRoot "retry-state.json"
  $state.PSObject.Properties.Remove("lastFailureRecord")
  $state | ConvertTo-Json -Depth 12 | Set-Content $statePath -Encoding UTF8
  $missing = Invoke-StateValidation $statePath
  Assert-ExitCode 1 $missing.ExitCode "返工 implementing 状态缺少 lastFailureRecord 必须失败"

  $state | Add-Member -NotePropertyName "lastFailureRecord" -NotePropertyValue ([pscustomobject]@{
    branch = "acc/TK-T01/1"; path = "traceability/acceptance/TK-T01-20260910.md"
    commit = ("b" * 40); attempt = 1
  })
  $state | ConvertTo-Json -Depth 12 | Set-Content $statePath -Encoding UTF8
  $valid = Invoke-StateValidation $statePath
  Assert-ExitCode 0 $valid.ExitCode "合法 lastFailureRecord 的返工状态必须通过"

  # 失败证据必须是最近一次验收：第二轮失败时继续引用 attempt=1 属于陈旧反证，禁止通过。
  $state.attempts.accept = 2
  $state | ConvertTo-Json -Depth 12 | Set-Content $statePath -Encoding UTF8
  $staleAttempt = Invoke-StateValidation $statePath
  Assert-ExitCode 1 $staleAttempt.ExitCode "陈旧 lastFailureRecord.attempt 必须失败"

  # JSON 数字 1 与字符串 "1" 不能等价：后者会让 evidence 分支序号不再是严格的状态契约。
  $state.lastFailureRecord.attempt = "2"
  $state.lastFailureRecord.branch = "acc/TK-T01/2"
  $state | ConvertTo-Json -Depth 12 | Set-Content $statePath -Encoding UTF8
  $nonIntegerAttempt = Invoke-StateValidation $statePath
  Assert-ExitCode 1 $nonIntegerAttempt.ExitCode "字符串 lastFailureRecord.attempt 必须失败"

  $state.lastFailureRecord.attempt = 2
  $state | ConvertTo-Json -Depth 12 | Set-Content $statePath -Encoding UTF8
  $latestAttempt = Invoke-StateValidation $statePath
  Assert-ExitCode 0 $latestAttempt.ExitCode "最近一轮整数 lastFailureRecord.attempt 必须通过"

  # 状态计数/身份与时刻也是契约：原入口验证坏输入被拒，跨偏移按真实时刻判。
  $healthyStateJson = $state | ConvertTo-Json -Depth 20
  foreach ($case in @('fractional-count','string-count','fake-guid','offset-valid','offset-expired','bad-calendar')) {
    $probe = $healthyStateJson | ConvertFrom-Json
    $expected = 1
    switch ($case) {
      'fractional-count' { $probe.tickCount = 1.5 }
      'string-count' { $probe.attempts.accept = '2' }
      'fake-guid' { $probe.tickToken = '-' * 36 }
      'offset-valid' {
        $expected = 0
        $probe.run.startedAt = '2020-01-01T10:00:00+02:00'
        $probe.run.leaseExpiresAt = '2020-01-01T09:30:00+00:00'
        $probe.heartbeat.updatedAt = '2020-01-01T09:00:00+00:00'
      }
      'offset-expired' {
        $probe.run.startedAt = '2020-01-01T08:00:00+00:00'
        $probe.run.leaseExpiresAt = '2020-01-01T09:00:00+02:00'
        $probe.heartbeat.updatedAt = '2020-01-01T09:00:00+00:00'
      }
      'bad-calendar' { $probe.run.startedAt = '2020-99-99T10:00:00+00:00' }
    }
    $probe | ConvertTo-Json -Depth 20 | Set-Content $statePath -Encoding UTF8
    $checked = Invoke-StateValidation $statePath
    Assert-ExitCode $expected $checked.ExitCode "状态边界 $case"
    Write-Output "state-boundary: $case expected=$expected actual=$($checked.ExitCode)"
  }

  # branch 的任务段也必须与 currentTask 对齐，避免把别的任务的失败结论注入当前返工。
  $state.lastFailureRecord.branch = "acc/OTHER-T01/2"
  $state | ConvertTo-Json -Depth 12 | Set-Content $statePath -Encoding UTF8
  $wrongTaskBranch = Invoke-StateValidation $statePath
  Assert-ExitCode 1 $wrongTaskBranch.ExitCode "其他任务的 lastFailureRecord.branch 必须失败"

  # 返工完成后进入 awaiting_acceptance 时，该失败记录必须立刻清除，不能跨阶段残留并被
  # 误解为仍在修复或可合入的证据。
  $state.phase = "awaiting_acceptance"
  $state.headSha = ("c" * 40)
  $state.run = $null
  $state | ConvertTo-Json -Depth 12 | Set-Content $statePath -Encoding UTF8
  $residualFailureRecord = Invoke-StateValidation $statePath
  Assert-ExitCode 1 $residualFailureRecord.ExitCode "非返工阶段残留 lastFailureRecord 必须失败"

  # PIPE §4.7③：使用隔离真实Git的三方index，不模拟合并器内部集合算法。
  # 正例必须完成暂存；大小写分歧必须保留工作文件与三方stage，不能只核非零退出。
  foreach ($case in @('union','note-case','schema-case','missing-stage')) {
    $unionRepo = Join-Path $tempRoot ("union-" + $case)
    New-Item -ItemType Directory -Force $unionRepo | Out-Null
    & git -C $unionRepo init --quiet
    Assert-ExitCode 0 $LASTEXITCODE "并集夹具Git初始化"
    & git -C $unionRepo config core.autocrlf false
    Assert-ExitCode 0 $LASTEXITCODE "并集夹具关闭换行转换"
    # 用户级/模板排除规则不得让临时索引跳过这个被测路径。
    $excludePath = Join-Path $unionRepo ".git/info/exclude"
    if (Test-Path -LiteralPath $excludePath) { [IO.File]::WriteAllText($excludePath, "", [Text.UTF8Encoding]::new($false)) }
    $findingPath = 'RobWork/doc/industrial-robot-design/traceability/findings.json'
    $workingPath = Join-Path $unionRepo $findingPath
    New-Item -ItemType Directory -Force (Split-Path -Parent $workingPath) | Out-Null
    [IO.File]::WriteAllText($workingPath, 'conflict-preserved', [Text.UTF8Encoding]::new($false))
    $indexInfo = @()
    foreach ($stage in 1..3) {
      if ($case -eq 'missing-stage' -and $stage -eq 3) { continue }
      # 固定两空格JSON契合登记簿原有文本格式；中文用于检验Git UTF-8解码与回写。
      $entry = "    {`n      `"id`": `"F-001`",`n      `"title`": `"原始条目`"`n    }"
      if ($stage -gt 1) { $entry += ",`n    {`n      `"id`": `"F-00$stage`",`n      `"title`": `"新增条目`"`n    }" }
      $schema = 'ird-findings/1'; $note = 'RuleA'
      if ($case -eq 'note-case' -and $stage -eq 3) { $note = 'Rulea' }
      if ($case -eq 'schema-case' -and $stage -eq 3) { $schema = 'IRD-FINDINGS/1' }
      $text = "{`n  `"schemaVersion`": `"$schema`",`n  `"note`": `"$note`",`n  `"findings`": [`n$entry`n  ]`n}`n"
      $blobPath = Join-Path $tempRoot "union-stage-$stage.json"
      [IO.File]::WriteAllText($blobPath, $text, [Text.UTF8Encoding]::new($false))
      $blob = & git -C $unionRepo hash-object -w $blobPath
      Assert-ExitCode 0 $LASTEXITCODE "并集夹具写Git对象"
      $indexInfo += "100644 $blob $stage`t$findingPath"
    }
    # Windows PowerShell 5.1 的原生命令管道会以宿主编码写stdin，Git无法稳定解析；
    # 用UTF-8无BOM文件和cmd重定向保持Git index-info为ASCII字节。
    $indexInfoPath = Join-Path $tempRoot ("union-index-" + $case + ".txt")
    [IO.File]::WriteAllText($indexInfoPath, ($indexInfo -join "`n") + "`n", [Text.UTF8Encoding]::new($false))
    & $env:ComSpec /d /c ('"git" -C "' + $unionRepo + '" update-index --index-info < "' + $indexInfoPath + '"')
    Assert-ExitCode 0 $LASTEXITCODE "并集夹具注入三方stage"
    $initialUnmerged = @(& git -C $unionRepo ls-files -u)
    Assert-ExitCode 0 $LASTEXITCODE "核查并集初始三方索引"
    $expectedStageCount = 3
    if ($case -eq 'missing-stage') { $expectedStageCount = 2 }
    Assert-Equal $expectedStageCount $initialUnmerged.Count "并集合并前核对冲突stage数"
    $unionScript = Join-Path $RepoRoot 'RobWork/scripts/industrialrobot/merge-findings-union.ps1'
    $unionOutput = @(& $script:statePowerShell -NoProfile -File $unionScript -RepoRoot $unionRepo 2>&1)
    $unionExit = $LASTEXITCODE
    $unmerged = @(& git -C $unionRepo ls-files -u)
    Assert-ExitCode 0 $LASTEXITCODE "核查并集暂存状态"
    if ($case -eq 'union') {
      Assert-ExitCode 0 $unionExit "正常并集"
      Assert-Equal 0 $unmerged.Count "正常并集应完成暂存"
      $mergedFindings = Get-Content $workingPath -Raw -Encoding UTF8 | ConvertFrom-Json
      Assert-Equal 'F-001,F-002,F-003' ($mergedFindings.findings.id -join ',') "正常并集条目序列"
      Assert-Equal '原始条目' $mergedFindings.findings[0].title "中文不得损坏"
    } else {
      $expectedExit = 1; $reasonPattern = '顶层'
      # 缺stage是调用错误（exit2），不能由PS5.1的NativeCommandError冒充语义拒绝。
      if ($case -eq 'missing-stage') { $expectedExit = 2; $reasonPattern = '三方暂存条目' }
      Assert-ExitCode $expectedExit $unionExit "并集拒绝错误归类"
      Assert-Equal $expectedStageCount $unmerged.Count "拒绝并集须保留原冲突stage"
      Assert-Equal 'conflict-preserved' ([IO.File]::ReadAllText($workingPath)) "拒绝并集须保留原工作文件"
      if ("$unionOutput" -notmatch $reasonPattern) { throw "拒绝原因须符合错误归类，不能由解析失败冒充：$unionOutput" }
    }
    Write-Output "union-boundary: $case actual=$unionExit"
  }

  Write-Output "test-pipeline-pipeline: PASS"
}
finally {
  # 临时目录仅承载测试伪锁与派生状态；无论断言成功或失败都必须清理，避免污染后续审查。
  $resolvedTemp = [IO.Path]::GetFullPath($tempRoot)
  $tempParent = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
  if (-not $resolvedTemp.StartsWith($tempParent, [StringComparison]::OrdinalIgnoreCase)) { throw "临时清理路径越界：$resolvedTemp" }
  if (Test-Path -LiteralPath $resolvedTemp) { Remove-Item -LiteralPath $resolvedTemp -Recurse -Force }
}
