$ErrorActionPreference='Stop'
$root=Join-Path $PSScriptRoot 'clone'
Set-Location $root
$head='e5c58aa0b6b3958b90e3a22ef42c36e4c9a4dc7e'
$base='88cae2102d16a338c5f0a7725d080f1c0d802f95'
$batch='RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01/batch33'
function Blob([string]$rev,[string]$path) {
 $psi=[Diagnostics.ProcessStartInfo]::new('git');$psi.WorkingDirectory=$root;$psi.UseShellExecute=$false;$psi.RedirectStandardOutput=$true;$psi.RedirectStandardError=$true
 $psi.ArgumentList.Add('show');$psi.ArgumentList.Add($rev+':'+$path)
 $p=[Diagnostics.Process]::Start($psi);$m=[IO.MemoryStream]::new();$p.StandardOutput.BaseStream.CopyTo($m);$p.WaitForExit()
 if($p.ExitCode -ne 0){throw $p.StandardError.ReadToEnd()};return ,$m.ToArray()
}
function RawHash([byte[]]$bytes){[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes)).ToLowerInvariant()}
function NormHash([string]$text){RawHash ([Text.Encoding]::UTF8.GetBytes(($text.TrimStart([char]0xFEFF).Replace("`r`n","`n").TrimEnd("`n"))+"`n"))}
function TextAt([string]$rev,[string]$path){[Text.Encoding]::UTF8.GetString((Blob $rev $path))}
$d=(TextAt $head 'RobWork/doc/industrial-robot-design/reuse/functions/diagnostics.json')|ConvertFrom-Json
$old=(TextAt $base 'RobWork/doc/industrial-robot-design/reuse/functions/diagnostics.json')|ConvertFrom-Json
$new=@($d.functions|Where-Object functionId -match 'audit-b33')
$refreshed=@($d.functions|Where-Object { $id=$_.functionId;$p=$old.functions|Where-Object functionId -eq $id; $p -and (($_|ConvertTo-Json -Depth 30 -Compress) -cne ($p|ConvertTo-Json -Depth 30 -Compress)) })
$rows=@($new)+@($refreshed)
$checks=@()
foreach($row in $rows){
 foreach($s in $row.sources){$text=TextAt $head $s.path;$lines=$text.Replace("`r`n","`n").Split("`n");$part=$lines[($s.startLine-1)..($s.endLine-1)] -join "`n";$checks += [pscustomobject]@{kind='section';row=$row.functionId;path=$s.path;start=$s.startLine;end=$s.endLine;qualifiedSymbol=$s.qualifiedSymbol;symbolFound=$part.Contains($s.symbol);match=((NormHash $part)-eq $s.sha256)}}
 foreach($s in @($row.inputs)+@($row.auditEvidence)){$checks += [pscustomobject]@{kind='whole';row=$row.functionId;path=$s.path;match=((NormHash (TextAt $head $s.path))-eq $s.sha256)}}
}
$manifest=(TextAt $head ($batch+'/artifact-manifest.json'))|ConvertFrom-Json
$rawChecks=@($manifest.entries|ForEach-Object{$bytes=Blob $head ($batch+'/'+$_.path);[pscustomobject]@{path=$_.path;bytes=$bytes.Length;expectedBytes=$_.bytes;match=(((RawHash $bytes)-eq $_.rawSha256)-and($bytes.Length-eq$_.bytes))}})
$dep=(TextAt $head ($batch+'/dependency.json'))|ConvertFrom-Json
$deps=@($dep.files|ForEach-Object{[pscustomobject]@{path=$_.path;match=((RawHash ([IO.File]::ReadAllBytes($_.path)))-eq $_.rawSha256)}})
$prefix=(TextAt $head ($batch+'/findings-prefix.json'))|ConvertFrom-Json
$fpath='RobWork/doc/industrial-robot-design/traceability/findings.json'
$prior=Blob $prefix.base $fpath;$current=Blob $head $fpath
$n=$prefix.preservedPrefixBytes;$priorHash=RawHash $prior[0..($n-1)];$currentHash=RawHash $current[0..($n-1)]
$changed=@(& git diff --name-only $base $head)
$protected=@(& git diff --name-only 549a61834 $head -- RobWork/RobWorkStudio/src/rwslibs/industrialrobot RobWork/RobWork/src RobWork/RobWorkSim/src vcpkg)
$result=[ordered]@{head=$head;base=$base;newRows=$new.functionId;refreshedRows=$refreshed.functionId;allRowsPending=(@($rows|Where-Object {$_.status-ne'pending'-or$_.decision-ne'pending'-or$_.capabilityId-ne'NONE'}).Count-eq0);bindingCount=$checks.Count;bindingFailures=@($checks|Where-Object {-not $_.match});bindings=$checks;manifestCount=$rawChecks.Count;manifestFailures=@($rawChecks|Where-Object {-not $_.match});manifest=$rawChecks;manifestGitBlobRawSha256=(RawHash (Blob $head ($batch+'/artifact-manifest.json')));externalDependencyChecks=$deps;findingsPrefix=[ordered]@{bytes=$n;expected=$prefix.rawSha256;base=$priorHash;head=$currentHash;match=($priorHash-eq$currentHash-and$currentHash-eq$prefix.rawSha256)};scopeCount=$changed.Count;protectedChanges=$protected;catalogChanged=($changed-contains'RobWork/doc/industrial-robot-design/reuse/catalog.json')}
$result|ConvertTo-Json -Depth 15|Set-Content -Encoding utf8 (Join-Path $PSScriptRoot 'binding-results.json')
[pscustomobject]$result|Select-Object head,newRows,refreshedRows,allRowsPending,bindingCount,bindingFailures,manifestCount,manifestFailures,manifestGitBlobRawSha256,externalDependencyChecks,findingsPrefix,scopeCount,protectedChanges,catalogChanged|ConvertTo-Json -Depth 7
