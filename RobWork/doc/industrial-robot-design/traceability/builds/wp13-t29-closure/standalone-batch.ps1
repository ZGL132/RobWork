# 独立模式仅运行本模式实际注册的目标；不将集成框架测试数量冒充独立覆盖。
# GUI目标在同一VS x64环境下串行执行，runner同时检查其他工作树的GUI进程。
$ErrorActionPreference='Stop'
$runner=Join-Path $PSScriptRoot 'run.ps1'
$names=@('evidence','evidence_contract','policy','policy_contract','runtime',
 'optimization_contract','core','requirements','requirements_contract',
 'modeling','modeling_contract','ui','requirements_gui','modeling_gui','ui_gui') |
 ForEach-Object { 'standalone_'+$_ }
foreach($name in $names){
 & pwsh -NoProfile -File $runner -Mode test -Name $name
 if($LASTEXITCODE -ne 0){throw "独立模式失败已留痕，停止批次：$name"}
}
& D:/software/Miniconda3/python.exe (Join-Path $PSScriptRoot 'validate_reports.py') @names
exit $LASTEXITCODE
