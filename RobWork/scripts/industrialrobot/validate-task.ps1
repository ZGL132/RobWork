param([Parameter(Mandatory=$true)][string]$TaskFile)
if (-not (Test-Path $TaskFile)) { throw "task file not found: $TaskFile" }
$tasks = @(Get-Content $TaskFile -Raw | ConvertFrom-Json)
$required = 'taskId','unit','masterWp','title','status','dependsOn','requirements','designRefs','verify'
foreach ($t in $tasks) {
  foreach ($k in $required) { if ($null -eq $t.$k) { throw "missing task field: $k" } }
  if ($t.taskId -notmatch '^[A-Z]+-(T[0-9]+|CR-[0-9]+)$') { throw "invalid taskId: $($t.taskId)" }
  if ($t.masterWp -notmatch '^WP-[A-I]$') { throw "invalid masterWp: $($t.masterWp)" }
  if (@($t.verify).Count -eq 0) { throw "verify must contain at least one command: $($t.taskId)" }
}
Write-Output "validate-task: PASS ($($tasks.Count) tasks)"
