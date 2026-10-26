# REUSE-T52 独立冒烟测试执行器（冒烟 exe 与框架 DLL 分离目录——运行期
# PATH 补框架/RobWorkSim/vcpkg bin，见 DTB §5.1 DLL 处置口径；GOV-T27
# run-tests.ps1 同款产出三元组）。
param([string]$RepoRoot,[string]$Label,[string]$Filter)
$ErrorActionPreference='Stop'
$evidence=Join-Path $RepoRoot 'RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T52/smoke'
$env:QT_QPA_PLATFORM='windows'
$env:PATH="D:\software\Qt\6.11.1\msvc2022_64\bin;$RepoRoot\vcpkg\installed\x64-windows\bin;$RepoRoot\build\RobWork\bin\Release;$RepoRoot\build\RobWorkSim\bin\Release;"+$env:PATH
$exe=Join-Path $RepoRoot 'build/wp07-smoke/dynamics/Release/sdurws_ird_dynamics_test.exe'
$args=@()
if ($Filter) { $args+=("--gtest_filter="+$Filter) }
$args+=("--gtest_output=xml:"+$evidence+"/"+$Label+".xml")
$args+=("--ird_report="+$evidence+"/"+$Label+"-report.json")
& $exe @args *> "$evidence/$Label.log"
$testExit=$LASTEXITCODE
Get-Content "$evidence/$Label.log" -Tail 8
exit $testExit
