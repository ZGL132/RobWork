param([string]$Evidence)
$ErrorActionPreference = 'Stop'
& 'D:/software/Microsoft Visual Studio/2022/Community/Common7/Tools/Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
Set-Location -LiteralPath (Join-Path $Evidence 'repo')
Get-ChildItem Env: | Where-Object { $_.Name -match '^(VSCMD|VisualStudio|QT_|QML_)' } | Format-Table -AutoSize | Out-File (Join-Path $Evidence 'environment.log')
$buildDir = Join-Path $Evidence 'build'
& cmake -S RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01 -B $buildDir -G 'Visual Studio 17 2022' -A x64 '-DRobWork_DIR=D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWork/cmake' '-DCMAKE_PREFIX_PATH=D:/10_Source_Repos/21_robot/RobWork/vcpkg/installed/x64-windows;D:/software/Qt/6.11.1/msvc2022_64' '-DRWSIM_TARGETS_DIR=D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkSim/cmake/targets' *> (Join-Path $Evidence 'configure.log')
$LASTEXITCODE | Set-Content (Join-Path $Evidence 'configure.exit.txt')
if ($LASTEXITCODE -ne 0) { throw 'configure failed' }
& cmake --build $buildDir --config Release --target ird_reuse_audit_probe *> (Join-Path $Evidence 'build.log')
$LASTEXITCODE | Set-Content (Join-Path $Evidence 'build.exit.txt')
if ($LASTEXITCODE -ne 0) { throw 'build failed' }
$env:PATH = 'D:/10_Source_Repos/21_robot/RobWork/build/RobWorkSim/bin/Release;D:/10_Source_Repos/21_robot/RobWork/build/RobWork/bin/Release;D:/10_Source_Repos/21_robot/RobWork/vcpkg/installed/x64-windows/bin;D:/software/Qt/6.11.1/msvc2022_64/bin;' + $env:PATH
$probeExe = Join-Path $buildDir 'Release/ird_reuse_audit_probe.exe'
& $probeExe '--gtest_filter=ReusePlanningAudit.*' ('--gtest_output=xml:' + (Join-Path $Evidence 'gtest.xml')) ('--ird_report=' + (Join-Path $Evidence 'ird-test-report.json')) *> (Join-Path $Evidence 'tests.log')
$LASTEXITCODE | Set-Content (Join-Path $Evidence 'tests.exit.txt')
if ($LASTEXITCODE -ne 0) { throw 'tests failed' }
Write-Output 'configure/build/4 public planning tests finished'
