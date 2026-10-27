# REUSE-T41 独立验收——BASE 基线对照（在 BASE cf00a7dc9 临时 worktree 内跑同款全量套件）
Set-Location 'C:\Users\zgl18\AppData\Local\Temp\rw-t41-baseline'
$tasks = @(Get-ChildItem 'RobWork/doc/industrial-robot-design/tasks/*.json' | ForEach-Object { $_.FullName })
Write-Output ("contract-file-count=" + $tasks.Count)
& 'C:\Users\zgl18\AppData\Local\Temp\rw-t41-baseline\RobWork\scripts\industrialrobot\validate-docs.ps1' -TaskFile $tasks
Write-Output ("baseline-suite-exit=" + $LASTEXITCODE)
exit $LASTEXITCODE
