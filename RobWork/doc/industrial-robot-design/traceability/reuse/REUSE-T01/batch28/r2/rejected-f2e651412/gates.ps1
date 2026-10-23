$ErrorActionPreference='Continue'
$acceptRoot='C:/Users/zgl18/AppData/Local/Temp/ird-accept-b28-68277e2f39'
Set-Location (Join-Path $acceptRoot 'clone')
$task='RobWork/doc/industrial-robot-design/tasks/REUSE-T01.json'
foreach($gate in @('validate-task','validate-reuse','validate-docs','verify-task-scope','commit-lint')){
 $args=@('-NoProfile','-File',"RobWork/scripts/industrialrobot/$gate.ps1")
 if($gate -in @('validate-task','validate-reuse','verify-task-scope')){$args+=@('-TaskFile',$task)}
 if($gate -eq 'validate-reuse'){$args+=@('-Head','f2e65141259cd43ee5e63799514a733908440355')}
 if($gate -eq 'verify-task-scope'){$args+=@('-Base','f6d3de37ddbc8375a334b456b058e3d548141484','-Head','f2e65141259cd43ee5e63799514a733908440355')}
 if($gate -eq 'commit-lint'){$args+=@('-Range','f6d3de37ddbc8375a334b456b058e3d548141484..f2e65141259cd43ee5e63799514a733908440355')}
 & pwsh @args *> (Join-Path $acceptRoot "$gate.log")
 Set-Content (Join-Path $acceptRoot "$gate.exit.txt") $LASTEXITCODE
}
