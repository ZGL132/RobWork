# REUSE-T51 独立验收版测试执行器（与入库 run-tests.ps1 / run-contract.ps1
# 机制逐行同构——PATH/环境/exe/filter/XML/report 机制全同；唯一差异＝证据
# 输出指向本验收目录，隔离运行，不覆盖入库 REUSE-T51 证据原件）。
param([string]$RepoRoot,[string]$Label,[string]$Filter,[string]$Exe)
$ErrorActionPreference='Stop'
$evidence=$PSScriptRoot
$env:QT_QPA_PLATFORM='windows'
$env:PATH="D:\software\Qt\6.11.1\msvc2022_64\bin;$RepoRoot\vcpkg\installed\x64-windows\bin;$RepoRoot\build\RobWork\bin\Release;$RepoRoot\build\RobWorkStudio\bin\Release;"+$env:PATH
if (-not $Exe) { $Exe = Join-Path $RepoRoot 'build/RobWorkStudio/bin/Release/sdurws_ird_kinematics_test.exe' }
if ($Filter) { & $Exe "--gtest_filter=$Filter" "--gtest_output=xml:$evidence/$Label.xml" "--ird_report=$evidence/$Label-report.json" *> "$evidence/$Label.log" }
else { & $Exe "--gtest_output=xml:$evidence/$Label.xml" "--ird_report=$evidence/$Label-report.json" *> "$evidence/$Label.log" }
$testExit=$LASTEXITCODE
Get-Content "$evidence/$Label.log" -Tail 8
exit $testExit
