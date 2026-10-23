param(
  [string]$RepoRoot = "",
  [string]$BatchFile = ""
)

# 本脚本把 PIPE §6.3 的 B.1 批次顺序变成机器门禁。批次清单不是任务契约，
# 它只回答三个编排问题：哪些任务属于本批、按什么顺序入队、哪一步还有外部前置。
# 业务需求与任务验收仍以各 canonical 契约为准，避免在编排层复制业务语义。
if (-not $RepoRoot) {
  $scriptDir = $PSScriptRoot
  if (-not $scriptDir -or -not (Test-Path $scriptDir)) {
    $scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
  }
  $RepoRoot = (Resolve-Path (Join-Path $scriptDir "..\..\..")).Path
}

$docBase = Join-Path $RepoRoot "RobWork/doc/industrial-robot-design"
if (-not (Test-Path $docBase)) {
  # validate-docs.ps1 的 RepoRoot 口径是源码根 RobWork/，而直接调用本脚本时通常传仓库根。
  # 两种入口都属于项目既有合法调用方式，因此这里只做目录存在性判别，不靠调用方式猜测。
  $sourceDocBase = Join-Path $RepoRoot "doc/industrial-robot-design"
  if (Test-Path $sourceDocBase) {
    $docBase = $sourceDocBase
  } else {
    throw "repo root resolution failed: doc base not found under '$RepoRoot'"
  }
}
if (-not $BatchFile) {
  $BatchFile = Join-Path $docBase "traceability/pipeline/b1-host-integration-batch.json"
}
if (-not (Test-Path $BatchFile)) {
  throw "B.1 batch manifest not found: $BatchFile"
}

$batch = Get-Content $BatchFile -Raw -Encoding UTF8 | ConvertFrom-Json
$errors = @()

if ($batch.schemaVersion -ne "ird-pipeline-batch/1") {
  $errors += "schemaVersion must be ird-pipeline-batch/1"
}
if ($batch.batchId -ne "B1-HOST-INTEGRATION") {
  $errors += "batchId must be B1-HOST-INTEGRATION"
}
if ($batch.status -notin @("planned", "ready", "running", "done", "blocked")) {
  $errors += "invalid batch status: $($batch.status)"
}

$stages = @($batch.stages)
# O-45 裁决（2026-09-29，所有者"解决阻塞"口令）授权治理微任务 WP-14-T11 插入批次序 10，
# 阶段数 11→12（PIPE §6.3 v1.18 批次插入例外条款——插入不改变既有阶段相对序）。
if ($stages.Count -ne 12) {
  $errors += "B.1 batch must contain exactly 12 implementation stages, got $($stages.Count)"
}

$seenTasks = @{}
$seenStageIds = @{}
$previousTask = "DOC-T14"
for ($i = 0; $i -lt $stages.Count; $i++) {
  $stage = $stages[$i]
  $expectedOrder = $i + 1
  if ($stage.order -ne $expectedOrder) {
    $errors += "stage order must be contiguous: expected $expectedOrder, got $($stage.order)"
  }
  if (-not $stage.stageId -or $seenStageIds.ContainsKey("$($stage.stageId)")) {
    $errors += "missing or duplicate stageId at order $expectedOrder"
  } else {
    $seenStageIds["$($stage.stageId)"] = $true
  }
  if (-not $stage.taskId -or $seenTasks.ContainsKey("$($stage.taskId)")) {
    $errors += "missing or duplicate taskId at order $expectedOrder"
    continue
  }
  $seenTasks["$($stage.taskId)"] = $true

  $contractFile = Join-Path $docBase "$($stage.contractPath)"
  if (-not (Test-Path $contractFile)) {
    $errors += "contractPath not found for $($stage.taskId): $($stage.contractPath)"
    continue
  }
  $contract = Get-Content $contractFile -Raw -Encoding UTF8 | ConvertFrom-Json
  if ($contract.taskId -ne $stage.taskId) {
    $errors += "contract taskId mismatch at order $expectedOrder"
  }
  if ($contract.branch -ne $stage.branch) {
    $errors += "contract branch mismatch for $($stage.taskId)"
  }
  if ($contract.status -notin @("planned", "ready", "done", "blocked")) {
    $errors += "invalid contract status for $($stage.taskId): $($contract.status)"
  }
  if (@($contract.dependsOn) -notcontains $previousTask) {
    $errors += "$($stage.taskId) must depend on previous batch task $previousTask"
  }
  $previousTask = "$($stage.taskId)"
}

$gateMap = @{}
foreach ($gate in @($batch.externalGates)) {
  if (-not $gate.gateId -or $gateMap.ContainsKey("$($gate.gateId)")) {
    $errors += "external gate id missing or duplicated"
    continue
  }
  $gateMap["$($gate.gateId)"] = $gate
  if ($gate.status -notin @("open", "satisfied")) {
    $errors += "invalid external gate status for $($gate.gateId): $($gate.status)"
  }
  if (-not $seenTasks.ContainsKey("$($gate.beforeTask)")) {
    $errors += "external gate targets unknown batch task: $($gate.beforeTask)"
  }
}
foreach ($stage in $stages) {
  foreach ($gateId in @($stage.requiredExternalGates)) {
    if (-not $gateMap.ContainsKey("$gateId")) {
      $errors += "$($stage.taskId) references unknown external gate: $gateId"
    }
  }
}

# WP-24-T08 的 T03b 前置是当前批次唯一无法写入 dependsOn 的真实外部门禁。
# 在 canonical WP-24-T03 契约补建前，清单必须显式携带该门，防评审时遗漏后误放行。
$wp24t08 = $stages | Where-Object { $_.taskId -eq "WP-24-T08" }
if (-not $wp24t08 -or @($wp24t08.requiredExternalGates) -notcontains "WP-24-T03-COMPLETE") {
  $errors += "WP-24-T08 must require external gate WP-24-T03-COMPLETE"
}

if ($errors.Count -gt 0) {
  $errors | ForEach-Object { Write-Error $_ }
  exit 1
}

$openGates = @($batch.externalGates | Where-Object { $_.status -eq "open" } | ForEach-Object { $_.gateId })
Write-Output "validate-b1-batch: PASS (stages=$($stages.Count), openGates=$($openGates.Count))"
if ($openGates.Count -gt 0) {
  Write-Output "open-gates: $($openGates -join ',')"
}
