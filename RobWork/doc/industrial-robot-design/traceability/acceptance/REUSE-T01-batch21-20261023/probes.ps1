$ErrorActionPreference='Continue'
$auditRoot=$PSScriptRoot
$repo=Join-Path $auditRoot 'repo'
$scriptDir=Join-Path $repo 'RobWork/scripts/industrialrobot'
$task=Join-Path $repo 'RobWork/doc/industrial-robot-design/tasks/REUSE-T01.json'
$commands=@()
function Run([string]$Name,[string]$Exe,[string[]]$Arguments) {
  $log=Join-Path $auditRoot ($Name+'.log')
  $start=[DateTime]::UtcNow.ToString('o')
  & $Exe @Arguments *> $log
  $code=$LASTEXITCODE
  $script:commands += [pscustomobject]@{name=$Name;executable=$Exe;arguments=$Arguments;cwd=$repo;startUtc=$start;endUtc=[DateTime]::UtcNow.ToString('o');exitCode=$code;log=($Name+'.log')}
  Set-Content -LiteralPath (Join-Path $auditRoot ($Name+'.exit.txt')) -Value $code
  $script:commands|ConvertTo-Json -Depth 8|Set-Content (Join-Path $auditRoot 'commands.json') -Encoding utf8
  Write-Output "$Name exit=$code"
}
$ps7=(Get-Command pwsh).Source
$ps51=(Get-Command powershell).Source
New-Item -ItemType Directory -Path (Join-Path $auditRoot 'before') -Force|Out-Null
Copy-Item -LiteralPath (Join-Path $repo 'RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01/batch21/lite-before.ps1') -Destination (Join-Path $auditRoot 'before/test-governance-lite.ps1')
Copy-Item -LiteralPath (Join-Path $scriptDir 'commit-lint.ps1') -Destination (Join-Path $auditRoot 'before/commit-lint.ps1')
Push-Location $repo
try {
  Run 'lite-before-ps7' $ps7 @('-NoProfile','-File',(Join-Path $auditRoot 'before/test-governance-lite.ps1'),'-RepoRoot',$repo,'-OutputDir',(Join-Path $auditRoot 'lite-before-ps7'))
  Run 'lite-before-ps51' $ps51 @('-NoProfile','-File',(Join-Path $auditRoot 'before/test-governance-lite.ps1'),'-RepoRoot',$repo,'-OutputDir',(Join-Path $auditRoot 'lite-before-ps51'))
  $bad=Get-Content -LiteralPath $task -Raw -Encoding UTF8|ConvertFrom-Json
  $bad.branch='main'
  $badPath=Join-Path $auditRoot 'invalid-branch.json'
  $bad|ConvertTo-Json -Depth 100|Set-Content -LiteralPath $badPath -Encoding utf8
  Run 'validate-invalid-branch' $ps7 @('-NoProfile','-File',(Join-Path $scriptDir 'validate-task.ps1'),'-RepoRoot',$repo,'-TaskFile',$badPath)
  Run 'verify-invalid-branch-dryrun' $ps7 @('-NoProfile','-File',(Join-Path $scriptDir 'verify-task.ps1'),'-RepoRoot',$repo,'-TaskFile',$badPath,'-DryRun')
  Run 'verify-valid-dryrun' $ps7 @('-NoProfile','-File',(Join-Path $scriptDir 'verify-task.ps1'),'-RepoRoot',$repo,'-TaskFile',$task,'-DryRun')
  Run 'verify-task-real' $ps7 @('-NoProfile','-File',(Join-Path $scriptDir 'verify-task.ps1'),'-RepoRoot',$repo,'-TaskFile',$task)
  Run 'commit-lint-frozen-range' $ps7 @('-NoProfile','-File',(Join-Path $scriptDir 'commit-lint.ps1'),'-RepoRoot',$repo,'-Range','d52fd06aafcb025a7d494d59c6d44bc9c16f3643..cff60ddd4a05fdae399932d8b6a3ccb5b1cdada7')
} finally {Pop-Location}
