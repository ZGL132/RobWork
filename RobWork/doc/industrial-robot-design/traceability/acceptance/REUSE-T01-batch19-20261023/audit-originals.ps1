$ErrorActionPreference='Stop'
$root=Join-Path $PSScriptRoot 'repo'
$out=Join-Path $PSScriptRoot 'evidence'
$batch=Join-Path $root 'RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01/batch19'
$checks=[Collections.Generic.List[object]]::new()
function Check($name,$ok,$detail) {
    $checks.Add([pscustomobject]@{name=$name;passed=[bool]$ok;detail=$detail})
    Write-Output "$name : $ok $detail"
}
function BlobHash($ref,[switch]$WindowsCheckout) {
    $p=[Diagnostics.Process]::new();$p.StartInfo.FileName='git';$p.StartInfo.WorkingDirectory=$root
    $p.StartInfo.UseShellExecute=$false;$p.StartInfo.RedirectStandardOutput=$true
    foreach($arg in @('cat-file','blob',$ref)){$p.StartInfo.ArgumentList.Add($arg)}
    [void]$p.Start();$stream=[IO.MemoryStream]::new();$p.StandardOutput.BaseStream.CopyTo($stream);$p.WaitForExit()
    if($p.ExitCode){throw "git blob failed: $ref"}
    $sha=[Security.Cryptography.SHA256]::Create()
    try{
        $bytes=$stream.ToArray()
        if($WindowsCheckout){$bytes=[Text.Encoding]::UTF8.GetBytes([Text.Encoding]::UTF8.GetString($bytes).Replace("`r`n","`n").Replace("`n","`r`n"))}
        return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-','').ToLowerInvariant()
    }finally{$sha.Dispose();$stream.Dispose();$p.Dispose()}
}
$verification=Get-Content (Join-Path $batch 'verification.json') -Raw|ConvertFrom-Json
foreach($i in $verification.rawInputs){
    $hash=(Get-FileHash (Join-Path $root $i.path)).Hash.ToLowerInvariant()
    $blobHash=BlobHash ('HEAD:'+$i.path)
    $checkoutHash=BlobHash ('HEAD:'+$i.path) -WindowsCheckout
    $sharedHash=(Get-FileHash (Join-Path 'C:/Users/zgl18/.codex/worktrees/reuse-audit/RobWork' $i.path)).Hash.ToLowerInvariant()
    Check ('raw-'+[IO.Path]::GetFileName($i.path)) ($sharedHash -eq $i.sha256 -and $i.sha256 -in @($blobHash,$checkoutHash)) "report=$($i.sha256) gitBlob=$blobHash windowsCRLF=$checkoutHash tempCheckout=$hash"
}
foreach($kind in @('ps7','ps51','red','intermediate-green')) {
    $dir=Join-Path $batch $kind
    $r=Get-Content (Join-Path $dir 'reuse-test-report.json') -Raw|ConvertFrom-Json
    $x=[xml](Get-Content (Join-Path $dir 'reuse-governance.junit.xml') -Raw)
    $cases=@($x.testsuite.testcase)
    $failures=@($x.SelectNodes('//testcase/failure')).Count
    $passed=@($r.tests|Where-Object passed).Count
    $expected=152;$wantPassed=152;$wantFailed=0
    if($kind -eq 'red'){$expected=8;$wantPassed=0;$wantFailed=8}
    if($kind -eq 'intermediate-green'){$expected=8;$wantPassed=8}
    Check "$kind-counts" ($r.expected -eq $expected -and $r.executed -eq $expected -and $r.tests.Count -eq $expected -and $r.passed -eq $wantPassed -and $passed -eq $wantPassed -and $r.failed -eq $wantFailed -and $r.skipped -eq 0 -and $r.notRun -eq 0 -and $r.complete -and -not $r.driverFailure -and $cases.Count -eq $expected -and [int]$x.testsuite.tests -eq $expected -and [int]$x.testsuite.failures -eq $wantFailed -and $failures -eq $wantFailed -and @($x.SelectNodes('//error|//skipped')).Count -eq 0) "expected=$expected passed=$passed failed=$failures"
    $bad=[Collections.Generic.List[string]]::new()
    $driver=[IO.File]::ReadAllText((Join-Path $dir 'driver.log'))
    foreach($t in $r.tests){
        $log=Join-Path $dir ($t.name+'.log')
        if(-not(Test-Path $log) -or [IO.File]::ReadAllText($log) -cne $t.output){$bad.Add($t.name+':log')}
        $node=@($cases|Where-Object name -eq $t.name)
        if($node.Count -ne 1 -or [bool]$node[0].failure -eq [bool]$t.passed){$bad.Add($t.name+':xml')}
        $line="$($t.name) : $($t.passed) (exit=$($t.exitCode) expected=$($t.expected))"
        if(-not $driver.Contains($line)){$bad.Add($t.name+':driver')}
        if($kind -ne 'red' -and (($t.expected -eq 'PASS' -and $t.exitCode -ne 0) -or ($t.expected -ne 'PASS' -and ($t.exitCode -eq 0 -or -not $t.output.Contains($t.expected))))){$bad.Add($t.name+':oracle')}
    }
    Check "$kind-every-log-xml-driver" ($bad.Count -eq 0) (@($bad)-join ',')
    if($kind -in @('ps7','ps51')){
        Check "$kind-frozen-driver-inputs" ($r.validatorSha256 -eq $verification.rawInputs[0].sha256 -and $r.helperSha256 -eq $verification.rawInputs[1].sha256 -and $r.driverSha256 -eq $verification.rawInputs[2].sha256) 'validator/helper/driver raw bytes match frozen SHA'
    }
    if($kind -eq 'red'){
        Check 'red-old-helper-bytes' ($r.helperSha256 -eq (BlobHash ($verification.red.helperParent+':RobWork/scripts/industrialrobot/reuse-contract.ps1') -WindowsCheckout)) "BOM/CRLF checkout of old helper: $($r.helperSha256)"
        Check 'red-driver-bytes' ($r.driverSha256 -eq (Get-FileHash (Join-Path $dir 'driver.ps1')).Hash) $r.driverSha256
    }
    if($kind -eq 'intermediate-green'){
        Check 'intermediate-helper-is-distinct' ($r.helperSha256 -ne $verification.rawInputs[1].sha256) $r.helperSha256
    }
}
$paths=@(& git -C $root diff --name-only eddb82a8e29f91660b048a69f4ec90c3d0a0e63a HEAD)
$task=Get-Content (Join-Path $root 'RobWork/doc/industrial-robot-design/tasks/REUSE-T01.json') -Raw|ConvertFrom-Json
$badPaths=@($paths|Where-Object {$_ -notin $task.allowedFiles})
Check 'changed-340-exact-allowed' ($paths.Count -eq 340 -and $badPaths.Count -eq 0) "changed=$($paths.Count) outOfScope=$($badPaths.Count)"
$unexpected=@($paths|Where-Object {$_ -notmatch '^RobWork/doc/industrial-robot-design/' -and $_ -notin @('RobWork/scripts/industrialrobot/reuse-contract.ps1','RobWork/scripts/industrialrobot/test-reuse-governance.ps1')})
Check 'no-production-framework-vcpkg' ($unexpected.Count -eq 0) ($unexpected-join ',')
$matrix=Get-Content (Join-Path $root 'RobWork/doc/industrial-robot-design/reuse/functions/foundation-governance.json') -Raw|ConvertFrom-Json
Check 'no-real-script-execution-rows' (@($matrix.functions|Where-Object execution).Count -eq 0) "existing governance functions=$($matrix.functions.Count)"
$result=[ordered]@{reviewedCommit='d52fd06aafcb025a7d494d59c6d44bc9c16f3643';checks=$checks;passed=@($checks|Where-Object passed).Count;failed=@($checks|Where-Object {-not $_.passed}).Count}
$result|ConvertTo-Json -Depth 12|Set-Content (Join-Path $out 'original-evidence-audit.json') -Encoding utf8
if($result.failed){exit 1}
