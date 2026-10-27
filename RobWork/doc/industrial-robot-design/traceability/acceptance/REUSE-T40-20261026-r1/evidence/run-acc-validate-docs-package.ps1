# REUSE-T40 独立验收④（决定性口径）：当前 HEAD 门禁实测 validate-docs -TaskFile
# ＝本包契约及其 dependsOn 前置（GOV-T21 复用治理独验 / GOV-T22 复用门禁独验）。
# PowerShell 数组传参（ACC 实测教训：-File 形式下多值位置溢出会误绑 -RepoRoot）。
Set-Location 'D:\10_Source_Repos\21_robot\RobWork'
$taskFiles = @(
  'RobWork/doc/industrial-robot-design/tasks/REUSE-T40.json',
  'RobWork/doc/industrial-robot-design/tasks/GOV-T21.json',
  'RobWork/doc/industrial-robot-design/tasks/GOV-T22.json'
)
Write-Host ("PACKAGE TASKFILE-COUNT=" + $taskFiles.Count)
& 'RobWork/scripts/industrialrobot/validate-docs.ps1' -TaskFile $taskFiles
Write-Host ("PACKAGE VALIDATE-DOCS-EXIT=" + $LASTEXITCODE)
exit $LASTEXITCODE
