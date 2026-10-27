$ErrorActionPreference='Continue'
$acceptRoot=Split-Path -Parent $PSCommandPath
Set-Location (Join-Path $acceptRoot 'clone')
$task='RobWork/doc/industrial-robot-design/tasks/REUSE-T01.json'
foreach($gate in @('validate-task','validate-reuse','validate-docs','verify-task-scope','commit-lint')){
 $gateArgs=@('-NoProfile','-File',"RobWork/scripts/industrialrobot/$gate.ps1")
 if($gate -in @('validate-task','validate-reuse','verify-task-scope')){$gateArgs+=@('-TaskFile',$task)}
 if($gate -eq 'validate-reuse'){$gateArgs+=@('-Head','98c97238fa3c34aad51330b3b196439844badb69')}
 if($gate -eq 'verify-task-scope'){$gateArgs+=@('-Base','fd194d51b586085e26adeae4a73b72a06cb5ce21','-Head','98c97238fa3c34aad51330b3b196439844badb69')}
 if($gate -eq 'commit-lint'){$gateArgs+=@('-Range','fd194d51b586085e26adeae4a73b72a06cb5ce21..98c97238fa3c34aad51330b3b196439844badb69')}
 & pwsh @gateArgs *> (Join-Path $acceptRoot "$gate.log")
 Set-Content (Join-Path $acceptRoot "$gate.exit.txt") $LASTEXITCODE
}
