$ErrorActionPreference='Stop'
[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false)
Set-Location (Join-Path $PSScriptRoot 'repo')
$j=Get-Content RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01/batch4-search.json -Raw|ConvertFrom-Json
$results=@();$n=0
foreach($r in $j.results){
 $n++;$name='search-'+$n.ToString('00')
 if($r.command -notmatch '^rg -n "[^"\r\n]+" (RobWork/[A-Za-z0-9_./ -]+)$'){throw 'unexpected search command'}
 Invoke-Expression ($r.command+' 1> "'+(Join-Path $PSScriptRoot ($name+'.stdout.txt'))+'" 2> "'+(Join-Path $PSScriptRoot ($name+'.stderr.txt'))+'"')
 $rc=$LASTEXITCODE
 $rc|Set-Content (Join-Path $PSScriptRoot ($name+'.exit.txt'))
 $actual=[IO.File]::ReadAllText((Join-Path $PSScriptRoot ($name+'.stdout.txt'))).Replace('\','/').Replace("`r`n","`n").TrimEnd("`n")-split "`n"|Sort-Object
 $expected=$r.stdout.Replace('\','/').Replace("`r`n","`n").TrimEnd("`n")-split "`n"|Sort-Object
 $delta=@(Compare-Object $expected $actual)
 $results+=[pscustomobject]@{command=$r.command;exitCode=$rc;recordedExitCode=$r.exitCode;stdoutSetMatches=($delta.Count-eq0);diffCount=$delta.Count;stderrEmpty=((Get-Item (Join-Path $PSScriptRoot ($name+'.stderr.txt'))).Length-eq0);diff=$delta}
}
$results|ConvertTo-Json -Depth 10|Set-Content (Join-Path $PSScriptRoot 'search-replay.json') -Encoding utf8
$results|Select-Object exitCode,stdoutSetMatches,diffCount,stderrEmpty|Format-Table
