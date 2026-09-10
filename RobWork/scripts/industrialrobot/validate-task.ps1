param([Parameter(Mandatory=$true)][string]$TaskFile)
if (-not (Test-Path $TaskFile)) { throw "task file not found: $TaskFile" }
$tasks = @(Get-Content $TaskFile -Raw | ConvertFrom-Json)
$required = 'taskId','unit','masterWp','title','status','dependsOn','requirements','designRefs','verify'
$readyRequired = 'requirements','designRefs','verify','allowedFiles','forbiddenFiles','outputs','acceptance'
foreach ($t in $tasks) {
  foreach ($k in $required) { if ($null -eq $t.$k) { throw "missing task field: $k" } }
  if ($t.taskId -notmatch '^[A-Z]+-(T[0-9]+|CR-[0-9]+)$') { throw "invalid taskId: $($t.taskId)" }
  if ($t.masterWp -notmatch '^WP-[A-I]$') { throw "invalid masterWp: $($t.masterWp)" }
  if ($t.status -notin 'planned','ready','blocked','done','design-written') { throw "invalid task status: $($t.taskId)" }
  if (@($t.verify).Count -eq 0) { throw "verify must contain at least one command: $($t.taskId)" }
  # ready 表示任务可直接领取，必须具备可评审的需求、范围、产物与验收；
  # planned 允许保留任务占位，但绝不能因字段名存在而被当作已准备就绪。
  if ($t.status -eq 'ready') {
    foreach ($k in $readyRequired) {
      if (@($t.$k).Count -eq 0) { throw "ready task has empty field ${k}: $($t.taskId)" }
    }
  }
}
Write-Output "validate-task: PASS ($($tasks.Count) tasks)"
