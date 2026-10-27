# 对实际validate-task执行分支命名正/负例。隔离fixture不声明任何生产变更授权；
# 移除reuse字段只为把分支结构单测与已知全量跨单元失败分开，真实T13仍执行H1-H6。
$ErrorActionPreference='Stop'
$root=(Get-Location).Path
$ev=Join-Path $root 'RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T13'
$fixture=Join-Path $root '.cache/foundation/branch-fixture.json'
$task=Get-Content (Join-Path $root 'RobWork/doc/industrial-robot-design/tasks/REUSE-T13.json') -Raw | ConvertFrom-Json
$task.taskId='FOUNDATION-CR-99'
$task.PSObject.Properties.Remove('reuse')
$results=@()
foreach ($case in @(
    @('codex/reuse-foundation',$true),@('wp03-t01',$true),@('doc-t03',$true),
    @('main',$false),@('redesign-main',$false),@('codex/../main',$false),
    @('codex/foo bar',$false),@('codex/',$false),@('codex/foo.lock',$false))) {
    $task.branch=$case[0]
    $task | ConvertTo-Json -Depth 25 | Set-Content -LiteralPath $fixture -Encoding utf8
    $text=& pwsh -NoProfile -File (Join-Path $root 'RobWork/scripts/industrialrobot/validate-task.ps1') -TaskFile $fixture 2>&1
    $code=$LASTEXITCODE
    $ok=($code -eq 0) -eq $case[1]
    $results += [ordered]@{branch=$case[0];expectedAccepted=$case[1];exitCode=$code;passed=$ok;output=($text -join "`n")}
}
$results | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $ev 'branch-contract-results.json') -Encoding utf8
if (@($results | Where-Object {-not $_.passed}).Count) { throw '分支命名正/负例失败' }
Write-Output "branch-contract: $($results.Count) passed / 0 skipped / 0 failed (仅分支结构，不替代真实T13复用准入)"
