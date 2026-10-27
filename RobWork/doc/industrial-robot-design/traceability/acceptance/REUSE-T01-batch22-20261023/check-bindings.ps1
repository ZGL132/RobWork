$ErrorActionPreference='Stop'
$repo='C:/Users/zgl18/AppData/Local/Temp/ird-b22-accept-7c3d9afa/RobWork'
Set-Location $repo
function Digest([string]$text) {
  $v=($text.TrimStart([char]0xFEFF) -replace "`r`n","`n").TrimEnd("`n")+"`n"
  return [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($v))).ToLowerInvariant()
}
function Require($ok,$detail) { if(-not $ok){throw $detail} }
$base='cff60ddd4a05fdae399932d8b6a3ccb5b1cdada7'
$head='daf0dcd175fb76c956154fe0213f2d791dad8575'
$p='RobWork/doc/industrial-robot-design/reuse/functions/execution.json'
$old=((& git show "${base}:$p") -join "`n")|ConvertFrom-Json
$now=Get-Content $p -Raw|ConvertFrom-Json
Require ($old.functions.Count -eq 41 -and $now.functions.Count -eq 67) 'count mismatch'
for($i=0;$i -lt 41;$i++) { Require (($old.functions[$i]|ConvertTo-Json -Depth 50 -Compress) -ceq ($now.functions[$i]|ConvertTo-Json -Depth 50 -Compress)) "changed prior row $i" }
$added=@($now.functions|Select-Object -Skip 41)
$defs=Get-Content RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01/batch22-definitions.json -Raw|ConvertFrom-Json
Require ($defs.Count -eq 26) 'definitions count'
$bindings=0
foreach($f in $added) {
 Require ($f.status -eq 'pending' -and $f.decision -eq 'pending') "resolved row $($f.functionId)"
 $d=@($defs|Where-Object functionId -CEQ $f.functionId)
 Require ($d.Count -eq 1) "definition identity $($f.functionId)"
 foreach($s in $f.sources) {
  Require (($s|ConvertTo-Json -Compress) -ceq ($d[0].source|ConvertTo-Json -Compress)) "source record mismatch $($f.functionId)"
  $lines=([IO.File]::ReadAllText((Join-Path $repo $s.path)) -replace "`r`n","`n") -split "`n"
  $region=$lines[($s.startLine-1)..($s.endLine-1)] -join "`n"
  Require ((Digest $region) -eq $s.sha256) "source hash $($f.functionId)"
 }
 foreach($b in @($f.inputs)+@($f.auditEvidence)) {
  Require ((Digest ([IO.File]::ReadAllText((Join-Path $repo $b.path)))) -eq $b.sha256) "input hash $($b.path)"
  $bindings++
 }
}
Write-Output "Rows: old 41 unchanged; new 26 pending; 26 source regions and $bindings full-file/evidence bindings match."
$j=Get-Content ../logs/ird-test-report.json -Raw|ConvertFrom-Json
[xml]$x=Get-Content ../logs/gtest.xml -Raw
Require ($j.summary.total -eq 27 -and $j.summary.passed -eq 26 -and $j.summary.failed -eq 1 -and $j.summary.skipped -eq 0 -and $j.summary.notRun -eq 0) 'JSON counts'
$cases=@($x.testsuites.testsuite.testcase)
Require ($cases.Count -eq 27 -and @($cases|Where-Object failure).Count -eq 1) 'XML counts'
foreach($t in $j.tests){
 $tc=@($cases|Where-Object{($_.classname+'.'+$_.name) -ceq $t.testId})
 Require ($tc.Count -eq 1) "XML case missing $($t.testId)"
 Require (($t.outcome -eq 'failed') -eq ($null -ne $tc[0].failure)) "XML outcome mismatch $($t.testId)"
}
$original=@($j.tests|Where-Object testId -NotMatch 'ReuseBoundary')
Require ($original.Count -eq 25 -and @($original|Where-Object outcome -NE 'passed').Count -eq 0) 'original 25 not all pass'
Write-Output 'Fresh actual execution: original25=25p/0f/0skip; new2=1p/1f/0skip; JSON/XML 27 identities and outcomes agree.'
$catalog=Get-Content RobWork/doc/industrial-robot-design/reuse/catalog.json -Raw|ConvertFrom-Json
$cap=@($catalog.capabilities|Where-Object id -eq 'UTIL-THREAD')
if($cap.Count -eq 0) { $cap=@($catalog.entries|Where-Object id -eq 'UTIL-THREAD') }
Write-Output ('Catalog top fields: '+($catalog.PSObject.Properties.Name -join ','))
Write-Output ('Commit file count: '+@(& git diff --name-only $base $head).Count)
Write-Output ('Frozen clone HEAD: '+(& git rev-parse HEAD))
Write-Output ('Frozen clone status: '+((& git status --short)-join ';'))
