$ErrorActionPreference='Stop'
$taskRoot='C:/Users/zgl18/.codex/worktrees/486b/RobWork'
$names=@('runtime','io','modeling','modeling_contract','modeling_gui','ui','ui_contract','ui_gui','core','standalone_modeling','standalone_modeling_contract','standalone_modeling_gui','standalone_ui','standalone_ui_contract','standalone_ui_gui','standalone_io','standalone_runtime')
foreach($suite in $names){
 & pwsh -NoProfile -File "$taskRoot/build/modeling-interaction-20261021/run.ps1" -Name $suite -Tag batch1-final *> "$taskRoot/build/modeling-interaction-20261021/batch1-final-$suite-wrapper.log"
 $taskCode=$LASTEXITCODE
 Write-Output "$suite exit=$taskCode"
 if($taskCode -ne 0){Get-Content "$taskRoot/build/modeling-interaction-20261021/batch1-final-$suite-wrapper.log" -Tail 65;exit $taskCode}
}
