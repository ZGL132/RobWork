param([string]$RepoRoot = (Resolve-Path "$PSScriptRoot\..\..").Path)
$base = Join-Path $RepoRoot 'doc/industrial-robot-design'
$errors = @()
# PIPE/ACC 是定时执行器的可执行输入，不能只检查文件存在：以下锚点把 P0 并发临界区、
# 验收失败证据回传与 evidence 分支命名的关键语义纳入文档门禁，防止后续修订悄然删回旧规则。
$pipelineText = Get-Content (Join-Path $base 'automation-pipeline.md') -Raw -Encoding UTF8
$acceptanceText = Get-Content (Join-Path $base 'acceptance-protocol.md') -Raw -Encoding UTF8
if ($pipelineText -notmatch 'Windows 命名 mutex') { $errors += 'PIPE missing Windows named mutex critical-section rule (P0)' }
if ($pipelineText -notmatch 'lastFailureRecord') { $errors += 'PIPE missing retry failure-evidence record rule (P1)' }
if ($pipelineText -notmatch '阻塞等待返回后再次 renew') { $errors += 'PIPE missing post-wait renew rule (P0)' }
if ($pipelineText -notmatch 'b1-host-integration-batch.json') { $errors += 'PIPE missing B.1 batch manifest rule' }
if ($pipelineText -notmatch '禁止使用子智能体') { $errors += 'PIPE missing current single-session execution rule' }
$b1BatchValidator = Join-Path $RepoRoot 'RobWork/scripts/industrialrobot/validate-b1-batch.ps1'
if (-not (Test-Path $b1BatchValidator)) { $b1BatchValidator = Join-Path $RepoRoot 'scripts/industrialrobot/validate-b1-batch.ps1' }
if (-not (Test-Path $b1BatchValidator)) {
  $errors += 'missing B.1 batch validator'
} else {
  $b1Result = $null
  try { $b1Result = & $b1BatchValidator -RepoRoot $RepoRoot 2>&1 } catch { $b1Result = "ERROR: $_" }
  if ("$b1Result" -notmatch 'validate-b1-batch: PASS') { $errors += 'B.1 batch manifest validation failed' }
}
if ($acceptanceText -match '落在 `acc/<taskId>` 分支') { $errors += 'ACC still contains stale acc/<taskId> evidence-branch wording' }
if ($acceptanceText -notmatch 'acc/<taskId>/<attempt>') { $errors += 'ACC missing attempt-scoped evidence branch wording' }
$status = Get-Content (Join-Path $base 'traceability/unit-status.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$ids = @($status.units | ForEach-Object { $_.id })
if ($ids.Count -ne 20 -or ($ids | Sort-Object -Unique).Count -ne 20) { $errors += 'unit-status must contain 20 unique units' }
foreach ($u in $status.units) {
  if ($u.designCompletion -notin 'written','not-written') { $errors += "invalid designCompletion: $($u.id)" }
  if ($u.designCompletion -eq 'written' -and -not (Test-Path (Join-Path $base $u.design))) { $errors += "missing written design: $($u.design)" }
}
$taskFiles = @(Get-ChildItem (Join-Path $base "tasks") -Recurse -Filter "*.json" -ErrorAction SilentlyContinue | Where-Object { $_.Name -ne "foundation-tasks.json" })
$taskIds = @()
foreach ($f in $taskFiles) {
  $x = Get-Content $f.FullName -Raw -Encoding UTF8 | ConvertFrom-Json
  if ($x -is [array]) { $errors += "array task file must be named foundation-tasks.json: $($f.Name)"; continue }
  if ($x.taskId -notmatch "^([A-Z]+-(T[0-9]+|CR-[0-9]+)|WP-[0-9]+-T[0-9]+)$") { $errors += "invalid task file: $($f.Name)" }
  $taskIds += $x.taskId
}
if (($taskIds | Sort-Object -Unique).Count -ne $taskIds.Count) { $errors += "duplicate task ids" }
$trace = Get-Content (Join-Path $base 'traceability/requirements-to-units.json') -Raw -Encoding UTF8 | ConvertFrom-Json
foreach ($e in $trace.entries) { if ($ids -notcontains $e.unit) { $errors += "unknown unit $($e.unit)" }; if ($e.masterWp -notmatch '^WP-[A-I]$') { $errors += "invalid master WP" } }
$phaseIndexPath = Join-Path $base 'traceability/phase-one-task-index.json'
if (-not (Test-Path $phaseIndexPath)) {
  $errors += 'missing phase-one task index'
} else {
  $phaseIndex = Get-Content $phaseIndexPath -Raw -Encoding UTF8 | ConvertFrom-Json
  $phaseTaskIds = @($phaseIndex.entries | ForEach-Object { $_.taskId })
  if ($phaseTaskIds.Count -ne ($phaseTaskIds | Sort-Object -Unique).Count) { $errors += 'duplicate phase-one task ids' }
  foreach ($entry in $phaseIndex.entries) {
    if ($ids -notcontains $entry.unit) { $errors += "phase index has unknown unit: $($entry.unit)"; continue }
    $designPath = Join-Path $base $entry.design
    if (-not (Test-Path $designPath)) { $errors += "phase index design missing: $($entry.design)"; continue }
    $designText = Get-Content $designPath -Raw -Encoding UTF8
    if (-not $designText.Contains($entry.sourceRow)) { $errors += "phase index task row mismatch: $($entry.taskId)" }
  }
}
# ---------- 资产清单门禁（Agent-native 治理 Task 1，2026-10-17） ----------
# context/README.md 定义七类资产与默认上下文规则，asset-inventory.json 是机读登记簿。
# 本段把"清单完整性"变成机器检查：路径存在、分类/字段/布尔类型合法、canonicalLinks
# 可解析、设计树根级资产全覆盖——新增根级文件/目录未登记即报错，防止 Agent 上下文
# 地图缺位失真（清单字段口径见 context/README.md §6）。
$invKinds = @('authority','workflow','task-source','generated','evidence','decision','archive')
$invOwners = @('owner','pipeline','sessions','script')
$assetCount = 0
$contextReadme = Join-Path $base 'context/README.md'
$inventoryPath = Join-Path $base 'context/asset-inventory.json'
if (-not (Test-Path $contextReadme)) { $errors += 'missing context/README.md (asset classification rules)' }
if (-not (Test-Path $inventoryPath)) {
  $errors += 'missing context/asset-inventory.json'
} else {
  $inv = $null
  try { $inv = Get-Content $inventoryPath -Raw -Encoding UTF8 | ConvertFrom-Json } catch { $errors += "asset-inventory.json is not valid JSON: $_" }
  if ($inv) {
    if ($inv.schemaVersion -ne 'ird-asset-inventory/1') { $errors += "asset-inventory schemaVersion must be 'ird-asset-inventory/1', got '$($inv.schemaVersion)'" }
    $registered = @()
    foreach ($a in @($inv.assets)) {
      foreach ($f in 'path','kind','owner','defaultContext','generated','canonicalLinks') {
        if ($a.PSObject.Properties.Name -notcontains $f) { $errors += "asset-inventory entry missing field '$f': $($a.path)" }
      }
      if ($a.kind -notin $invKinds) { $errors += "asset-inventory invalid kind '$($a.kind)': $($a.path)" }
      if ($a.owner -notin $invOwners) { $errors += "asset-inventory invalid owner '$($a.owner)': $($a.path)" }
      if ($a.defaultContext -isnot [bool]) { $errors += "asset-inventory defaultContext must be boolean: $($a.path)" }
      if ($a.generated -isnot [bool]) { $errors += "asset-inventory generated must be boolean: $($a.path)" }
      if ($registered -contains $a.path) { $errors += "asset-inventory duplicate path: $($a.path)" }
      $registered += $a.path
      if (-not (Test-Path (Join-Path $base $a.path))) { $errors += "asset-inventory path not found: $($a.path)"; continue }
      # canonicalLinks：相对设计树根的路径，可带 #锚点（锚段无文件系统语义，校验时剥离只验路径存在）
      foreach ($link in @($a.canonicalLinks)) {
        if (-not (Test-Path (Join-Path $base ($link -replace '#.*$','')))) { $errors += "asset-inventory broken canonicalLink '$link' at: $($a.path)" }
      }
    }
    # 根级全覆盖：设计树根下每个文件/目录都必须已登记（-contains 大小写不敏感，匹配 Windows 文件系统语义）。
    # 点前缀条目跳过——那是工具本地目录（如 .zcode 工作流运行产物，自含 .gitignore 自忽略），
    # 不是设计资产，不进清单（context/README.md §6 规则 5）。
    foreach ($r in @(Get-ChildItem $base -Force | Where-Object { $_.Name -notlike '.*' } | ForEach-Object { $_.Name })) {
      if ($registered -notcontains $r) { $errors += "unregistered root-level asset: $r" }
    }
    $assetCount = @($inv.assets).Count
  }
}
if ($errors.Count) { $errors | ForEach-Object { Write-Error $_ }; exit 1 }
Write-Output "validate-docs: PASS ($($ids.Count) units, $($trace.entries.Count) trace entries, $($taskFiles.Count) task files, $assetCount registered assets)"
