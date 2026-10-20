$ErrorActionPreference='Stop'
$batchRunner='C:/Users/zgl18/.codex/worktrees/486b/RobWork/build/modeling-completion-20261020-b3h/run.ps1'
foreach($suite in @('standalone_modeling','standalone_modeling_contract','standalone_modeling_gui')){
 & pwsh -NoProfile -File $batchRunner -Mode test -Name $suite
 if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
}
