param([string]$AuditRoot)
$ErrorActionPreference='Continue'
git -C (Join-Path $AuditRoot 'clone') config core.longpaths true
Set-Location (Join-Path $AuditRoot 'clone')
$out=Join-Path $AuditRoot 'evidence'
$task='RobWork/doc/industrial-robot-design/tasks/REUSE-T01.json'
$head='52285db6f0d276742531ecb7e7bcf73db283e563'
$jobs=@(
 @{name='task';script='validate-task.ps1';args=@('-TaskFile',$task)},
 @{name='docs';script='validate-docs.ps1';args=@()},
 @{name='reuse';script='validate-reuse.ps1';args=@('-TaskFile',$task,'-Head',$head)},
 @{name='scope';script='verify-task-scope.ps1';args=@('-TaskFile',$task,'-Base','933dc6016','-Head',$head)},
 @{name='commit-lint';script='commit-lint.ps1';args=@('-Range',('933dc6016..'+$head))}
)
foreach($job in $jobs){
 $scriptPath=Join-Path 'RobWork/scripts/industrialrobot' $job.script
 $arguments=$job.args
 ('pwsh -NoProfile -File '+$scriptPath+' '+($arguments -join ' ')) | Set-Content (Join-Path $out ($job.name+'.command.txt'))
 & pwsh -NoProfile -File $scriptPath @arguments *> (Join-Path $out ($job.name+'.log'))
 $code=$LASTEXITCODE
 $code | Set-Content (Join-Path $out ($job.name+'.exit.txt'))
 Write-Output ($job.name+': '+$code)
}
git -c core.whitespace=cr-at-eol diff --check ('933dc6016..'+$head) *> (Join-Path $out 'diff-check.log')
$LASTEXITCODE | Set-Content (Join-Path $out 'diff-check.exit.txt')
git status --short *> (Join-Path $out 'clone-final-status.log')
