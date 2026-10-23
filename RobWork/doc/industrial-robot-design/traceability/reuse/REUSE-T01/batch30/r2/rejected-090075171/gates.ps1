param([string]$AuditRoot)
Set-Location (Join-Path $AuditRoot 'clone')
$out=Join-Path $AuditRoot 'evidence'
$task='RobWork/doc/industrial-robot-design/tasks/REUSE-T01.json'
$head='0900751718849863af1190ece61c8ddb1f1cf547'
$jobs=@(
 @{name='task';script='validate-task.ps1';args=@('-TaskFile',$task)},
 @{name='docs';script='validate-docs.ps1';args=@()},
 @{name='reuse';script='validate-reuse.ps1';args=@('-TaskFile',$task,'-Head',$head)},
 @{name='scope';script='verify-task-scope.ps1';args=@('-TaskFile',$task,'-Base','bb283be1dbb6ae62c59d6563e3c7dac0a4ae5103','-Head',$head)},
 @{name='commit-lint';script='commit-lint.ps1';args=@('-Range','bb283be1dbb6ae62c59d6563e3c7dac0a4ae5103..0900751718849863af1190ece61c8ddb1f1cf547')}
)
foreach($job in $jobs){
 $scriptPath=Join-Path 'RobWork/scripts/industrialrobot' $job.script
 $arguments=$job.args
 & pwsh -NoProfile -File $scriptPath @arguments *> (Join-Path $out ($job.name+'.log'))
 $LASTEXITCODE | Set-Content (Join-Path $out ($job.name+'.exit.txt'))
}
git diff --check bb283be1dbb6ae62c59d6563e3c7dac0a4ae5103..0900751718849863af1190ece61c8ddb1f1cf547 *> (Join-Path $out 'diff-check.log')
$LASTEXITCODE | Set-Content (Join-Path $out 'diff-check.exit.txt')
