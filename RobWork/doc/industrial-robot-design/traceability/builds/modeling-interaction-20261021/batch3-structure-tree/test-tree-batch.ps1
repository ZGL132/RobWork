$ErrorActionPreference='Stop'
$taskRoot='C:/Users/zgl18/.codex/worktrees/486b/RobWork'
# Windows GUI严格串行；同一批XML和原生报告由既有run.ps1统一落盘。
foreach($suite in @('modeling','modeling_contract','modeling_gui','ui','ui_contract','ui_gui','standalone_modeling','standalone_modeling_contract','standalone_modeling_gui','standalone_ui','standalone_ui_contract','standalone_ui_gui')){
 & pwsh -NoProfile -File "$taskRoot/build/modeling-interaction-20261021/run.ps1" -Name $suite -Tag batch3-final *> "$taskRoot/build/modeling-interaction-20261021/batch3-final-$suite-wrapper.log"
 $taskCode=$LASTEXITCODE
 Write-Output "$suite exit=$taskCode"
 if($taskCode -ne 0){Get-Content "$taskRoot/build/modeling-interaction-20261021/batch3-final-$suite-wrapper.log" -Tail 55;exit $taskCode}
}
