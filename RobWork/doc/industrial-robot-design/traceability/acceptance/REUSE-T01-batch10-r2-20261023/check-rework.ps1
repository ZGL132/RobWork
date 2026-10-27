$ErrorActionPreference='Stop'
$root=Join-Path $PSScriptRoot 'repo'
Set-Location $root
$design='RobWork/doc/industrial-robot-design'
$batch="$design/traceability/reuse/REUSE-T01"
function Hash([string]$s){$s=$s.TrimStart([char]0xfeff).Replace("`r`n","`n").TrimEnd("`n")+"`n";$sha=[Security.Cryptography.SHA256]::Create();try{([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($s)))).Replace('-','').ToLowerInvariant()}finally{$sha.Dispose()}}
function SetEqual($a,$b){@((Compare-Object @($a|Sort-Object) @($b|Sort-Object))).Count-eq0}
$inv=Get-Content "$batch/test-build-inventory.json" -Raw|ConvertFrom-Json
$oldInv=(@(git show ('e0bee2994eee0e8f3299ea5ccf200c354f4454be:'+$batch+'/test-build-inventory.json'))-join "`n")|ConvertFrom-Json
$checks=@();$rows=@();$hashes=@();$removed=@();$evidenceRows=@()
$ids=(Get-Content 'C:/Users/zgl18/AppData/Local/Temp/REUSE-T01-batch10-r1-ff13cbaf/exact-changes.json' -Raw|ConvertFrom-Json).functionId
foreach($file in Get-ChildItem "$design/reuse/functions" -File -Filter '*.json'){$rows+=(Get-Content $file.FullName -Raw|ConvertFrom-Json).functions}
foreach($m in $inv.macros){
 $s=$m.source;$cm=[IO.File]::ReadAllText((Join-Path $root $s.path));$actual=@([regex]::Matches($cm,'(?m)^\s*ird_add_gtest\(\s*([^\s)]+)\s*\)')|ForEach-Object {$_.Groups[1].Value})
 $f=@($rows|Where-Object functionId -eq ($m.unit+'.CMake.ird_add_gtest'));if($f.Count-ne1){throw ('Macro rows '+$m.unit)}
 $old=$oldInv.macros|Where-Object unit -eq $m.unit
 $removed+=[pscustomobject]@{unit=$m.unit;targets=@($old.targets|Where-Object {$_-notin$m.targets})}
 $checks+=[pscustomobject]@{unit=$m.unit;actual=$actual;inventory=$m.targets;function=$f[0].targets;inventoryMatches=(SetEqual $actual $m.targets);functionMatches=(SetEqual $actual $f[0].targets);uniqueActual=(@($actual|Sort-Object -Unique).Count-eq$actual.Count);inventorySourceMatchesFunction=(($s|ConvertTo-Json -Compress)-ceq($f[0].sources[0]|ConvertTo-Json -Compress))}
 foreach($src in @($s)+@($f[0].sources)){$lines=[IO.File]::ReadAllLines((Join-Path $root $src.path));$hashes+=[pscustomobject]@{kind='macroSource';path=$src.path;ok=((Hash ($lines[($src.startLine-1)..($src.endLine-1)]-join "`n"))-eq$src.sha256)}}
 $hashes+=[pscustomobject]@{kind='macroInput';path=$m.input.path;ok=((Hash ([IO.File]::ReadAllText((Join-Path $root $m.input.path))))-eq$m.input.sha256)}
}
foreach($f in $rows|Where-Object {$_.functionId-in$ids}){
 $evidenceRows+=$f.functionId
 foreach($a in $f.auditEvidence){$hashes+=[pscustomobject]@{kind='auditEvidence';functionId=$f.functionId;path=$a.path;ok=((Hash ([IO.File]::ReadAllText((Join-Path $root $a.path))))-eq$a.sha256)}}
 foreach($a in $f.inputs){$hashes+=[pscustomobject]@{kind='functionInput';functionId=$f.functionId;path=$a.path;ok=((Hash ([IO.File]::ReadAllText((Join-Path $root $a.path))))-eq$a.sha256)}}
}
$refs=@($rows|Where-Object {$_.auditEvidence.path-contains($batch+'/test-build-inventory.json')})
foreach($f in $refs){foreach($a in $f.auditEvidence|Where-Object path -eq ($batch+'/test-build-inventory.json')){$hashes+=[pscustomobject]@{kind='allInventoryReferences';functionId=$f.functionId;path=$a.path;ok=((Hash ([IO.File]::ReadAllText((Join-Path $root $a.path))))-eq$a.sha256)}}}
$checks|ConvertTo-Json -Depth 8|Set-Content (Join-Path $PSScriptRoot 'macro-target-checks.json')
$removed|Where-Object {$_.targets.Count}|ConvertTo-Json -Depth 5|Set-Content (Join-Path $PSScriptRoot 'removed-targets.json')
$hashes|ConvertTo-Json -Depth 5|Set-Content (Join-Path $PSScriptRoot 'hash-checks.json')
[pscustomobject]@{macros=$checks.Count;failedTargets=@($checks|Where-Object {-not$_.inventoryMatches-or-not$_.functionMatches}).Count;removedUnits=@($removed|Where-Object {$_.targets.Count}).Count;removedTargets=@($removed.targets).Count;evidenceRows=$evidenceRows.Count;inventoryReferences=$refs.Count;hashChecks=$hashes.Count;failedHashes=@($hashes|Where-Object {-not$_.ok}).Count;mainInventoryUnchanged=(($inv.mains|ConvertTo-Json -Depth 20 -Compress)-ceq($oldInv.mains|ConvertTo-Json -Depth 20 -Compress))}|ConvertTo-Json|Tee-Object (Join-Path $PSScriptRoot 'facts.json')
$specs=@(
 @{name='B10-FAIL';source='C:/Users/zgl18/AppData/Local/Temp/REUSE-T01-batch10-r1-ff13cbaf';dest="$design/traceability/acceptance/REUSE-T01-batch10-r1-20261023"},
 @{name='GTest-PASS';source='C:/Users/zgl18/AppData/Local/Temp/ird-accept-gtest-listeners-675a9c64d3b04ef7bc0519b539926fb7';dest="$design/traceability/acceptance/REUSE-T01-gtest-listener-20261023"}
)
$archive=@();foreach($s in $specs){
 foreach($f in Get-ChildItem -LiteralPath $s.dest -File -Recurse){$rel=[IO.Path]::GetRelativePath((Join-Path $root $s.dest),$f.FullName);$original=Join-Path $s.source $rel;$exists=Test-Path -LiteralPath $original;$raw=$false;$norm=$false;if($exists){$raw=(Get-FileHash -LiteralPath $original).Hash-eq(Get-FileHash -LiteralPath $f.FullName).Hash;$norm=[IO.File]::ReadAllText($original).Replace("`r`n","`n")-ceq[IO.File]::ReadAllText($f.FullName).Replace("`r`n","`n")};$archive+=[pscustomobject]@{archive=$s.name;path=$rel;exists=$exists;raw=$raw;normalized=$norm}}
 foreach($f in Get-ChildItem -LiteralPath $s.source -File){if(-not(Test-Path -LiteralPath (Join-Path $s.dest $f.Name))){$archive+=[pscustomobject]@{archive=$s.name;path=$f.Name;exists=$false;raw=$false;normalized=$false}}}
}
$archive|ConvertTo-Json -Depth 5|Set-Content (Join-Path $PSScriptRoot 'archive-checks.json')
$archive|Group-Object archive|ForEach-Object {[pscustomobject]@{archive=$_.Name;files=$_.Count;raw=@($_.Group|Where-Object raw).Count;newlineOnly=@($_.Group|Where-Object {$_.normalized-and-not$_.raw}).Count;failed=@($_.Group|Where-Object {-not$_.normalized}).Count}}
$archive|Where-Object {-not$_.normalized}|Format-Table
