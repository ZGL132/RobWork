param([string]$RepoRoot = (Resolve-Path "$PSScriptRoot\..\..").Path)
$base = Join-Path $RepoRoot 'doc/industrial-robot-design'
$errors = @()
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
  if ($x.taskId -notmatch "^[A-Z]+-(T[0-9]+|CR-[0-9]+)$") { $errors += "invalid task file: $($f.Name)" }
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
if ($errors.Count) { $errors | ForEach-Object { Write-Error $_ }; exit 1 }
Write-Output "validate-docs: PASS ($($ids.Count) units, $($trace.entries.Count) trace entries, $($taskFiles.Count) task files)"
