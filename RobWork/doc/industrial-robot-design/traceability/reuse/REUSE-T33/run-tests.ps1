param([string]$RepoRoot,[string]$Label,[string]$Filter,[string]$ExeName)
$ErrorActionPreference='Stop'
$evidence=Join-Path $RepoRoot 'RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T33'
$env:QT_QPA_PLATFORM='windows'
$env:PATH="D:\software\Miniconda3;D:\software\Qt\6.11.1\msvc2022_64\bin;$RepoRoot\vcpkg\installed\x64-windows\bin;$RepoRoot\build\RobWork\bin\Release;"+$env:PATH
if (-not $ExeName) { $ExeName='sdurws_ird_modeling_test.exe' }
$exe=Join-Path $RepoRoot ("build/RobWorkStudio/bin/Release/" + $ExeName)
$argList=@("--gtest_output=xml:$evidence/$Label.xml","--ird_report=$evidence/$Label-report.json")
if ($Filter) { $argList+=("--gtest_filter=$Filter") }
& $exe @argList *> "$evidence/$Label.log"
$testExit=$LASTEXITCODE
Get-Content "$evidence/$Label.log" -Tail 8
exit $testExit
