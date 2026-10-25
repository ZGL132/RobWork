# REUSE-T21 第1轮独立验收：仅重跑C4实际消费者与相关契约（过滤器与实施方run-affected-tests.ps1一致），
# 输出落验收目录，不触碰实施方证据原件；新增集成core全量条目。
param([Parameter(Mandatory=$true)][string]$RepoRoot)
$ErrorActionPreference = 'Stop'
$evidence = Join-Path $RepoRoot 'RobWork/doc/industrial-robot-design/traceability/acceptance/REUSE-T21-20261025-r1'
$env:QT_QPA_PLATFORM = 'windows'
$env:PATH = "D:\software\Miniconda3;D:\software\Qt\6.11.1\msvc2022_64\bin;$RepoRoot\vcpkg\installed\x64-windows\bin;$RepoRoot\build\RobWork\bin\Release;" + $env:PATH
$cases = @(
    @('acc-core', 'build/RobWorkStudio/bin/Release/sdurws_ird_core_test.exe', '*'),
    @('smoke-core', 'build/govt24-smoke/core/Release/sdurws_ird_core_test.exe', '*'),
    @('testkit', 'build/RobWorkStudio/bin/Release/sdurws_ird_testkit_test.exe', 'AllCloseWithin.*:AssertMacrosBehavior.*:CheckAtMost.*:CheckClose.*:CheckIdentical.*:Set*.*:StableOrder.*:ReproBenchmark.*'),
    @('trajectory', 'build/RobWorkStudio/bin/Release/sdurws_ird_trajectory_test.exe', 'TimeParam*.*:TrjContinuity.*'),
    @('trajectory-contract', 'build/RobWorkStudio/bin/Release/sdurws_ird_trajectory_contract_test.exe', 'TrjRecheckContract.*:TrjContinuousProcessContract.*:TrjAt06GoldenContract.*'),
    @('selection', 'build/RobWorkStudio/bin/Release/sdurws_ird_selection_test.exe', 'SelCombinationCheck.*:SelCombinationCheckEvaluator.*:SelScreeningMotor.*:SelScreeningGearbox.*:SelLinearScreening.*'),
    @('selection-contract', 'build/RobWorkStudio/bin/Release/sdurws_ird_selection_contract_test.exe', 'SelCombinationCheckContract.*:SelHardScreeningContract.*:SelFeasibleSetContract.*:SelGoldenDatasetContract.*'),
    @('optimization', 'build/RobWorkStudio/bin/Release/sdurws_ird_optimization_test.exe', 'OptPareto.*'),
    @('optimization-contract', 'build/RobWorkStudio/bin/Release/sdurws_ird_optimization_contract_test.exe', 'OptObjectiveParetoContract.*:OptGoldenDatasetContract.*')
)
foreach ($case in $cases) {
    $name, $relativeExe, $filter = $case
    $exe = Join-Path $RepoRoot $relativeExe
    & $exe "--gtest_filter=$filter" "--gtest_output=xml:$evidence/$name.xml" "--ird_report=$evidence/$name-report.json" *> "$evidence/$name.log"
    $testExit = $LASTEXITCODE
    Write-Output "$name exit=$testExit"
    Get-Content "$evidence/$name.log" -Tail 3
    if ($testExit -ne 0) { exit $testExit }
}
