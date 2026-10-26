# GOV-T27 kinematics 全量测试执行器（REUSE-T51 run-tests.ps1 同款机制）：
# 跑构建好的 sdurws_ird_kinematics_test.exe，产出 gtest XML＋per-exe
# ird-test-report.json＋完整日志到本证据目录，退出码透传测试结果。
# 注意：--gtest_filter 仅在 Filter 非空时传（空串显式传参被 gtest 判"零匹配"）。
param([string]$RepoRoot,[string]$Label,[string]$Filter)
$ErrorActionPreference='Stop'
$evidence=Join-Path $RepoRoot 'RobWork/doc/industrial-robot-design/traceability/reuse/GOV-T27'
$env:QT_QPA_PLATFORM='windows'
$env:PATH="D:\software\Qt\6.11.1\msvc2022_64\bin;$RepoRoot\vcpkg\installed\x64-windows\bin;$RepoRoot\build\RobWork\bin\Release;$RepoRoot\build\RobWorkStudio\bin\Release;"+$env:PATH
$exe=Join-Path $RepoRoot 'build/RobWorkStudio/bin/Release/sdurws_ird_kinematics_test.exe'
$args=@()
if ($Filter) { $args+=("--gtest_filter="+$Filter) }
$args+=("--gtest_output=xml:"+$evidence+"/"+$Label+".xml")
$args+=("--ird_report="+$evidence+"/"+$Label+"-report.json")
& $exe @args *> "$evidence/$Label.log"
$testExit=$LASTEXITCODE
Get-Content "$evidence/$Label.log" -Tail 8
exit $testExit
