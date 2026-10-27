$ErrorActionPreference = 'Stop'
$reviewRoot = 'C:/Users/zgl18/AppData/Local/Temp/REUSE-T01-B17-independent-7eb99134'
& 'D:/software/Microsoft Visual Studio/2022/Community/Common7/Tools/Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64 -SkipAutomaticLocation *> (Join-Path $reviewRoot 'vs-run.log')
$env:PATH = 'D:/10_Source_Repos/21_robot/RobWork/build/RobWorkSim/bin/Release;D:/10_Source_Repos/21_robot/RobWork/build/RobWork/bin/Release;D:/10_Source_Repos/21_robot/RobWork/vcpkg/installed/x64-windows/bin;D:/software/Qt/6.11.1/msvc2022_64/bin;' + $env:PATH
$env:QT_QPA_PLATFORM = 'windows'
$probePath = Join-Path $reviewRoot 'build/Release/ird_reuse_audit_probe.exe'
foreach ($run in @(@{name='candidates';filter='ReuseAuditMatching.*'}, @{name='core-red';filter='ReuseAuditCoreBoundary.*'})) {
    $runPath = Join-Path $reviewRoot $run.name
    New-Item -ItemType Directory -Path $runPath | Out-Null
    & $probePath ('--gtest_filter=' + $run.filter) ('--gtest_output=xml:' + (Join-Path $runPath 'gtest.xml')) ('--ird_report=' + (Join-Path $runPath 'ird-test-report.json')) *> (Join-Path $runPath 'stdout.log')
    $LASTEXITCODE | Set-Content -LiteralPath (Join-Path $runPath 'exit.txt')
    $run.name + ' exit=' + $LASTEXITCODE
}
@{VSCMD_ARG_TGT_ARCH=$env:VSCMD_ARG_TGT_ARCH;QT_QPA_PLATFORM=$env:QT_QPA_PLATFORM;executable=$probePath;executableSha256=(Get-FileHash -LiteralPath $probePath -Algorithm SHA256).Hash.ToLowerInvariant()} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $reviewRoot 'run-environment.json') -Encoding utf8
