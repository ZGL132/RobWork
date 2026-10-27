$ErrorActionPreference='Stop'
$b=$PSScriptRoot;$r=Join-Path $b 'repo';$design=Join-Path $r 'RobWork/doc/industrial-robot-design';$arc=Join-Path $design 'traceability/acceptance';$records=@()
$roots=@{ 'REUSE-T01-batch12-r2-20261023'='C:/Users/zgl18/AppData/Local/Temp/ird-accept-b12-r2-20261023-005243-1428146295'; 'REUSE-T01-batch13-20261023'='C:/Users/zgl18/AppData/Local/Temp/ird-accept-b13-e26aa5c7a08d4853bc745a9142615c69' }
foreach($name in $roots.Keys){foreach($f in (Get-ChildItem -LiteralPath (Join-Path $arc $name) -File)){
 $src=Join-Path $roots[$name] $f.Name;$exists=Test-Path -LiteralPath $src;$raw=$false;$nl=$false
 if($exists){$raw=((Get-FileHash -LiteralPath $src -Algorithm SHA256).Hash -ceq (Get-FileHash -LiteralPath $f.FullName -Algorithm SHA256).Hash);$nl=(([IO.File]::ReadAllText($src) -replace "`r`n","`n") -ceq ([IO.File]::ReadAllText($f.FullName) -replace "`r`n","`n"))}
 $records += [pscustomobject]@{archive=$name;file=$f.Name;source=$src;exists=$exists;rawMatch=$raw;newlineOnly=(!$raw -and $nl);pass=($raw -or $nl)}
}}
$records|ConvertTo-Json -Depth 10|Set-Content (Join-Path $b 'archive-checks.json') -Encoding utf8
$files=@(& git -C $r diff --name-only HEAD^ HEAD);$files|Set-Content (Join-Path $b 'changed-files.log')
$excluded=@($files|Where-Object {$_ -match '/traceability/acceptance/.+\.log$' -or $_ -match '/traceability/reuse/REUSE-T01/batch14-(task|docs)\.log$'})
$included=@($files|Where-Object {$_ -notin $excluded})
& git -C $r diff --check HEAD^ HEAD -- @included *> (Join-Path $b 'scoped-diff-check.log');$diffExit=$LASTEXITCODE;$diffExit|Set-Content (Join-Path $b 'scoped-diff-check.exit.txt')
$excluded|Set-Content (Join-Path $b 'diff-check-exclusions.log')
& git -C $r diff --name-only HEAD^ HEAD -- RobWork/RobWorkStudio/src RobWork/RobWork/src RobWork/RobWorkSim/src vcpkg *> (Join-Path $b 'product-upstream-diff.log')
& git -C $r status --porcelain=v1 *> (Join-Path $b 'final-status.log')
$all=@();Get-ChildItem (Join-Path $design 'reuse/functions') -Filter '*.json'|ForEach-Object {$all+=@((Get-Content $_.FullName -Raw|ConvertFrom-Json).functions)}
[ordered]@{archiveFiles=$records.Count;rawMatches=@($records|Where-Object {$_.rawMatch}).Count;newlineOnly=@($records|Where-Object {$_.newlineOnly}).Count;failures=@($records|Where-Object {!$_.pass});changedFiles=$files.Count;diffCheckExit=$diffExit;excludedLogs=$excluded;allRelations=$all.Count;decisions=@($all|Group-Object decision,status|ForEach-Object {[ordered]@{name=$_.Name;count=$_.Count}})}|ConvertTo-Json -Depth 10|Tee-Object (Join-Path $b 'final-facts.json')
