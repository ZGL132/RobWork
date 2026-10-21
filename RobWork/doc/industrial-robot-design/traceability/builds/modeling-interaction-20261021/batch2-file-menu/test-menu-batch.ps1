$ErrorActionPreference='Stop'
$taskRoot='C:/Users/zgl18/.codex/worktrees/486b/RobWork'
foreach($suite in @('ui','ui_contract','ui_gui','standalone_ui','standalone_ui_contract','standalone_ui_gui')){
 & pwsh -NoProfile -File "$taskRoot/build/modeling-interaction-20261021/run.ps1" -Name $suite -Tag batch2-file-menu *> "$taskRoot/build/modeling-interaction-20261021/batch2-file-menu-$suite-wrapper.log"
 $taskCode=$LASTEXITCODE
 Write-Output "$suite exit=$taskCode"
 if($taskCode -ne 0){Get-Content "$taskRoot/build/modeling-interaction-20261021/batch2-file-menu-$suite-wrapper.log" -Tail 40;exit $taskCode}
}
