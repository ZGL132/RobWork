$ErrorActionPreference='Stop'
$root=Join-Path $PSScriptRoot 'repo/RobWork/doc/industrial-robot-design/traceability/acceptance'
$specs=@(
 @{name='B9r2';source='C:/Users/zgl18/AppData/Local/Temp/REUSE-T01-batch9-r2-fff40861';dest=(Join-Path $root 'REUSE-T01-batch9-r2-20261023')},
 @{name='coverage';source='C:/Users/zgl18/AppData/Local/Temp/ird-coverage-t01-20261022-234816-264333';dest=(Join-Path $root 'REUSE-T01-coverage-20261023')}
)
$out=@()
foreach($s in $specs){
 $files=@(Get-ChildItem -LiteralPath $s.source -File)
 foreach($dir in Get-ChildItem -LiteralPath $s.source -Directory){if($dir.Name-ne'repo'){$files+=@(Get-ChildItem -LiteralPath $dir.FullName -File -Recurse)}}
 foreach($f in $files){$rel=[IO.Path]::GetRelativePath($s.source,$f.FullName);$to=Join-Path $s.dest $rel;$exists=Test-Path -LiteralPath $to;$raw=$false;$norm=$false
 if($exists){$raw=(Get-FileHash -LiteralPath $f.FullName).Hash-eq(Get-FileHash -LiteralPath $to).Hash;$norm=[IO.File]::ReadAllText($f.FullName).Replace("`r`n","`n")-ceq[IO.File]::ReadAllText($to).Replace("`r`n","`n")}
 $out+=[pscustomobject]@{archive=$s.name;path=$rel;exists=$exists;raw=$raw;normalized=$norm}
 }
}
$out|ConvertTo-Json -Depth 5|Set-Content (Join-Path $PSScriptRoot 'archive-checks.json')
$out|Group-Object archive|ForEach-Object {[pscustomobject]@{archive=$_.Name;files=$_.Count;raw=@($_.Group|Where-Object raw).Count;normalizedOnly=@($_.Group|Where-Object {$_.normalized-and-not$_.raw}).Count;failed=@($_.Group|Where-Object {-not$_.normalized}).Count}}
$out|Where-Object {-not$_.normalized}|Format-Table
