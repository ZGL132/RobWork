$ErrorActionPreference='Stop'
$root=Join-Path $PSScriptRoot 'repo'
Set-Location $root
$head='549a61834bea9755b873dde478ee80c5b9f4b15a'; $base='a02785a10'
$design='RobWork/doc/industrial-robot-design';$batch="$design/traceability/reuse/REUSE-T01/batch32"
function Blob([string]$rev,[string]$path){
 $p=[Diagnostics.Process]::new();$p.StartInfo.FileName='git';$p.StartInfo.WorkingDirectory=$root;$p.StartInfo.UseShellExecute=$false;$p.StartInfo.RedirectStandardOutput=$true;$p.StartInfo.ArgumentList.Add('show');$p.StartInfo.ArgumentList.Add("${rev}:$path");$p.Start()|Out-Null;$m=[IO.MemoryStream]::new();$p.StandardOutput.BaseStream.CopyTo($m);$p.WaitForExit();if($p.ExitCode){throw 'git blob failed'};return ,$m.ToArray()
}
function Hash([byte[]]$bytes){[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes)).ToLowerInvariant()}
function Text([byte[]]$bytes){[Text.Encoding]::UTF8.GetString($bytes).TrimStart([char]0xFEFF)}
function Canon([string]$s){($s.Replace("`r`n","`n").TrimEnd("`n")+"`n")}
function Digest([string]$s){Hash ([Text.Encoding]::UTF8.GetBytes((Canon $s)))}
$allRows=@();$errors=@();$counts=@();$excerpts=@();$raws=@()
foreach($unit in 'core','execution','ui'){
 $path="$design/reuse/functions/$unit.json";$old=(Text (Blob $base $path)|ConvertFrom-Json).functions;$new=(Text (Blob $head $path)|ConvertFrom-Json).functions
 $map=@{};foreach($r in $old){$map[$r.functionId]=$r}
 $added=@($new|Where-Object {-not $map.ContainsKey($_.functionId)})
 $changed=@($new|Where-Object {$map.ContainsKey($_.functionId) -and (($_|ConvertTo-Json -Depth 30 -Compress) -cne ($map[$_.functionId]|ConvertTo-Json -Depth 30 -Compress))})
 $counts+=@{unit=$unit;added=$added.Count;refreshed=$changed.Count;refreshedIds=@($changed.functionId)}
 foreach($r in @($added)+@($changed)){
  if($r.status -ne 'pending' -or $r.decision -ne 'pending'){$errors+="nonpending $($r.functionId)"}
  foreach($s in $r.sources){$txt=Text (Blob $head $s.path);$lines=(Canon $txt).Split("`n");$slice=($lines[($s.startLine-1)..($s.endLine-1)] -join "`n")+"`n";$actual=Digest $slice;if($actual -cne $s.sha256){$errors+="source hash $($r.functionId)"};$allRows+=@{functionId=$r.functionId;path=$s.path;start=$s.startLine;end=$s.endLine;qualifiedSymbol=$s.qualifiedSymbol;declared=$s.sha256;actual=$actual;symbolPresent=$slice.Contains($s.symbol)};$excerpts+="### $($r.functionId) | $($s.qualifiedSymbol) | $($s.path):$($s.startLine)-$($s.endLine)`n$slice"}
  foreach($i in @($r.inputs)+@($r.auditEvidence)){ $actual=Digest (Text (Blob $head $i.path));if($actual -cne $i.sha256){$errors+="input/evidence hash $($r.functionId) $($i.path)"}}
 }
}
$manifest=Text (Blob $head "$batch/artifact-manifest.json")|ConvertFrom-Json
foreach($e in $manifest.entries){$p="$batch/$($e.path)";$blob=Blob $head $p;$disk=[IO.File]::ReadAllBytes((Join-Path $root $p));$actual=Hash $blob;$diskHash=Hash $disk;$ok=$actual -ceq $e.rawSha256 -and $blob.Length -eq $e.bytes -and $diskHash -ceq $actual;if(-not $ok){$errors+="manifest $p"};$raws+=@{path=$p;bytes=$blob.Length;gitBlobRawSha256=$actual;worktreeRawSha256=$diskHash;expected=$e.rawSha256;pass=$ok}}
$mp="$batch/artifact-manifest.json";$mb=Blob $head $mp;$raws+=@{path=$mp;bytes=$mb.Length;gitBlobRawSha256=(Hash $mb);worktreeRawSha256=(Hash ([IO.File]::ReadAllBytes((Join-Path $root $mp))));selfReference=$false}
$fp="$design/traceability/findings.json";$before=Blob $base $fp;$after=Blob $head $fp;$prefix=[byte[]]$after[0..1289097];$oldPrefix=[byte[]]$before[0..1289097];$prefixHash=Hash $prefix;$prefixPass=($prefixHash -ceq (Hash $oldPrefix)) -and $prefixHash -ceq 'a751ddefc1b3e6a2f557016af32862cc5e79c01d5f87e2e07f3a9744ee2c72b6';if(-not $prefixPass){$errors+='findings prefix'}
$fOld=(Text $before|ConvertFrom-Json);$fNew=(Text $after|ConvertFrom-Json)
$paths=@(git diff --name-only $base $head);$task=Get-Content "$design/tasks/REUSE-T01.json" -Raw|ConvertFrom-Json
$scope=@();foreach($p in $paths){$allowed=@($task.allowedFiles|Where-Object {$p -like $_}).Count -gt 0;$forbidden=@($task.forbiddenFiles|Where-Object {$p -like $_}).Count -gt 0;if(-not $allowed -or $forbidden){$errors+="scope $p"};$scope+=@{path=$p;allowed=$allowed;forbidden=$forbidden}}
@{head=$head;base=(git rev-parse $base);counts=$counts;rows=$allRows;errors=$errors;manifestCount=$manifest.entries.Count;rawArtifacts=$raws;findingsPrefix=@{bytes=1289098;sha256=$prefixHash;pass=$prefixPass};scopeCount=$paths.Count;scope=$scope}|ConvertTo-Json -Depth 20|Set-Content "$PSScriptRoot/static-audit.json" -Encoding utf8
$excerpts|Set-Content "$PSScriptRoot/source-excerpts.md" -Encoding utf8
$counts|ConvertTo-Json -Depth 5
Write-Output "manifest=$($manifest.entries.Count) scope=$($paths.Count) errors=$($errors.Count) prefix=$prefixPass"
$errors
