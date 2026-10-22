$ErrorActionPreference='Stop'
$reviewRoot='C:/Users/zgl18/AppData/Local/Temp/REUSE-T01-B17-independent-7eb99134'
$originalRoot=Join-Path $reviewRoot 'repo/RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01/probe-r17'
$results=@()
foreach($name in @('candidates','core-red')) {
  $runPath=Join-Path $reviewRoot $name
  [xml]$xml=Get-Content -LiteralPath (Join-Path $runPath 'gtest.xml') -Raw
  $report=Get-Content -LiteralPath (Join-Path $runPath 'ird-test-report.json') -Raw | ConvertFrom-Json
  $old=Get-Content -LiteralPath (Join-Path $originalRoot "$name/ird-test-report.json") -Raw | ConvertFrom-Json
  $testcases=@($xml.SelectNodes('//testcase'))
  $failed=@($testcases | Where-Object {$_.SelectNodes('failure').Count -gt 0}).Count
  $skipped=@($testcases | Where-Object {$_.SelectNodes('skipped').Count -gt 0}).Count
  $passed=$testcases.Count-$failed-$skipped
  $exitCode=[int]([IO.File]::ReadAllText((Join-Path $runPath 'exit.txt')).Trim())
  if($report.summary.total -ne $testcases.Count -or $report.summary.passed -ne $passed -or $report.summary.failed -ne $failed -or $report.summary.skipped -ne $skipped -or $report.summary.notRun -ne 0) {throw "$name XML/report mismatch"}
  if([int]$xml.testsuites.tests -ne $testcases.Count -or [int]$xml.testsuites.failures -ne $failed -or [int]$xml.testsuites.disabled -ne 0 -or [int]$xml.testsuites.errors -ne 0) {throw "$name XML summary mismatch"}
  $ids=@($testcases | ForEach-Object {$_.classname+'.'+$_.name} | Sort-Object)
  $reportIds=@($report.tests | ForEach-Object {$_.testId} | Sort-Object)
  $oldIds=@($old.tests | ForEach-Object {$_.testId} | Sort-Object)
  if(($ids -join "`n") -ne ($reportIds -join "`n") -or ($ids -join "`n") -ne ($oldIds -join "`n")) {throw "$name test identity mismatch"}
  foreach($test in $report.tests) { if($test.outcome -ne ($old.tests | Where-Object {$_.testId -eq $test.testId}).outcome) {throw "$name original outcome mismatch"} }
  if($name -eq 'candidates' -and ($passed -ne 2 -or $failed -ne 0 -or $exitCode -ne 0)) {throw 'candidate result differs'}
  if($name -eq 'core-red' -and ($passed -ne 0 -or $failed -ne 1 -or $exitCode -ne 1)) {throw 'core result differs'}
  if($name -eq 'core-red' -and $xml.OuterXml -notmatch 'Actual: true') {throw 'missing real predicate failure'}
  $artifacts=@('gtest.xml','ird-test-report.json','stdout.log','exit.txt') | ForEach-Object {[ordered]@{path="$name/$_";rawSha256=(Get-FileHash -LiteralPath (Join-Path $runPath $_) -Algorithm SHA256).Hash.ToLowerInvariant()}}
  $results += [ordered]@{name=$name;total=$testcases.Count;passed=$passed;failed=$failed;skipped=$skipped;notRun=0;exitCode=$exitCode;testIds=$ids;matchesOriginalTestIdentityAndOutcomes=$true;xmlAndNativeReportAgree=$true;artifacts=@($artifacts)}
}
$value=[ordered]@{result='PASS';meaning='证据一致性通过；不将core真实失败改写为产品通过';runs=$results;totals=@{total=3;passed=2;failed=1;skipped=0;notRun=0};oldProbeTestsNotRun=30}
$value | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $reviewRoot 'test-evidence-check.json') -Encoding utf8
$value | ConvertTo-Json -Depth 3
