param([string]$Root)
$ErrorActionPreference='Stop'
$out=Join-Path $Root 'independent-state';New-Item -ItemType Directory -Force $out|Out-Null
$fixture=Join-Path $Root 'state-green/fixture';$validator=Join-Path $Root 'repo/RobWork/scripts/industrialrobot/validate-state.ps1'
$baseline=Get-Content (Join-Path $Root 'state-green/pwsh-healthy.json') -Raw
$results=@()
foreach($shell in @('pwsh','powershell')){
 $exe=(Get-Command $shell -CommandType Application | Select-Object -First 1).Source
 foreach($case in @('boolean-tick','negative-tick','int64-tick','negative-attempt','boolean-policy','fractional-lease','string-lease','zero-lease','int64-policy','equal-instant-offsets','uppercase-guid','guid-N-format')){
  $s=$baseline|ConvertFrom-Json;$expected=1
  switch($case){
   'boolean-tick'{$s.tickCount=$true}
   'negative-tick'{$s.tickCount=-1}
   'int64-tick'{$s.tickCount=[long]2147483648;$expected=0}
   'negative-attempt'{$s.attempts.implement=-1}
   'boolean-policy'{$s.policy.noProgressLimit=$true}
   'fractional-lease'{$s.policy.leaseMinutes.implement=1.5}
   'string-lease'{$s.policy.leaseMinutes.acceptance='5'}
   'zero-lease'{$s.policy.leaseMinutes.acceptance=0}
   'int64-policy'{$s.policy.maxFixCycles=[long]2147483648;$expected=0}
   'equal-instant-offsets'{$s.run.startedAt='2020-01-01T10:00:00+02:00';$s.run.leaseExpiresAt='2020-01-01T08:00:00Z';$s.heartbeat.updatedAt='2020-01-01T08:00:00Z';$expected=0}
   'uppercase-guid'{$s.tickToken='01234567-89AB-CDEF-0123-456789ABCDEF';$expected=0}
   'guid-N-format'{$s.tickToken='0123456789abcdef0123456789abcdef'}
  }
  $file=Join-Path $out ($shell+'-'+$case+'.json');$log=Join-Path $out ($shell+'-'+$case+'.log')
  $s|ConvertTo-Json -Depth 20|Set-Content $file -Encoding utf8
  & $exe -NoProfile -File $validator -RepoRoot $fixture -StateFile $file *> $log
  $code=$LASTEXITCODE;$ok=($code-eq$expected);$results+=[pscustomobject]@{shell=$shell;executable=$exe;case=$case;expectedExit=$expected;actualExit=$code;passed=$ok}
  Write-Output "$shell $case expected=$expected actual=$code passed=$ok"
 }
}
$results|ConvertTo-Json -Depth 6|Set-Content (Join-Path $out 'report.json') -Encoding utf8
if(@($results|Where-Object{-not $_.passed}).Count){exit 1}
