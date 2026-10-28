param([string]$Name,[string]$Filter='*',[string]$Tag='run',[int]$TimeoutMs=180000,[string]$Exe='')
$repo='C:\Users\zgl18\.codex\worktrees\core-review-fixes\RobWork'
$out=Join-Path $repo 'build\fix-execution-20261028'
if(-not $Exe){$Exe=Join-Path $repo ('build\RobWorkStudio\bin\Release\'+$Name+'.exe')}
$log=Join-Path $out ($Tag+'.log');$err=Join-Path $out ($Tag+'.stderr.log')
$xml=Join-Path $out ($Tag+'.xml');$json=Join-Path $out ($Tag+'-ird-test-report.json')
$p=Start-Process -FilePath $Exe -ArgumentList @("--gtest_filter=$Filter","--gtest_output=xml:$xml","--ird_report=$json") -WorkingDirectory (Split-Path $Exe) -PassThru -WindowStyle Hidden -RedirectStandardOutput $log -RedirectStandardError $err
"$Tag pid=$($p.Id) exe=$Exe filter=$Filter" | Add-Content (Join-Path $out 'test-processes.log')
if(-not $p.WaitForExit($TimeoutMs)){taskkill /PID $p.Id /T /F | Add-Content $err;throw '本轮测试有界超时'}
$p.Refresh(); "nativeExit=$($p.ExitCode)" | Add-Content $log
Get-Content $log -Tail 12
exit $p.ExitCode
