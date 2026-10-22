$ErrorActionPreference='Stop'
[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false)
$root=Join-Path $PSScriptRoot 'repo'
Set-Location $root
$base='8a7174945a8f280f674a40ec46a512b5b5f1b414'
$tip='9fd7256cf61a7768622810ed547617a66013949b'
function Hash([string]$s){$s=$s.TrimStart([char]0xfeff).Replace("`r`n","`n").TrimEnd("`n")+"`n";$sha=[Security.Cryptography.SHA256]::Create();try{([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($s)))).Replace('-','').ToLowerInvariant()}finally{$sha.Dispose()}}
$rows=@();$checks=@();$excerpt=[Collections.Generic.List[string]]::new()
foreach($p in @(git diff --name-only $base $tip -- RobWork/doc/industrial-robot-design/reuse/functions)){
 $old=(@(git show ($base+':'+$p))-join "`n")|ConvertFrom-Json
 $new=Get-Content $p -Raw|ConvertFrom-Json
 foreach($f in $new.functions){
  if($f.functionId -in $old.functions.functionId){continue}
  $rows+= $f
  foreach($s in $f.sources){
   $lines=[IO.File]::ReadAllLines((Join-Path $root $s.path))
   $section=$lines[($s.startLine-1)..($s.endLine-1)]-join "`n"
   $sha=Hash $section
   $checks+=[pscustomobject]@{id=$f.functionId;qualifiedSymbol=$s.qualifiedSymbol;path=$s.path;startLine=$s.startLine;endLine=$s.endLine;hashMatches=($sha-eq$s.sha256);actualSha=$sha;symbolOccurs=$section.Contains(($s.symbol -split '::')[-1]);first=$section.Split("`n")[0];last=$section.Split("`n")[-1]}
   $excerpt.Add("FUNCTION $($f.functionId) $($s.path) $($s.startLine)-$($s.endLine)")
   for($i=$s.startLine-1;$i-lt$s.endLine;$i++){$excerpt.Add(('{0}: {1}'-f($i+1),$lines[$i]))}
   foreach($a in $f.auditedSections){$checks+=[pscustomobject]@{id=$f.functionId;kind='auditedSections';startLine=$a.startLine;endLine=$a.endLine;hashMatches=((Hash ($lines[($a.startLine-1)..($a.endLine-1)]-join "`n"))-eq$a.sha256)}}
  }
  foreach($i in $f.inputs){$checks+=[pscustomobject]@{id=$f.functionId;kind='input';path=$i.path;hashMatches=((Hash ([IO.File]::ReadAllText((Join-Path $root $i.path))))-eq$i.sha256)}}
 }
}
$rows|ConvertTo-Json -Depth 15|Set-Content (Join-Path $PSScriptRoot 'new-rows.json') -Encoding utf8
$checks|ConvertTo-Json -Depth 10|Set-Content (Join-Path $PSScriptRoot 'bindings.json') -Encoding utf8
$excerpt|Set-Content (Join-Path $PSScriptRoot 'source-excerpts.txt') -Encoding utf8
$catalog=Get-Content RobWork/doc/industrial-robot-design/reuse/catalog.json -Raw|ConvertFrom-Json
$cat=$catalog.capabilities|Where-Object id -in @('UTIL-BINARCHIVE','HOST-LINES')
$cb=@();foreach($u in $cat.upstream){$cb+=[pscustomobject]@{path=$u.path;shaMatches=((Hash ([IO.File]::ReadAllText((Join-Path $root $u.path))))-eq$u.sha256);target=$u.target;targetFile=$u.targetFile;targetShaMatches=((Hash ([IO.File]::ReadAllText((Join-Path $root $u.targetFile))))-eq$u.targetSha256)}}
foreach($t in $cat.tests){$cb+=[pscustomobject]@{path=$t.path;shaMatches=((Hash ([IO.File]::ReadAllText((Join-Path $root $t.path))))-eq$t.sha256)}}
$cb|ConvertTo-Json -Depth 10|Set-Content (Join-Path $PSScriptRoot 'catalog-bindings.json') -Encoding utf8
$failed=@($checks|Where-Object hashMatches -eq $false)
[pscustomobject]@{reviewedCommit=$tip;baseCommit=$base;rows=$rows.Count;units=@($rows.ownerUnit|Sort-Object -Unique);allPending=(@($rows|Where-Object {$_.decision-ne'pending'-or$_.status-ne'pending'}).Count-eq0);failedBindingCount=$failed.Count;catalog=$cb}|ConvertTo-Json -Depth 10|Set-Content (Join-Path $PSScriptRoot 'facts.json') -Encoding utf8
Write-Output ('newRows='+$rows.Count+' failedBindings='+$failed.Count)
$checks|Where-Object kind -eq $null|Format-Table id,startLine,endLine,hashMatches,first,last -Wrap

