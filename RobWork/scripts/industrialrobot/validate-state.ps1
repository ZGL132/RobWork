# 流水线状态校验（治理脚本）。
#
# 职责：把 automation-pipeline.md（PIPE）§0.2 的 state.json schema 从散文变成机器执行面——
# 守卫 5"流水线自身异常（状态损坏/不可修复矛盾）"的静态判定器。tick 编排者每 tick 最先
# 运行本脚本（PIPE §4.1）；校验失败即视为状态损坏，按守卫 5 转 stopped 等所有者，不得带病续跑。
#
# v2 增量（PIPE v1.4，审核结论 P0-1/P0-2/P1-5 对策）：
#   ⑨run 租约对象：派发中的工作者必须可识别、可判存活（kind/runId/workerId/ISO 时间），
#     phase 与 run 的存在性/种类强一致——"未超时的 implementing 只汇报不续派"的机器前提；
#   ⑩SHA 冻结链：implementing ⇒ base 为 40 位 SHA；awaiting_acceptance ⇒ base+headSha；
#     awaiting_merge ⇒ acceptedHead＋结构化 acceptanceRecord{branch,path,commit}——
#     合并按 SHA 执行、验收后分支再动即可被检出（防未验收提交混入合入）；
#   ⑪queue 结构化条目：{taskId,contractPath,branch} 三键必填、路径存在、任务非 done；
#     并输出队首"可领取性"结论（status=ready 且 dependsOn 全 done）——编排者不读契约正文
#     即可完成守卫 4 判定（P1-5：自足性与禁读契约的矛盾消解）；
#   ⑫firstUnitCheckpointExempted：首单元检查点豁免的一次性消费标记（P2）。
#
# 用法：pwsh -File validate-state.ps1 [-RepoRoot <仓库根>] [-StateFile <state.json>]
# 仓库根解析与 verify-task.ps1 同一套健壮逻辑（CORE-T01 验收 G-1 教训），调用方式无关。
param(
  [string]$RepoRoot = "",
  [string]$StateFile = ""
)
if (-not $RepoRoot) {
  $scriptDir = $PSScriptRoot
  if (-not $scriptDir -or -not (Test-Path $scriptDir)) { $scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path }
  $RepoRoot = (Resolve-Path (Join-Path $scriptDir "..\..\..")).Path
}
$docBase = Join-Path $RepoRoot "RobWork/doc/industrial-robot-design"
if (-not (Test-Path $docBase)) { throw "repo root resolution failed: doc base not found under '$RepoRoot'（请用 -RepoRoot 显式指定仓库根）" }
if (-not $StateFile) { $StateFile = Join-Path $docBase "traceability/pipeline/state.json" }
if (-not (Test-Path $StateFile)) { throw "state file not found: $StateFile" }

$s = Get-Content $StateFile -Raw -Encoding UTF8 | ConvertFrom-Json
$errors = @()
# 时间戳归一化：pwsh 7 的 ConvertFrom-Json 会把秒精度 ISO 串解析为 [DateTime]（字符串化后变
# 本地格式，打穿 ISO 正则；PS 5.1 则保持 String）——跨 shell 行为差异在此统一抹平。
function Convert-ToIso([object]$v) {
  if ($null -eq $v) { return $null }
  if ($v -is [datetime]) { return $v.ToString("yyyy-MM-ddTHH:mm:sszzz") }
  return "$v"
}
# 同格式 ISO 字符串的字典序比较（秒精度＋同偏移写下，字典序即时间序；PS5.1/pwsh7 行为一致）
function Compare-Iso([string]$a, [string]$b) { if ($a -lt $b) { -1 } elseif ($a -gt $b) { 1 } else { 0 } }
# ISO 时间戳口径：秒精度＋可选本地偏移/Z（与 pipeline-lock.ps1 写入口径一致）
$isoPattern = "^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}(:\d{2})?([+-]\d{2}:\d{2}|Z)?$"
$shaPattern = "^[0-9a-f]{40}$"

# ---------- ① schema 与 phase 枚举 ----------
if ($s.schemaVersion -ne "ird-pipeline/1") { $errors += "schemaVersion must be 'ird-pipeline/1', got '$($s.schemaVersion)'" }
$knownPhases = @("paused","idle","implementing","awaiting_acceptance","awaiting_merge","awaiting_unit_review","blocked","stopped")
if ($knownPhases -notcontains $s.phase) { $errors += "invalid phase: '$($s.phase)'（合法值：$($knownPhases -join '/')）" }

# ---------- ② heartbeat：ISO 时间戳＋无进展计数 ----------
$heartbeatAt = Convert-ToIso $s.heartbeat.updatedAt
if ($heartbeatAt -notmatch $isoPattern) {
  $errors += "heartbeat.updatedAt must be ISO datetime 'YYYY-MM-DDTHH:mm[:ss][+HH:MM|Z]'（PIPE v1.3：纯日期无法排序同日 tick），got '$heartbeatAt'"
}
if ($null -eq $s.heartbeat.ticksNoProgress -or $s.heartbeat.ticksNoProgress -lt 0) { $errors += "heartbeat.ticksNoProgress must be an integer >= 0" }
if ($null -eq $s.tickCount -or $s.tickCount -lt 0) { $errors += "tickCount must be an integer >= 0（v1.4：锁内 tickId 的状态侧计数）" }

# ---------- ③ attempts ----------
foreach ($k in @("implement","fix")) {
  if ($null -eq $s.attempts.$k -or $s.attempts.$k -lt 0) { $errors += "attempts.$k must be an integer >= 0" }
}

# ---------- ④ policy 必备字段（PIPE §0.2：缺字段即状态损坏，不允许"默认隐含"） ----------
$policyBools = @("strictQueueOrder","autoDiscovery","unitCheckpoint","unitCheckpointExemptFirstUnit","firstUnitCheckpointExempted")
foreach ($k in $policyBools) {
  if ($null -eq $s.policy.$k) { $errors += "policy missing bool: $k" }
  elseif ($s.policy.$k -isnot [bool]) { $errors += "policy.$k must be bool, got '$($s.policy.$k)'" }
}
if ($null -eq $s.policy.autoMerge) { $errors += "policy missing: autoMerge" }
else {
  if ($s.policy.autoMerge.enabled -isnot [bool]) { $errors += "policy.autoMerge.enabled must be bool" }
  if ($null -eq $s.policy.autoMerge.classes) { $errors += "policy.autoMerge.classes must be an array（可为空数组）" }
  else {
    # classes 词表（PIPE §6.1 执行语义表）：超出词表的"配置"从未被任何分支消费，即误导性配置
    $classVocab = @("doc","build","implementation")
    foreach ($c in @($s.policy.autoMerge.classes)) {
      if ($classVocab -notcontains "$c") { $errors += "policy.autoMerge.classes invalid entry '$c'（词表：doc/build/implementation，PIPE §6.1）" }
    }
  }
}
foreach ($k in @("noProgressLimit","maxImplementRestarts","maxFixCycles")) {
  if ($null -eq $s.policy.$k -or $s.policy.$k -lt 0) { $errors += "policy.$k must be an integer >= 0" }
}
if ($s.policy.noProgressLimit -ne $null -and $s.policy.noProgressLimit -lt 1) { $errors += "policy.noProgressLimit must be >= 1" }
if ($null -eq $s.policy.leaseMinutes -or $null -eq $s.policy.leaseMinutes.implement -or $null -eq $s.policy.leaseMinutes.acceptance) {
  $errors += "policy.leaseMinutes must contain implement & acceptance (minutes >= 1)"
}
elseif ($s.policy.leaseMinutes.implement -lt 1 -or $s.policy.leaseMinutes.acceptance -lt 1) { $errors += "policy.leaseMinutes values must be >= 1" }

# ---------- 构建契约索引（queue/history 引用一致性＋可领取性判定的数据源） ----------
$taskMap = @{}
Get-ChildItem (Join-Path $docBase "tasks") -Recurse -Filter "*.json" -ErrorAction SilentlyContinue | ForEach-Object {
  if ($_.Name -eq "foundation-tasks.json") { return }  # 数组索引文件不是执行契约（DTB §8）
  $j = Get-Content $_.FullName -Raw -Encoding UTF8 | ConvertFrom-Json
  if ($j -is [array]) { return }
  if ($j.taskId) { $taskMap[$j.taskId] = $j }
}

# ---------- ⑤ queue：结构化条目（v1.4）——taskId/contractPath/branch 三键，路径存在，非 done ----------
$claimability = $null
$seen = @()
$qi = 0
foreach ($q in @($s.queue)) {
  $qi++
  if ($q -is [string]) { $errors += "queue[$qi] must be a structured entry {taskId,contractPath,branch}, got plain string '$q'（v1.4：编排者自足性）"; continue }
  foreach ($k in @("taskId","contractPath","branch")) {
    if (-not $q.$k) { $errors += "queue[$qi] missing key: $k" }
  }
  if (-not $q.taskId) { continue }
  if ($seen -contains "$($q.taskId)") { $errors += "queue duplicate entry: $($q.taskId)" }
  $seen += "$($q.taskId)"
  if (-not $taskMap.ContainsKey("$($q.taskId)")) { $errors += "queue[$qi] references unknown task: $($q.taskId)"; continue }
  $tj = $taskMap["$($q.taskId)"]
  if ("$($tj.status)" -eq "done") { $errors += "queue[$qi] task already done (must be removed on merge): $($q.taskId)" }
  if ($q.contractPath) {
    # contractPath 相对 docBase（与 docRefs 同口径）
    if (-not (Test-Path (Join-Path $docBase "$($q.contractPath)"))) { $errors += "queue[$qi] contractPath not found: $($q.contractPath)" }
    elseif ("$($q.taskId)" -ne "$($tj.taskId)") { $errors += "queue[$qi] contractPath does not contain claimed task: $($q.taskId)" }
  }
  if ($q.branch -and "$($q.branch)" -notmatch '^[a-z][a-z0-9]*-t[0-9]+$') { $errors += "queue[$qi] invalid branch '$($q.branch)'（expect like wp03-t01 / doc-t03）" }
  # 队首可领取性（仅报告，不作为 schema 错误——队首不可领是合法等待态）
  if ($qi -eq 1) {
    $undone = @(); foreach ($d in @($tj.dependsOn)) { if ("$($taskMap["$d"].status)" -ne "done") { $undone += "$d" } }
    $claimable = ("$($tj.status)" -eq "ready" -and $undone.Count -eq 0)
    $reason = if ("$($tj.status)" -ne "ready") { "status=$($tj.status)" } elseif ($undone.Count -gt 0) { "deps not done: $($undone -join ',')" } else { "ready, deps satisfied" }
    $claimability = @{ task = "$($q.taskId)"; claimable = $claimable; reason = $reason }
  }
}

# ---------- ⑥ currentTask/branch/base/run 与 phase 一致性（v1.4 强化：SHA 冻结链＋run 租约） ----------
$needsTask = @("implementing","awaiting_acceptance","awaiting_merge") -contains $s.phase
if ($needsTask -and -not $s.currentTask) { $errors += "phase '$($s.phase)' requires currentTask to be set" }
if ((@("idle","paused") -contains $s.phase) -and $s.currentTask) { $errors += "phase '$($s.phase)' must have currentTask = null" }
if ($s.currentTask -and -not $taskMap.ContainsKey("$($s.currentTask)")) { $errors += "currentTask references unknown task: $($s.currentTask)" }
if ($s.currentTask -and "$($taskMap["$($s.currentTask)"].status)" -eq "done") { $errors += "currentTask is already done: $($s.currentTask)" }

if ($s.phase -eq "implementing") {
  if (-not $s.branch) { $errors += "phase 'implementing' requires branch to be set" }
  if ("$($s.base)" -notmatch $shaPattern) { $errors += "phase 'implementing' requires base = 40-hex SHA（领取时冻结的 redesign-main 基线，v1.4）" }
}
if ($s.phase -eq "awaiting_acceptance") {
  if ("$($s.base)" -notmatch $shaPattern) { $errors += "phase 'awaiting_acceptance' requires base = 40-hex SHA" }
  if ("$($s.headSha)" -notmatch $shaPattern) { $errors += "phase 'awaiting_acceptance' requires headSha = 40-hex SHA（实施完成时冻结的分支尖端，v1.4）" }
}
if ($s.phase -eq "awaiting_merge") {
  if ("$($s.acceptedHead)" -notmatch $shaPattern) { $errors += "phase 'awaiting_merge' requires acceptedHead = 40-hex SHA（验收通过的被冻结提交，v1.4）" }
  if ($s.acceptedHead -and "$($s.headSha)" -ne "$($s.acceptedHead)") { $errors += "awaiting_merge: headSha ('$($s.headSha)') != acceptedHead ('$($s.acceptedHead)')——验收对象必须与送验对象一致" }
  if ($null -eq $s.acceptanceRecord -or -not $s.acceptanceRecord.branch -or -not $s.acceptanceRecord.path -or -not $s.acceptanceRecord.commit) {
    $errors += "phase 'awaiting_merge' requires structured acceptanceRecord {branch,path,commit}（验收证据定位，v1.4）"
  }
  else {
    if ("$($s.acceptanceRecord.branch)" -notmatch '^acc/') { $errors += "acceptanceRecord.branch must match ^acc/（验收记录独立分支，ACC §3）" }
    if ("$($s.acceptanceRecord.path)" -notmatch '^traceability/acceptance/.+\.md$') { $errors += "acceptanceRecord.path must be under traceability/acceptance/ and end .md" }
    if ("$($s.acceptanceRecord.commit)" -notmatch '^[0-9a-f]{7,40}$') { $errors += "acceptanceRecord.commit must be 7-40 hex" }
  }
}
if (@("idle","paused","implementing","awaiting_acceptance","awaiting_merge","awaiting_unit_review","blocked","stopped") -contains $s.phase) {
  # headSha/acceptedHead 只在对应阶段有意义；阶段回退（fail 返工）时必须清空，防陈旧 SHA 误合并
  if (@("idle","paused","blocked") -contains $s.phase) {
    if ($s.headSha -or $s.acceptedHead) { $errors += "phase '$($s.phase)' must have headSha/acceptedHead = null（陈旧 SHA 不得残留）" }
  }
}

# run 租约对象（v1.4）：implementing ⇒ run{kind=implement}；awaiting_acceptance ⇒ run 为 null（待派）或 run{kind=acceptance}（在验）
$hasRun = ($null -ne $s.run)
if ($s.phase -eq "implementing") {
  if (-not $hasRun) { $errors += "phase 'implementing' requires run object（工作者租约——未超时只汇报不续派的机器前提）" }
}
elseif ($hasRun -and $s.phase -ne "awaiting_acceptance") {
  $errors += "phase '$($s.phase)' must have run = null（无派发中的工作者）"
}
if ($hasRun) {
  foreach ($k in @("kind","runId","workerId","startedAt","leaseExpiresAt","task")) {
    if (-not $s.run.$k) { $errors += "run missing key: $k" }
  }
  if ($s.run.kind -and @("implement","acceptance") -notcontains "$($s.run.kind)") { $errors += "run.kind must be implement|acceptance, got '$($s.run.kind)'" }
  $runStart = Convert-ToIso $s.run.startedAt
  $runLease = Convert-ToIso $s.run.leaseExpiresAt
  if ($runStart -and $runStart -notmatch $isoPattern) { $errors += "run.startedAt must be ISO datetime" }
  if ($runLease -and $runLease -notmatch $isoPattern) { $errors += "run.leaseExpiresAt must be ISO datetime" }
  if ($runStart -match $isoPattern -and $runLease -match $isoPattern -and ((Compare-Iso $runLease $runStart) -lt 0)) {
    $errors += "run.leaseExpiresAt must be >= run.startedAt"
  }
  if ($s.run.task -and $s.currentTask -and "$($s.run.task)" -ne "$($s.currentTask)") { $errors += "run.task ('$($s.run.task)') != currentTask ('$($s.currentTask)')" }
  if ("$($s.run.kind)" -eq "acceptance" -and $s.phase -ne "awaiting_acceptance") { $errors += "run.kind=acceptance only valid in awaiting_acceptance" }
}

# ---------- ⑦ history 条目形态（结构化 failReason 为可选字段，PIPE §0.2） ----------
$hi = 0
foreach ($h in @($s.history)) {
  $hi++
  foreach ($k in @("task","unit","result","commits","mergedAt")) {
    if ($null -eq $h.$k) { $errors += "history[$hi] missing field: $k" }
  }
  if ($h.task -and -not $taskMap.ContainsKey("$($h.task)")) { $errors += "history[$hi] references unknown task: $($h.task)" }
}

# ---------- ⑧ docRefs 必备键（tick 三输入的指针，PIPE §0.1） ----------
foreach ($k in @("pipeline","acceptance","contractCompilation","agents","dtbTaskRegistry","taskContracts")) {
  if (-not $s.docRefs -or -not $s.docRefs.$k) { $errors += "docRefs missing key: $k" }
}

# ---------- ⑨ 状态纪律：叙述字段长度上限（豁免走 PIPE 修订，不走自由文本） ----------
if ($s.pausedReason -and $s.pausedReason.Length -gt 200) { $errors += "pausedReason exceeds 200 chars（PIPE §0.2 状态纪律：长叙述移入 PIPE 增量修订或验收记录）" }

if ($errors.Count) { $errors | ForEach-Object { Write-Error $_ }; exit 1 }
$headLine = "validate-state: PASS (phase=$($s.phase), queue=$(@($s.queue).Count), history=$(@($s.history).Count), tasks indexed=$($taskMap.Count))"
if ($claimability) {
  Write-Output "$headLine"
  Write-Output ("queue-head: {0} claimable={1} ({2})" -f $claimability.task, ($claimability.claimable.ToString().ToLower()), $claimability.reason)
} else {
  Write-Output "$headLine"
  Write-Output "queue-head: (empty)"
}
