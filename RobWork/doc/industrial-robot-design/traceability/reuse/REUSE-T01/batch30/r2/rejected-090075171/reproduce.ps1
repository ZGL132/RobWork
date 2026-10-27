param([string]$AuditRoot)
$ErrorActionPreference='Stop'
Set-Location (Join-Path $AuditRoot 'clone')
$out=Join-Path $AuditRoot 'evidence'
& 'D:/software/Microsoft Visual Studio/2022/Community/Common7/Tools/Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64 -SkipAutomaticLocation *> (Join-Path $out 'vs.log')
cmake -S RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01/batch30 -B (Join-Path $AuditRoot 'build') -G 'Visual Studio 17 2022' -A x64 '-DRobWork_DIR=D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWork/cmake' '-DRWSIM_TARGETS_DIR=D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkSim/cmake/targets' '-DCMAKE_PREFIX_PATH=D:/10_Source_Repos/21_robot/RobWork/vcpkg/installed/x64-windows;D:/software/Qt/6.11.1/msvc2022_64' *> (Join-Path $out 'configure.log')
$LASTEXITCODE | Set-Content (Join-Path $out 'configure.exit.txt')
if($LASTEXITCODE -ne 0){exit 2}
cmake --build (Join-Path $AuditRoot 'build') --config Release --target ird_digest_audit *> (Join-Path $out 'build.log')
$LASTEXITCODE | Set-Content (Join-Path $out 'build.exit.txt')
if($LASTEXITCODE -ne 0){exit 3}
$env:PATH='D:/10_Source_Repos/21_robot/RobWork/build/RobWork/bin/Release;D:/10_Source_Repos/21_robot/RobWork/build/RobWork/libs/Release;D:/10_Source_Repos/21_robot/RobWork/vcpkg/installed/x64-windows/bin;'+$env:PATH
$exe=Join-Path $AuditRoot 'build/Release/ird_digest_audit.exe'
& $exe ('--gtest_output=xml:'+(Join-Path $out 'gtest.xml')) ('--ird_report='+(Join-Path $out 'ird-test-report.json')) *> (Join-Path $out 'test.log')
$LASTEXITCODE | Set-Content (Join-Path $out 'test.exit.txt')
