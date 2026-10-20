param()
$ErrorActionPreference='Stop'
$auditHere=$PSScriptRoot
# 所有命令顺序执行；审核中独立的失败项留痕后继续盘点，不实施修复。
& pwsh -NoProfile -File "$auditHere/batch.ps1" -Group integration
$integrationExit=$LASTEXITCODE
if($integrationExit -in @(71,72,73)){exit $integrationExit}
& pwsh -NoProfile -File "$auditHere/batch.ps1" -Group tours
$toursExit=$LASTEXITCODE
if($toursExit -in @(71,72,73)){exit $toursExit}
& pwsh -NoProfile -File "$auditHere/run.ps1" -Mode gates
$gatesExit=$LASTEXITCODE
@{integration=$integrationExit;tours=$toursExit;gates=$gatesExit}|ConvertTo-Json|Set-Content "$auditHere/batch-summary.json" -Encoding utf8
