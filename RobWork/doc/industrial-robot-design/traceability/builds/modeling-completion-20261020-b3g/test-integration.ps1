$ErrorActionPreference='Stop'
# 集成树构建已结束；独立树可继续构建，但其GUI必须等本调度器全部结束后才启动。
$batchRunner='C:/Users/zgl18/.codex/worktrees/486b/RobWork/build/modeling-completion-20261020-b3g/run.ps1'
foreach($batchSuite in @('runtime','modeling','modeling_contract','modeling_gui',
    'kinematics','kinematics_contract','kinematics_gui','ui','ui_contract','ui_gui')){
    & pwsh -NoProfile -File $batchRunner -Mode test -Name $batchSuite
    if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
}
& pwsh -NoProfile -File $batchRunner -Mode test -Name core -Filter 'BuildRedLine.*'
if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
& pwsh -NoProfile -File $batchRunner -Mode tour -Name modeling-tour
exit $LASTEXITCODE
