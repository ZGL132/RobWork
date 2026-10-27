# REUSE-T51 kinematics 测试执行器（REUSE-T36 run-tests.ps1 同款机制移植）：
# 跑构建好的 sdurws_ird_kinematics_test.exe，产出 gtest XML＋per-exe
# ird-test-report.json＋完整日志到本证据目录，退出码透传测试结果。
param([string]$RepoRoot,[string]$Label,[string]$Filter)
$ErrorActionPreference='Stop'
$evidence=Join-Path $RepoRoot 'RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T51'
$env:QT_QPA_PLATFORM='windows'
$env:PATH="D:\software\Qt\6.11.1\msvc2022_64\bin;$RepoRoot\vcpkg\installed\x64-windows\bin;$RepoRoot\build\RobWork\bin\Release;$RepoRoot\build\RobWorkStudio\bin\Release;"+$env:PATH
$exe=Join-Path $RepoRoot 'build/RobWorkStudio/bin/Release/sdurws_ird_kinematics_test.exe'
& $exe "--gtest_filter=$Filter" "--gtest_output=xml:$evidence/$Label.xml" "--ird_report=$evidence/$Label-report.json" *> "$evidence/$Label.log"
$testExit=$LASTEXITCODE
Get-Content "$evidence/$Label.log" -Tail 8
exit $testExit
