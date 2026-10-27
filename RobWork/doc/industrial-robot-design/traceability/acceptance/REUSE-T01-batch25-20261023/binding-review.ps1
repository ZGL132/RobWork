param([string]$Root)
$ErrorActionPreference='Stop'
$repo=Join-Path $Root 'repo';$sha='d0fcb96f62a863684848161f7b8ecd93ef0be302';$parent='e60d5d3c9a47a24010964295a935d42986a290e1'
function Blob([string]$rev,[string]$path){
 $pi=[Diagnostics.ProcessStartInfo]::new();$pi.FileName='git';$pi.WorkingDirectory=$repo;$pi.Arguments='cat-file blob '+$rev+':'+$path;$pi.UseShellExecute=$false;$pi.RedirectStandardOutput=$true;$pi.RedirectStandardError=$true
 $p=[Diagnostics.Process]::Start($pi);$m=[IO.MemoryStream]::new();$p.StandardOutput.BaseStream.CopyTo($m);$err=$p.StandardError.ReadToEnd();$p.WaitForExit();if($p.ExitCode){throw $err};return ,$m.ToArray()
}
function Hash([byte[]]$bytes){return [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes)).ToLowerInvariant()}
function Digest([string]$s){Hash ([Text.Encoding]::UTF8.GetBytes(($s.TrimStart([char]0xfeff).Replace("`r`n","`n").TrimEnd("`n"))+"`n"))}
function TextBlob([string]$rev,[string]$path){[Text.Encoding]::UTF8.GetString((Blob $rev $path))}
$matrix='RobWork/doc/industrial-robot-design/reuse/functions/foundation-governance.json'
$before=(TextBlob $parent $matrix | ConvertFrom-Json).functions;$after=(TextBlob $sha $matrix | ConvertFrom-Json).functions
$old=@{};foreach($r in $before){$old[$r.functionId]=$r}
$new=@();$changed=@();$same=0;$checks=@();$errors=@()
foreach($r in $after){
 $id=$r.functionId
 if(-not $old.ContainsKey($id)){$new+=,$r}else{if(($r|ConvertTo-Json -Depth 50 -Compress) -ceq ($old[$id]|ConvertTo-Json -Depth 50 -Compress)){$same++;continue}else{$changed+=,$r}}
 if($r.status -ne 'pending' -or $r.decision -ne 'pending'){$errors+='status '+$id}
 foreach($s in $r.sources){
  $lines=(TextBlob $sha $s.path).TrimStart([char]0xfeff).Replace("`r`n","`n").Split("`n");$region=$lines[($s.startLine-1)..($s.endLine-1)] -join "`n"
  $ok=((Digest $region)-eq $s.sha256 -and $region.Contains([string]$s.symbol));$checks+=[pscustomobject]@{id=$id;kind='source';path=$s.path;start=$s.startLine;end=$s.endLine;symbol=$s.symbol;passed=$ok};if(-not $ok){$errors+='source '+$id}
 }
 foreach($kind in @('inputs','auditEvidence')){foreach($b in $r.$kind){$ok=(Digest (TextBlob $sha $b.path)) -eq $b.sha256;$checks+=[pscustomobject]@{id=$id;kind=$kind;path=$b.path;passed=$ok};if(-not $ok){$errors+=$kind+' '+$id+' '+$b.path}}}
 if($r.execution.entry -notin @($r.inputs.path)){$errors+='entry not full bound '+$id}
 foreach($s in $r.sources){if($s.path -notin @($r.inputs.path)){$errors+='source not full bound '+$id}}
}
$removed=@($before | Where-Object functionId -notin @($after.functionId));if($removed.Count){$errors+='removed rows'}
$expected=(Get-Content (Join-Path $repo 'RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01/batch25/verification.json') -Raw|ConvertFrom-Json).refreshedPendingRows
if(@(Compare-Object @($changed.functionId|Sort-Object) @($expected|Sort-Object)).Count){$errors+='changed list differs'}
$deltas=@();foreach($r in $changed){$props=@();foreach($p in $r.PSObject.Properties){$prior=$old[$r.functionId].($p.Name);if(($p.Value|ConvertTo-Json -Depth 50 -Compress) -cne ($prior|ConvertTo-Json -Depth 50 -Compress)){$props+=$p.Name}};$deltas+=[pscustomobject]@{id=$r.functionId;changedProperties=$props};if(@($props|Where-Object{$_ -notin @('sources','inputs','auditEvidence')}).Count){$errors+='unexpected old-row mutation '+$r.functionId}}
$archive='RobWork/doc/industrial-robot-design/traceability/acceptance/REUSE-T01-batch23-20261023/'
$original='C:/Users/zgl18/AppData/Local/Temp/REUSE-T01-batch23-accept-a887070b624e4eb7837db502b230cbb0'
$manifest=TextBlob $sha ($archive+'artifact-manifest.json')|ConvertFrom-Json;$archiveChecks=@()
foreach($e in @($manifest)+@([pscustomobject]@{path='artifact-manifest.json'})){
 $raw=Blob $sha ($archive+$e.path);$origin=[IO.File]::ReadAllBytes((Join-Path $original $e.path));$actual=Hash $raw;$originHash=Hash $origin;$manifestOk=($e.path -eq 'artifact-manifest.json' -or ($actual -eq $e.rawSha256 -and $raw.Length -eq $e.bytes));$ok=($actual -eq $originHash -and $manifestOk)
 $archiveChecks+=[pscustomobject]@{path=$e.path;gitBytes=$raw.Length;gitRawSha256=$actual;originalBytes=$origin.Length;originalRawSha256=$originHash;manifestMatch=$manifestOk;passed=$ok};if(-not $ok){$errors+='archive '+$e.path}
}
$beforeChecks=@();foreach($pair in @(@('state-before.ps1','validate-state.ps1'),@('pipeline-before.ps1','test-pipeline-pipeline.ps1'))){$stored=Blob $sha ('RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01/batch25/'+$pair[0]);$original=Blob $parent ('RobWork/scripts/industrialrobot/'+$pair[1]);$normOk=(Digest ([Text.Encoding]::UTF8.GetString($stored))) -eq (Digest ([Text.Encoding]::UTF8.GetString($original)));$beforeChecks+=[pscustomobject]@{stored=$pair[0];parent=$pair[1];rawSame=((Hash $stored) -eq (Hash $original));storedRawSha256=(Hash $stored);parentBlobRawSha256=(Hash $original);normalizedSame=$normOk};if(-not $normOk){$errors+='before not parent '+$pair[0]}}
$checks|ConvertTo-Json -Depth 10|Set-Content (Join-Path $Root 'binding-checks.json') -Encoding utf8
$archiveChecks|ConvertTo-Json -Depth 10|Set-Content (Join-Path $Root 'b23-git-blob-original-checks.json') -Encoding utf8
$deltas|ConvertTo-Json -Depth 10|Set-Content (Join-Path $Root 'old-row-property-deltas.json') -Encoding utf8
$summary=[pscustomobject]@{frozen=$sha;parent=$parent;newCount=$new.Count;changedCount=$changed.Count;unchangedOld=$same;removedCount=$removed.Count;newIds=@($new.functionId);changedIds=@($changed.functionId);bindings=$checks.Count;bindingFailed=@($checks|Where-Object{-not $_.passed}).Count;archiveManifestCount=$manifest.Count;archiveIncludingManifest=$archiveChecks.Count;archiveFailed=@($archiveChecks|Where-Object{-not $_.passed}).Count;before=$beforeChecks;errors=$errors}
$summary|ConvertTo-Json -Depth 10|Set-Content (Join-Path $Root 'binding-summary.json') -Encoding utf8
$summary|ConvertTo-Json -Depth 10
if($errors.Count){exit 1}
