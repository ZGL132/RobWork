$ErrorActionPreference='Stop'
# 先由调用者完成双模式构建，再只用此一个调度器串行运行所有GUI，避免交叉抢焦点。
$batchRunner='C:/Users/zgl18/.codex/worktrees/486b/RobWork/build/modeling-completion-20261020-b3g/run.ps1'
foreach($batchSuite in @('runtime','modeling','modeling_contract','modeling_gui',
    'kinematics','kinematics_contract','kinematics_gui','ui','ui_contract','ui_gui',
    'standalone_runtime','standalone_modeling','standalone_modeling_contract','standalone_modeling_gui',
    'standalone_kinematics','standalone_kinematics_contract','standalone_kinematics_gui')){
    & pwsh -NoProfile -File $batchRunner -Mode test -Name $batchSuite
    if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
}
& pwsh -NoProfile -File $batchRunner -Mode test -Name core -Filter 'BuildRedLine.*'
if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
& pwsh -NoProfile -File $batchRunner -Mode tour -Name modeling-tour
exit $LASTEXITCODE
