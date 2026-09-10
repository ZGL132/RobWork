# 流水线状态校验（治理脚本）。
#
# 职责：把 automation-pipeline.md（PIPE）§0.2 的 state.json schema 从散文变成机器执行面——
# 守卫 5"流水线自身异常（状态损坏/不可修复矛盾）"的静态判定器。tick 编排者每 tick 最先
# 运行本脚本（PIPE §4.1）；校验失败即视为状态损坏，按守卫 5 转 stopped 等所有者，不得带病续跑。
#
# 校验范围（对应 PIPE §0.2 schema）：
#   ①schemaVersion/phase 枚举；②heartbeat 必须为 ISO 本地时间戳（同日多 tick 可排序，PIPE v1.3）；
#   ③policy 必备字段与取值域；④queue 引用的契约必须存在且非 done；⑤currentTask/branch/base
#   与 phase 的一致性；⑥history 条目形态；⑦docRefs 必备键；⑧pausedReason ≤ 200 字
#   （状态纪律：叙述性豁免必须走 PIPE 增量修订，state.json 只留短事实陈述）。
#
# 用法：pwsh -File validate-state.ps1 [-RepoRoot <仓库根>] [-StateFile <state.json>]
# 仓库根解析与 verify-task.ps1 同一套健壮逻辑（CORE-T01 验收 G-1 教训：不单独依赖 $PSScriptRoot，
# 以 doc 目录存在性自检兜底），保证 -File / -Command、pwsh 7 / Windows PowerShell 5.1 行为一致。
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

# ---------- ① schema 与 phase 枚举 ----------
if ($s.schemaVersion -ne "ird-pipeline/1") { $errors += "schemaVersion must be 'ird-pipeline/1', got '$($s.schemaVersion)'" }
$knownPhases = @("paused","idle","implementing","awaiting_acceptance","awaiting_merge","awaiting_unit_review","blocked","stopped")
if ($knownPhases -notcontains $s.phase) { $errors += "invalid phase: '$($s.phase)'（合法值：$($knownPhases -join '/')）" }

# ---------- ② heartbeat：ISO 时间戳＋无进展计数 ----------
# 兼容两种精度：YYYY-MM-DDTHH:mm(:ss)?，可带 ±HH:MM 偏移或 Z（v1.2.1 实际写法为 2026-09-10T11:55+08:00）。
if ("$($s.heartbeat.updatedAt)" -notmatch "^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}(:\d{2})?(\+\d{2}:\d{2}|Z)?$") {
  $errors += "heartbeat.updatedAt must be ISO datetime 'YYYY-MM-DDTHH:mm[:ss][+HH:MM|Z]'（PIPE v1.3：纯日期无法排序同日 tick），got '$($s.heartbeat.updatedAt)'"
}
if ($null -eq $s.heartbeat.ticksNoProgress -or $s.heartbeat.ticksNoProgress -lt 0) { $errors += "heartbeat.ticksNoProgress must be an integer >= 0" }

# ---------- ③ attempts ----------
foreach ($k in @("implement","fix")) {
  if ($null -eq $s.attempts.$k -or $s.attempts.$k -lt 0) { $errors += "attempts.$k must be an integer >= 0" }
}

# ---------- ④ policy 必备字段（PIPE §0.2：缺字段即状态损坏，不允许"默认隐含"） ----------
$policyBools = @("strictQueueOrder","autoDiscovery","unitCheckpoint","unitCheckpointExemptFirstUnit")
foreach ($k in $policyBools) {
  if ($null -eq $s.policy.$k) { $errors += "policy missing bool: $k" }
  elseif ($s.policy.$k -isnot [bool]) { $errors += "policy.$k must be bool, got '$($s.policy.$k)'" }
}
if ($null -eq $s.policy.autoMerge) { $errors += "policy missing: autoMerge" }
else {
  if ($s.policy.autoMerge.enabled -isnot [bool]) { $errors += "policy.autoMerge.enabled must be bool" }
  if ($null -eq $s.policy.autoMerge.classes) { $errors += "policy.autoMerge.classes must be an array（可为空数组）" }
}
foreach ($k in @("noProgressLimit","maxImplementRestarts","maxFixCycles")) {
  if ($null -eq $s.policy.$k -or $s.policy.$k -lt 0) { $errors += "policy.$k must be an integer >= 0" }
}
if ($s.policy.noProgressLimit -ne $null -and $s.policy.noProgressLimit -lt 1) { $errors += "policy.noProgressLimit must be >= 1" }

# ---------- 构建契约索引（queue/history 引用一致性的数据源） ----------
$taskMap = @{}
Get-ChildItem (Join-Path $docBase "tasks") -Recurse -Filter "*.json" -ErrorAction SilentlyContinue | ForEach-Object {
  if ($_.Name -eq "foundation-tasks.json") { return }  # 数组索引文件不是执行契约（DTB §8）
  $j = Get-Content $_.FullName -Raw -Encoding UTF8 | ConvertFrom-Json
  if ($j -is [array]) { return }
  if ($j.taskId) { $taskMap[$j.taskId] = "$($j.status)" }
}

# ---------- ⑤ queue：引用存在、不重复、不含 done ----------
$seen = @()
$qi = 0
foreach ($q in @($s.queue)) {
  $qi++
  if (-not $taskMap.ContainsKey("$q")) { $errors += "queue[$qi] references unknown task: $q"; continue }
  if ($seen -contains "$q") { $errors += "queue duplicate entry: $q" }
  $seen += "$q"
  if ($taskMap["$q"] -eq "done") { $errors += "queue[$qi] task already done (must be removed on merge): $q" }
}

# ---------- ⑥ currentTask/branch/base 与 phase 一致性 ----------
$needsTask = @("implementing","awaiting_acceptance","awaiting_merge") -contains $s.phase
if ($needsTask -and -not $s.currentTask) { $errors += "phase '$($s.phase)' requires currentTask to be set" }
if ((@("idle","paused") -contains $s.phase) -and $s.currentTask) { $errors += "phase '$($s.phase)' must have currentTask = null" }
if ($s.phase -eq "implementing" -and -not $s.branch) { $errors += "phase 'implementing' requires branch to be set" }
if ($s.currentTask -and -not $taskMap.ContainsKey("$($s.currentTask)")) { $errors += "currentTask references unknown task: $($s.currentTask)" }
if ($s.currentTask -and $taskMap["$($s.currentTask)"] -eq "done") { $errors += "currentTask is already done: $($s.currentTask)" }

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
foreach ($k in @("pipeline","acceptance","agents","dtbTaskRegistry","taskContracts","contractCompilation")) {
  if (-not $s.docRefs -or -not $s.docRefs.$k) { $errors += "docRefs missing key: $k" }
}

# ---------- ⑨ 状态纪律：叙述字段长度上限（豁免走 PIPE 修订，不走自由文本） ----------
if ($s.pausedReason -and $s.pausedReason.Length -gt 200) { $errors += "pausedReason exceeds 200 chars（PIPE §0.2 状态纪律：长叙述移入 PIPE 增量修订或验收记录）" }

if ($errors.Count) { $errors | ForEach-Object { Write-Error $_ }; exit 1 }
Write-Output "validate-state: PASS (phase=$($s.phase), queue=$(@($s.queue).Count), history=$(@($s.history).Count), tasks indexed=$($taskMap.Count))"
