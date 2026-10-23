$ErrorActionPreference='Stop'
$out=$PSScriptRoot
$repo=Join-Path $out 'repo'
Set-Location $repo
$script:commands=@()
function Run($name,$shell,$arguments,$expected=0){
  $log=Join-Path $out ($name+'.log')
  & $shell @arguments *> $log
  $code=$LASTEXITCODE
  [IO.File]::WriteAllText((Join-Path $out ($name+'.exit.txt')),[string]$code)
  $script:commands += [pscustomobject]@{name=$name;shell=$shell;arguments=$arguments;exitCode=$code;expected=$expected;passed=($code -eq $expected)}
  $script:commands|ConvertTo-Json -Depth 8|Set-Content (Join-Path $out 'commands.json') -Encoding UTF8
  Write-Output "$name exit=$code expected=$expected"
}
$scripts=Join-Path $repo 'RobWork/scripts/industrialrobot'
$batch=Join-Path $repo 'RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01/batch23'
foreach($sh in @(@{name='ps7';exe='pwsh'},@{name='ps51';exe='powershell'})){
 Run ('selftest-'+$sh.name) $sh.exe @('-NoProfile','-File',"$scripts/run-flake-probe.ps1",'-SelfTest')
 Run ('red-'+$sh.name) $sh.exe @('-NoProfile','-File',"$batch/parser-probes.ps1",'-Script',"$batch/flake-before.ps1",'-OutDir',"$out/red-$($sh.name)") 1
 Run ('green-'+$sh.name) $sh.exe @('-NoProfile','-File',"$batch/parser-probes.ps1",'-Script',"$scripts/run-flake-probe.ps1",'-OutDir',"$out/green-$($sh.name)")
}
Run 'governance' 'pwsh' @('-NoProfile','-File',"$batch/governance-probes.ps1")
Run 'scope-frozen' 'pwsh' @('-NoProfile','-File',"$scripts/verify-task-scope.ps1",'-TaskFile','RobWork/doc/industrial-robot-design/tasks/REUSE-T01.json','-Base','daf0dcd175fb76c956154fe0213f2d791dad8575','-Head','062522fdeb9215e6a604a11d95cfca99842523db')
Run 'verify-task-real' 'pwsh' @('-NoProfile','-File',"$scripts/verify-task.ps1",'-TaskFile','RobWork/doc/industrial-robot-design/tasks/REUSE-T01.json')
Run 'commit-lint' 'pwsh' @('-NoProfile','-File',"$scripts/commit-lint.ps1",'-Range','daf0dcd175fb76c956154fe0213f2d791dad8575..062522fdeb9215e6a604a11d95cfca99842523db')
