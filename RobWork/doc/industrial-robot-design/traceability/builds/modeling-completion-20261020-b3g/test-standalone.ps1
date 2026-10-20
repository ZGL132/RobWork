$ErrorActionPreference='Stop'
# 调用前独立树构建、集成GUI调度均须真实结束，一次只启动一个GUI exe。
$batchRunner='C:/Users/zgl18/.codex/worktrees/486b/RobWork/build/modeling-completion-20261020-b3g/run.ps1'
foreach($batchSuite in @('standalone_runtime','standalone_modeling','standalone_modeling_contract',
    'standalone_modeling_gui','standalone_kinematics','standalone_kinematics_contract','standalone_kinematics_gui')){
    & pwsh -NoProfile -File $batchRunner -Mode test -Name $batchSuite
    if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
}
