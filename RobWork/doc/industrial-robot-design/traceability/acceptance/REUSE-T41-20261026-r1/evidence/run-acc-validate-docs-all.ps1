# REUSE-T41 独立验收——全量对照套件（tasks/*.json 全部契约数组传参，同进程 & 调用）
Set-Location 'D:\10_Source_Repos\21_robot\RobWork'
$tasks = @(Get-ChildItem 'RobWork/doc/industrial-robot-design/tasks/*.json' | ForEach-Object { $_.FullName })
Write-Output ("contract-file-count=" + $tasks.Count)
& 'D:\10_Source_Repos\21_robot\RobWork\RobWork\scripts\industrialrobot\validate-docs.ps1' -TaskFile $tasks
Write-Output ("all-tasks-suite-exit=" + $LASTEXITCODE)
exit $LASTEXITCODE
