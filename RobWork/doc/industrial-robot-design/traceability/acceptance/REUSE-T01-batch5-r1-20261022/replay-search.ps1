$ErrorActionPreference='Stop'
$root=Join-Path $PSScriptRoot 'repo'
Set-Location $root
$record=Get-Content 'RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01/batch5-search.json' -Raw|ConvertFrom-Json
$results=@();$index=0
function Canon($s){@(($s.Replace('\','/').Replace("`r`n","`n").TrimEnd("`n") -split "`n")|Sort-Object)-join "`n"}
foreach($r in $record.results){
 $index++
 if(-not $r.command.StartsWith('rg -n ')){throw 'Unexpected command'}
 $stdout=Join-Path $PSScriptRoot "search-$index.stdout.txt"
 $stderr=Join-Path $PSScriptRoot "search-$index.stderr.txt"
 Invoke-Expression ($r.command+' 1> $stdout 2> $stderr')
 $code=$LASTEXITCODE
 $actual=[IO.File]::ReadAllText($stdout)
 $err=[IO.File]::ReadAllText($stderr)
 $results+=[pscustomobject]@{index=$index;command=$r.command;exitCode=$code;expectedExit=$r.exitCode;stdoutSetMatches=((Canon $actual)-eq(Canon $r.stdout));stderrMatches=($err.Trim()-eq$r.stderr.Trim())}
}
$results|ConvertTo-Json -Depth 5|Set-Content (Join-Path $PSScriptRoot 'search-results.json') -Encoding utf8
$results|Format-Table index,exitCode,stdoutSetMatches,stderrMatches
