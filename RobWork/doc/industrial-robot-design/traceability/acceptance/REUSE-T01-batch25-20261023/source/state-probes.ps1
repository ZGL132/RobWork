param([string]$OutDir)
$ErrorActionPreference='Stop'
$b25Root=(Get-Location).Path
$b25Script=Join-Path $b25Root 'RobWork/scripts/industrialrobot/validate-state.ps1'
[IO.Directory]::CreateDirectory($OutDir)|Out-Null
$fixture=Join-Path $OutDir 'fixture'
$tasks=Join-Path $fixture 'RobWork/doc/industrial-robot-design/tasks'
New-Item -ItemType Directory -Force $tasks|Out-Null
'{"taskId":"TK-T01","status":"ready","branch":"wp02-t01"}'|Set-Content (Join-Path $tasks 'TK-T01.json') -Encoding utf8
$template=Get-Content 'RobWork/doc/industrial-robot-design/traceability/pipeline/state.json' -Raw|ConvertFrom-Json
$template.history=@();$template.queue=@();$template.docRefs.PSObject.Properties.Remove('b1BatchManifest')
$template.phase='implementing';$template.currentTask='TK-T01';$template.branch='wp02-t01';$template.base='a'*40
$template.run=[pscustomobject]@{kind='implement';runId=[guid]::NewGuid().ToString();workerId=[guid]::NewGuid().ToString();task='TK-T01';startedAt='2020-01-01T00:00:00+00:00';leaseExpiresAt='2020-01-01T01:00:00+00:00'}
$template.heartbeat.updatedAt='2020-01-01T00:30:00+00:00';$template.tickToken=[guid]::NewGuid().ToString();$template.attempts.fix=0;$template.lastFailureRecord=$null
$b25Results=@()
foreach($shell in @('pwsh','powershell')){
  foreach($case in @('healthy','fractional-count','string-count','fake-guid','offset-valid','offset-expired','bad-calendar')){
    $state=$template|ConvertTo-Json -Depth 20|ConvertFrom-Json
    $expected=1
    switch($case){
      'healthy'{$expected=0}
      'fractional-count'{$state.tickCount=1.5}
      'string-count'{$state.attempts.accept='1'}
      'fake-guid'{$state.tickToken='-'*36}
      'offset-valid'{$expected=0;$state.run.startedAt='2020-01-01T10:00:00+02:00';$state.run.leaseExpiresAt='2020-01-01T09:30:00+00:00';$state.heartbeat.updatedAt='2020-01-01T09:00:00+00:00'}
      'offset-expired'{$state.run.startedAt='2020-01-01T08:00:00+00:00';$state.run.leaseExpiresAt='2020-01-01T09:00:00+02:00';$state.heartbeat.updatedAt='2020-01-01T09:00:00+00:00'}
      'bad-calendar'{$state.run.startedAt='2020-99-99T10:00:00+00:00'}
    }
    $file=Join-Path $OutDir ($shell+'-'+$case+'.json')
    $state|ConvertTo-Json -Depth 20|Set-Content $file -Encoding utf8
    & $shell -NoProfile -File $b25Script -RepoRoot $fixture -StateFile $file *> (Join-Path $OutDir ($shell+'-'+$case+'.log'))
    $code=$LASTEXITCODE;$ok=($code -eq $expected)
    $b25Results+=[pscustomobject]@{shell=$shell;name=$case;expectedExit=$expected;exitCode=$code;passed=$ok}
    Write-Output "$shell $case exit=$code expected=$expected passed=$ok"
  }
}
$b25Results|ConvertTo-Json -Depth 5|Set-Content (Join-Path $OutDir 'report.json') -Encoding utf8
if(@($b25Results|Where-Object{-not $_.passed}).Count){exit 1}
