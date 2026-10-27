$ErrorActionPreference='Stop'
$b=$PSScriptRoot;$r=Join-Path $b 'repo'
function HashText([string]$t){$t=($t.TrimStart([char]0xfeff) -replace "`r`n","`n").TrimEnd("`n")+"`n";[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($t))).ToLowerInvariant()}
$rows=@(Get-Content (Join-Path $b 'new-rows.json') -Raw|ConvertFrom-Json);$checks=@();$lineCache=@{}
foreach($row in $rows){
 foreach($s in $row.sources){
  if(!$lineCache.ContainsKey($s.path)){$lineCache[$s.path]=([IO.File]::ReadAllText((Join-Path $r $s.path)).TrimStart([char]0xfeff) -replace "`r`n","`n") -split "`n"}
  $lines=$lineCache[$s.path];$actual=HashText ($lines[($s.startLine-1)..($s.endLine-1)] -join "`n")
  $checks += [pscustomobject]@{function=$row.functionId;kind='source';path=$s.path;start=$s.startLine;end=$s.endLine;expected=$s.sha256;actual=$actual;pass=($actual -ceq $s.sha256)}
 }
 foreach($kind in @('inputs','auditEvidence')){foreach($s in $row.$kind){
  $actual=HashText ([IO.File]::ReadAllText((Join-Path $r $s.path)))
  $checks += [pscustomobject]@{function=$row.functionId;kind=$kind;path=$s.path;expected=$s.sha256;actual=$actual;pass=($actual -ceq $s.sha256)}
 }}
}
$c=Get-Content (Join-Path $r 'RobWork/doc/industrial-robot-design/reuse/catalog.json') -Raw|ConvertFrom-Json
foreach($cap in ($c.capabilities|Where-Object {$_.id -eq 'UTIL-BINARCHIVE'})){
 foreach($s in @($cap.upstream)+@($cap.tests)){
  $actual=HashText ([IO.File]::ReadAllText((Join-Path $r $s.path)))
  $checks += [pscustomobject]@{function=$cap.id;kind='catalog';path=$s.path;expected=$s.sha256;actual=$actual;pass=($actual -ceq $s.sha256)}
  if($s.targetFile){$actual=HashText ([IO.File]::ReadAllText((Join-Path $r $s.targetFile)));$checks += [pscustomobject]@{function=$cap.id;kind='target';path=$s.targetFile;expected=$s.targetSha256;actual=$actual;pass=($actual -ceq $s.targetSha256)}}
 }
}
$checks|ConvertTo-Json -Depth 10|Set-Content (Join-Path $b 'hash-checks.json') -Encoding utf8
$defs=@(Get-Content (Join-Path $r 'RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01/batch14-definitions.json') -Raw|ConvertFrom-Json)
$boundary=@();foreach($d in $defs){$row=@($rows|Where-Object {$_.functionId -ceq $d.functionId});foreach($s in $d.sources){$lines=$lineCache[$s.path];$matched=@($row.sources|Where-Object {$_.path -ceq $s.path -and $_.startLine -eq $s.startLine -and $_.endLine -eq $s.endLine});$boundary += [pscustomobject]@{function=$d.functionId;path=$s.path;start=$s.startLine;end=$s.endLine;rangeMatches=($matched.Count -eq 1);firstMatches=($lines[$s.startLine-1].Trim() -ceq $s.first.Trim());lastMatches=($lines[$s.endLine-1].Trim() -ceq $s.last.Trim())}}}
$boundary|ConvertTo-Json -Depth 10|Set-Content (Join-Path $b 'definition-checks.json') -Encoding utf8
$s=Get-Content (Join-Path $r 'RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01/batch14-search.json') -Raw|ConvertFrom-Json
$installed=@();foreach($x in $s.installedCandidates){$exist=Test-Path -LiteralPath $x.path;if($x.rawSha256){$actual=(Get-FileHash -LiteralPath $x.path -Algorithm SHA256).Hash.ToLowerInvariant();$pass=($actual -ceq $x.rawSha256)}else{$actual=$exist;$pass=($exist -eq $x.exists)};$installed += [pscustomobject]@{path=$x.path;actual=$actual;pass=$pass}}
$installed|ConvertTo-Json -Depth 10|Set-Content (Join-Path $b 'installed-checks.json') -Encoding utf8
Push-Location $r
try{
 $replay=@();$i=0
 foreach($record in $s.results){$i++;$a=@($record.arguments);$exe=$a[0];$e=Join-Path $b "search-$i.stderr.log";$out=@(& $exe @($a[1..($a.Count-1)]) 2> $e);$exit=$LASTEXITCODE;$out|Set-Content (Join-Path $b "search-$i.log") -Encoding utf8;$saved=@(($record.stdout -replace "`r`n","`n").TrimEnd("`n") -split "`n"|Sort-Object);$got=@($out|Sort-Object);$delta=@(Compare-Object $saved $got);$replay += [pscustomobject]@{index=$i;arguments=$a;expectedExit=$record.exitCode;actualExit=$exit;savedLines=$saved.Count;actualLines=$got.Count;matches=($delta.Count -eq 0 -and $exit -eq $record.exitCode);differences=$delta}}
 $replay|ConvertTo-Json -Depth 20|Set-Content (Join-Path $b 'search-replay.json') -Encoding utf8
}finally{Pop-Location}
[ordered]@{relations=$rows.Count;sourceRanges=@($rows.sources).Count;hashChecks=$checks.Count;hashFailures=@($checks|Where-Object {!$_.pass});definitions=$defs.Count;definitionRanges=$boundary.Count;definitionFailures=@($boundary|Where-Object {!$_.rangeMatches -or !$_.firstMatches -or !$_.lastMatches});installedChecks=$installed.Count;installedFailures=@($installed|Where-Object {!$_.pass})}|ConvertTo-Json -Depth 10
$replay|Select-Object index,expectedExit,actualExit,savedLines,actualLines,matches|Format-Table
