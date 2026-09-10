param([string]$RepoRoot = (Resolve-Path "$PSScriptRoot\..\..").Path)
$base = Join-Path $RepoRoot 'doc/industrial-robot-design'
$errors = @()
$status = Get-Content (Join-Path $base 'traceability/unit-status.json') -Raw | ConvertFrom-Json
$ids = @($status.units | ForEach-Object { $_.id })
if ($ids.Count -ne 20 -or ($ids | Sort-Object -Unique).Count -ne 20) { $errors += 'unit-status must contain 20 unique units' }
foreach ($u in $status.units) { if ($u.status -eq "draft" -and -not (Test-Path (Join-Path $base $u.design))) { $errors += "missing design: $($u.design)" } }
$taskFiles = @(Get-ChildItem (Join-Path $base "tasks") -Recurse -Filter "*.json" -ErrorAction SilentlyContinue | Where-Object { $_.Name -ne "foundation-tasks.json" })
$taskIds = @()
foreach ($f in $taskFiles) {
  $x = Get-Content $f.FullName -Raw | ConvertFrom-Json
  if ($x -is [array]) { $errors += "array task file must be named foundation-tasks.json: $($f.Name)"; continue }
  if ($x.taskId -notmatch "^[A-Z]+-(T[0-9]+|CR-[0-9]+)$") { $errors += "invalid task file: $($f.Name)" }
  $taskIds += $x.taskId
}
if (($taskIds | Sort-Object -Unique).Count -ne $taskIds.Count) { $errors += "duplicate task ids" }
$trace = Get-Content (Join-Path $base 'traceability/requirements-to-units.json') -Raw | ConvertFrom-Json
foreach ($e in $trace.entries) { if ($ids -notcontains $e.unit) { $errors += "unknown unit $($e.unit)" }; if ($e.masterWp -notmatch '^WP-[A-I]$') { $errors += "invalid master WP" } }
if ($errors.Count) { $errors | ForEach-Object { Write-Error $_ }; exit 1 }
Write-Output "validate-docs: PASS ($($ids.Count) units, $($trace.entries.Count) trace entries, $($taskFiles.Count) task files)"\n
