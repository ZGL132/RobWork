# Fixture missing-work-package: delete the WP-07 detailed work-package plan file.
# Expected gate diagnostic keyword: unique detailed plan.
param([string]$TempRoot)

$target = Get-ChildItem -LiteralPath (Join-Path $TempRoot 'work-packages') -Filter 'WP-07-*.md' -File | Select-Object -First 1
if ($null -eq $target) { throw 'fixture injection failed: WP-07 plan not found' }
Remove-Item -LiteralPath $target.FullName -Force
