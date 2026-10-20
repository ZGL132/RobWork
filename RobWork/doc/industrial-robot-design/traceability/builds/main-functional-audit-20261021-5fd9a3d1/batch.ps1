param([ValidateSet('integration','tours','standalone')][string]$Group='integration')
$ErrorActionPreference='Stop'
$auditHere=$PSScriptRoot
$auditRunner=Join-Path $auditHere 'run.ps1'
# 每次只启动一个绝对路径测试程序；失败原样登记，后续只执行相互独立的检查。
$auditNames=if($Group -eq 'integration') {
 @('requirements','requirements_contract','requirements_gui','modeling','modeling_contract',
   'modeling_gui','runtime','kinematics','kinematics_contract','kinematics_gui','ui','ui_contract',
   'ui_gui','project','project_contract','policy','policy_contract','evidence','evidence_contract',
   'io','io_contract','trajectory','trajectory_contract','dynamics','dynamics_contract',
   'drivetrain','drivetrain_contract','selection','selection_contract','optimization',
   'optimization_contract','reporting','reporting_contract','workflow','workflow_contract')
} elseif($Group -eq 'tours') {
 @('requirements-tour','modeling-tour','auto','layout')
} else {
 @('standalone_requirements','standalone_requirements_contract','standalone_requirements_gui',
   'standalone_modeling','standalone_modeling_contract','standalone_modeling_gui')
}
$auditCodes=@()
foreach($auditName in $auditNames) {
 $auditMode=if($Group -eq 'tours'){'tour'}else{'test'}
 Write-Output "START $auditName $([DateTime]::UtcNow.ToString('o'))"
 & pwsh -NoProfile -File $auditRunner -Mode $auditMode -Name $auditName *> (Join-Path $auditHere "$auditName-launch.log")
 $auditExit=$LASTEXITCODE
 $auditCodes+=@{name=$auditName;exit=$auditExit;finishedUtc=[DateTime]::UtcNow.ToString('o')}
 $auditCodes | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $auditHere "$Group-exits.json") -Encoding utf8
 Write-Output "FINISH $auditName exit=$auditExit"
 if($auditExit -in @(71,72,73)) {throw 'GUI前置/并发/超时阻断，停止本组，保留证据。'}
}
if(@($auditCodes | Where-Object {$_.exit -ne 0}).Count -gt 0){exit 1}
