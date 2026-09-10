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
    $output = @(& pwsh -NoProfile -File $script:stateScript -RepoRoot $script:RepoRoot -StateFile $StatePath 2>&1)
    return [pscustomobject]@{ ExitCode = $LASTEXITCODE; Output = $output }
  }
  finally { $ErrorActionPreference = $oldPreference }
}

$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("ird-pipeline-regression-" + [guid]::NewGuid().ToString("N"))
try {
  # 为锁测试准备独立仓库壳。pipeline-lock 只要求 .git 目录存在，因此无需初始化真实 Git 仓库。
  $script:lockRepo = Join-Path $tempRoot "lock-repo"
  New-Item -ItemType Directory -Force (Join-Path $script:lockRepo ".git") | Out-Null
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

  # 从真实 paused 样例派生一个最小返工状态；所有队列、policy 和文档指针保持原样，
  # 仅填充 implementing 运行态所需字段，以便测试专注于失败证据 schema。
  $state = Get-Content $sourceState -Raw -Encoding UTF8 | ConvertFrom-Json
  $now = [DateTime]::Now
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

  Write-Output "test-pipeline-pipeline: PASS"
}
finally {
  # 临时目录仅承载测试伪锁与派生状态；无论断言成功或失败都必须清理，避免污染后续审查。
  if (Test-Path $tempRoot) { Remove-Item -LiteralPath $tempRoot -Recurse -Force }
}
