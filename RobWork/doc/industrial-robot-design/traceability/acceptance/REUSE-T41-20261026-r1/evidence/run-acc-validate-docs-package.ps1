# REUSE-T41 独立验收——决定性套件包装脚本（教训修正版：
# pwsh -File 子进程形式下数组会被展开为位置参数、第二个路径溢出误绑
# -RepoRoot（2026-10-26 首跑实测 exit 1，见 acc-validate-docs-package-suite.log
# 首段）——改为同进程 & 调用脚本文件，数组保真绑定 string[] -TaskFile）
Set-Location 'D:\10_Source_Repos\21_robot\RobWork'
$tasks = @(
  'RobWork/doc/industrial-robot-design/tasks/REUSE-T41.json',
  'RobWork/doc/industrial-robot-design/tasks/GOV-T21.json',
  'RobWork/doc/industrial-robot-design/tasks/GOV-T22.json'
)
& 'D:\10_Source_Repos\21_robot\RobWork\RobWork\scripts\industrialrobot\validate-docs.ps1' -TaskFile $tasks
Write-Output ("package-suite-exit=" + $LASTEXITCODE)
exit $LASTEXITCODE
