# REUSE-T52 dynamics 契约测试执行器（GOV-T27 run-contract.ps1 同款机制）。
param([string]$RepoRoot,[string]$Label)
$ErrorActionPreference='Stop'
$evidence=Join-Path $RepoRoot 'RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T52'
$env:QT_QPA_PLATFORM='windows'
$env:PATH="D:\software\Qt\6.11.1\msvc2022_64\bin;$RepoRoot\vcpkg\installed\x64-windows\bin;$RepoRoot\build\RobWork\bin\Release;$RepoRoot\build\RobWorkSim\bin\Release;$RepoRoot\build\RobWorkStudio\bin\Release;"+$env:PATH
$exe=Join-Path $RepoRoot 'build/RobWorkStudio/bin/Release/sdurws_ird_dynamics_contract_test.exe'
$args=@("--gtest_output=xml:"+$evidence+"/"+$Label+".xml")
$args+=("--ird_report="+$evidence+"/"+$Label+"-report.json")
& $exe @args *> "$evidence/$Label.log"
$testExit=$LASTEXITCODE
Get-Content "$evidence/$Label.log" -Tail 8
exit $testExit
