# WP-15-T20阶段合入：按Windows串行规则复跑六套测试与架构门禁。
# 脚本可从build或已归档证据目录重跑；避免依赖实施会话的工作目录。
param([string]$Label=('merge-recheck-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff')))
$ErrorActionPreference='Stop'
$kinMergeRepo=(& git -C $PSScriptRoot rev-parse --show-toplevel).Trim()
Set-Location $kinMergeRepo
$kinMergeRunner=Join-Path $kinMergeRepo 'RobWork/doc/industrial-robot-design/traceability/builds/kinematics-audit-20261021-5fd9a3d1/stage2/run-stage2.ps1'
foreach ($kinMergeName in @('kinematics','kinematics_contract','kinematics_gui','standalone_kinematics','standalone_kinematics_contract','standalone_kinematics_gui')) {
    & pwsh -NoProfile -File $kinMergeRunner -Mode test -Name $kinMergeName -Label $Label
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
& pwsh -NoProfile -File $kinMergeRunner -Mode gates -Label $Label
exit $LASTEXITCODE
