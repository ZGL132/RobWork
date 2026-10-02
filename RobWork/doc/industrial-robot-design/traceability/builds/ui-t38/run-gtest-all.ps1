# UI-T38 留痕 gtest 全量驱动（30 个 ird 测试可执行逐个运行＋JUnit XML 落盘＋汇总）。
# PATH 配方＝F-455 口径（主树各 bin＋vcpkg＋Qt＋Miniconda）。
$ErrorActionPreference = 'Continue'
$ROOT = 'D:\10_Source_Repos\21_robot\RobWork'
$BLD = "$ROOT\build"
$OUT = "$ROOT\RobWork\doc\industrial-robot-design\traceability\builds\ui-t38\gtest"
$CWD = "$ROOT\build\ui-t38-gtest-cwd"
New-Item -ItemType Directory -Force -Path $OUT, $CWD | Out-Null

# F-455 口径：PATH 需求清单（gui_test 需平台插件与 Qt DLL）。
$env:PATH = "$BLD\RobWork\bin\Release;$BLD\RobWorkSim\bin\Release;$BLD\RobWorkStudio\bin\Release;$BLD\RobWorkStudio\libs\Release;$ROOT\vcpkg\installed\x64-windows\bin;D:\software\Qt\6.11.1\msvc2022_64\bin;D:\software\Miniconda3;$env:PATH"

$exes = Get-ChildItem "$BLD\RobWorkStudio\bin\Release\sdurws_ird_*test*.exe" | Sort-Object Name
$totalPass = 0; $totalFail = 0; $failedExes = @()
foreach ($e in $exes) {
    $name = $e.BaseName
    $xml = "$OUT\$name.xml"
    $log = "$OUT\$name.log"
    Push-Location $CWD
    $p = Start-Process -FilePath $e.FullName -WorkingDirectory $CWD `
        -ArgumentList "--gtest_output=xml:$xml" `
        -RedirectStandardOutput "$CWD\out.tmp" -RedirectStandardError "$CWD\err.tmp" `
        -PassThru -Wait
    Pop-Location
    Copy-Item "$CWD\out.tmp" $log -Force -ErrorAction SilentlyContinue
    Add-Content $log (Get-Content "$CWD\err.tmp" -Raw -ErrorAction SilentlyContinue)
    # 从 XML 汇总（无 XML＝非 gtest 工具类，按退出码计）。
    if (Test-Path $xml) {
        [xml]$x = Get-Content $xml -Encoding UTF8
        $s = $x.testsuites | ForEach-Object { $_.testsuites } 
        $t = ([int]$x.testsuites.tests); $f = ([int]$x.testsuites.failures)
        if ($null -eq $t) { $t = 0; $f = 0; foreach ($ts in $x.testsuites.testsuite) { $t += [int]$ts.tests; $f += [int]$ts.failures } }
        Write-Host ("{0}: tests={1} failures={2} exit={3}" -f $name, $t, $f, $p.ExitCode)
        if ($f -gt 0 -or $p.ExitCode -ne 0) { $failedExes += $name } else { $totalPass += $t }
    } else {
        Write-Host ("{0}: (no xml) exit={1}" -f $name, $p.ExitCode)
        if ($p.ExitCode -ne 0) { $failedExes += $name }
    }
}
Remove-Item "$CWD\out.tmp", "$CWD\err.tmp" -Force -ErrorAction SilentlyContinue
Write-Output ("GTEST_TOTAL_PASSED_CASES=" + $totalPass)
if ($failedExes.Count -eq 0) { Write-Output 'GTEST_ALL_PASS' } else { Write-Output ("GTEST_FAILED_EXES=" + ($failedExes -join ',')) }
