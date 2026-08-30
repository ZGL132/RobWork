# Fixture orphan-task: delete the WP-25-T05 task card while the work package still declares it.
# Expected gate diagnostic keyword: Task ID without card.
param([string]$TempRoot)

$target = Get-ChildItem -LiteralPath (Join-Path $TempRoot 'agent-tasks') -Filter 'WP-25-T05-*.md' -File | Select-Object -First 1
if ($null -eq $target) { throw 'fixture injection failed: WP-25-T05 card not found' }
Remove-Item -LiteralPath $target.FullName -Force
