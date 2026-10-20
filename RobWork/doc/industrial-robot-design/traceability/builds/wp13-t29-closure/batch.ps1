# 真窗口严格串行；任一失败立即停止，原始XML/原生JSON/日志全部保留。
$ErrorActionPreference='Stop'
$runner=Join-Path $PSScriptRoot 'run.ps1'
$names=@('evidence','evidence_contract','policy','policy_contract','runtime',
 'optimization_contract','core','requirements','requirements_contract',
 'modeling','modeling_contract','ui','ui_contract','kinematics','kinematics_contract',
 'requirements_gui','modeling_gui','ui_gui','kinematics_gui')
foreach($name in $names){
 & pwsh -NoProfile -File $runner -Mode test -Name $name
 if($LASTEXITCODE -ne 0){throw "失败已留痕，停止批次：$name"}
}
& D:/software/Miniconda3/python.exe (Join-Path $PSScriptRoot 'validate_reports.py') @names
exit $LASTEXITCODE
