$ErrorActionPreference='Stop'
$root=Join-Path $PSScriptRoot 'repo'
Set-Location $root
$design='RobWork/doc/industrial-robot-design'
$batch="$design/traceability/reuse/REUSE-T01"
function Hash([string]$s){$s=$s.TrimStart([char]0xfeff).Replace("`r`n","`n").TrimEnd("`n")+"`n";$sha=[Security.Cryptography.SHA256]::Create();try{([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($s)))).Replace('-','').ToLowerInvariant()}finally{$sha.Dispose()}}
function Strip([string]$s){$s=[regex]::Replace($s,'(?s)/\*.*?\*/','');$s=[regex]::Replace($s,'(?m)//.*$','');[regex]::Replace($s,'\s','')}
$inv=Get-Content "$batch/test-build-inventory.json" -Raw|ConvertFrom-Json
$all=@();$changes=@()
foreach($p in @(git diff --name-only a7171137a77048e2f1102b80051fb66fce52daa9 HEAD -- "$design/reuse/functions")){
 $old=(@(git show ('a7171137a77048e2f1102b80051fb66fce52daa9:'+$p))-join "`n")|ConvertFrom-Json
 $new=Get-Content $p -Raw|ConvertFrom-Json
 $all+=$new.functions
 foreach($f in $new.functions){$json=$f|ConvertTo-Json -Depth 30 -Compress;$same=@($old.functions|Where-Object {($_|ConvertTo-Json -Depth 30 -Compress)-ceq$json});if($same.Count-eq0){$changes+=[pscustomobject]@{path=$p;row=$f;kind=$(if(@($old.functions|Where-Object functionId -eq $f.functionId).Count){'updated'}else{'added'})}}}
}
$changes|ForEach-Object {[pscustomobject]@{functionId=$_.row.functionId;kind=$_.kind}}|ConvertTo-Json -Depth 5|Set-Content (Join-Path $PSScriptRoot 'exact-changes.json')
$mainChecks=@();$excerpts=[Collections.Generic.List[string]]::new()
foreach($m in $inv.mains){
 $s=$m.source;$lines=[IO.File]::ReadAllLines((Join-Path $root $s.path));$text=$lines[($s.startLine-1)..($s.endLine-1)]-join "`n";$norm=Strip $text
 $excerpts.Add("MAIN $($m.path) $($s.startLine)-$($s.endLine)");$excerpts.Add($text)
 $cm=Get-Content $m.cmake.path -Raw;$relative=$m.path.Substring(($m.cmake.path -replace 'CMakeLists.txt$','').Length)
 $consumers=@();foreach($match in [regex]::Matches($cm,'(?ms)^\s*add_executable\(\s*(\S+)\s+(.*?)\)')){$block=[regex]::Replace($match.Groups[2].Value,'(?m)#.*$','');if($block -match ('(?<![\w/])'+[regex]::Escape($relative)+'(?=\s|$)')){$consumers+=$match.Groups[1].Value}}
 $mainChecks+=[pscustomobject]@{path=$m.path;start=$s.startLine;end=$s.endLine;qualified=$s.qualifiedSymbol;hashMatches=((Hash $text)-eq$s.sha256);definitionStarts=($text-match'^int main\(int argc, char\*\* argv\)');definitionEnds=($text.TrimEnd().EndsWith('}'));recordedTargets=@($m.targets);actualDirectConsumers=$consumers;targetMatch=(@(Compare-Object @($m.targets|Sort-Object) @($consumers|Sort-Object)).Count-eq0);groupMatches=(([BitConverter]::ToString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($norm)))).Replace('-','').ToLowerInvariant()-eq$m.codeGroup);normalized=$norm}
}
$mainChecks|ConvertTo-Json -Depth 8|Set-Content (Join-Path $PSScriptRoot 'main-checks.json')
$macroChecks=@();foreach($m in $inv.macros){
 $s=$m.source;$lines=[IO.File]::ReadAllLines((Join-Path $root $s.path));$text=$lines[($s.startLine-1)..($s.endLine-1)]-join "`n";$cm=Get-Content $s.path -Raw
 $excerpts.Add("MACRO $($m.unit) $($s.startLine)-$($s.endLine)");$excerpts.Add($text)
 $actual=@([regex]::Matches($cm,'(?m)^\s*ird_add_gtest\(\s*([^\s)]+)\s*\)')|ForEach-Object {$_.Groups[1].Value})
 $f=$all|Where-Object functionId -eq ($m.unit+'.CMake.ird_add_gtest')
 $macroChecks+=[pscustomobject]@{unit=$m.unit;source=$s;hashMatches=((Hash $text)-eq$s.sha256);inventoryTargets=$m.targets;functionTargets=$f.targets;actualConsumers=$actual;falseTargets=@($m.targets|Where-Object {$_-notin$actual});missingTargets=@($actual|Where-Object {$_-notin$m.targets});normalized=([regex]::Replace(([regex]::Replace($text,'(?m)#.*$','')),'\s',''))}
}
$macroChecks|ConvertTo-Json -Depth 8|Set-Content (Join-Path $PSScriptRoot 'macro-checks.json')
$excerpts|Set-Content (Join-Path $PSScriptRoot 'main-macro-source.txt')
$hashes=@();foreach($ch in $changes){foreach($a in $ch.row.auditEvidence){$hashes+=[pscustomobject]@{path=$a.path;ok=((Hash ([IO.File]::ReadAllText((Join-Path $root $a.path))))-eq$a.sha256)}}}
$hashes|Sort-Object path -Unique|ConvertTo-Json|Set-Content (Join-Path $PSScriptRoot 'evidence-hashes.json')
$envr=Get-Content "$batch/probe-r10/environment-artifacts.json" -Raw|ConvertFrom-Json
$probe=@();foreach($s in $envr.sources){$probe+=[pscustomobject]@{path=$s.path;ok=((Hash ([IO.File]::ReadAllText((Join-Path $root $s.path))))-eq$s.sha256)}}
foreach($s in $envr.externalArtifacts){$probe+=[pscustomobject]@{path=$s.path;ok=((Get-FileHash -LiteralPath $s.path -Algorithm SHA256).Hash.ToLowerInvariant()-eq$s.rawSha256)}}
$probe|ConvertTo-Json|Set-Content (Join-Path $PSScriptRoot 'probe-hashes.json')
[pscustomobject]@{changed=$changes.Count;added=@($changes|Where-Object kind -eq added).Count;updated=@($changes|Where-Object kind -eq updated).Count;mains=$mainChecks.Count;mainTargetMismatch=@($mainChecks|Where-Object targetMatch -eq $false).Count;mainHashMismatch=@($mainChecks|Where-Object hashMatches -eq $false).Count;macroCount=$macroChecks.Count;macroNormalizedGroups=@($macroChecks.normalized|Sort-Object -Unique).Count;macroUnitsWithFalseTargets=@($macroChecks|Where-Object {$_.falseTargets.Count}).Count;falseTargetCount=@($macroChecks.falseTargets).Count;evidenceHashFailures=@($hashes|Where-Object ok -eq $false).Count;probeHashFailures=@($probe|Where-Object ok -eq $false).Count}|ConvertTo-Json
$mainChecks|Where-Object targetMatch -eq $false|Select-Object path,recordedTargets,actualDirectConsumers|ConvertTo-Json -Depth 4
$macroChecks|Where-Object {$_.falseTargets.Count}|Select-Object unit,falseTargets,missingTargets|ConvertTo-Json -Depth 4

