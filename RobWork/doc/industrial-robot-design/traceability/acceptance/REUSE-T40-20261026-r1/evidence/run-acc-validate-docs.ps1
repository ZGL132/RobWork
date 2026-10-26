# REUSE-T40 独立验收④：当前 HEAD 门禁实测 validate-docs -TaskFile 全部契约。
# 按 ACC 教训用 PowerShell 数组传参（-File 形式下多值位置溢出会误绑 -RepoRoot），
# 故本包装脚本以数组展开全部 tasks/*.json 后调用 validate-docs.ps1。
# foundation-tasks.json 为 legacy 数组索引（78 内嵌契约，其中 71 个 ready 无
# allowedFiles——BASE 同态既有，与本包无关；validate-docs.ps1 自身 $taskFiles
# 扫描亦按名排除该文件），按门禁自身口径从 -TaskFile 套件排除。
Set-Location 'D:\10_Source_Repos\21_robot\RobWork'
$taskFiles = @(Get-ChildItem 'RobWork/doc/industrial-robot-design/tasks' -Filter '*.json' |
  Where-Object { $_.Name -ne 'foundation-tasks.json' } | ForEach-Object { $_.FullName })
Write-Host ("TASKFILE-COUNT=" + $taskFiles.Count)
& 'RobWork/scripts/industrialrobot/validate-docs.ps1' -TaskFile $taskFiles
Write-Host ("VALIDATE-DOCS-EXIT=" + $LASTEXITCODE)
exit $LASTEXITCODE
