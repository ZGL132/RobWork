$ErrorActionPreference='Stop'
$accRoot='C:/Users/zgl18/AppData/Local/Temp/REUSE-T01-b31-accept-df453223'
$repo=Join-Path $accRoot 'clone2'
Import-Module 'D:/software/Microsoft Visual Studio/2022/Community/Common7/Tools/Microsoft.VisualStudio.DevShell.dll'
Enter-VsDevShell -VsInstallPath 'D:/software/Microsoft Visual Studio/2022/Community' -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' *> (Join-Path $accRoot 'vs.log')
Set-Location $repo
cmake -S RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01/batch31 -B (Join-Path $accRoot 'build') -G 'Visual Studio 17 2022' -A x64 '-DRobWork_DIR=D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWork/cmake' '-DRWSIM_TARGETS_DIR=D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkSim/cmake/targets' '-DCMAKE_PREFIX_PATH=D:/10_Source_Repos/21_robot/RobWork/vcpkg/installed/x64-windows;D:/software/Qt/6.11.1/msvc2022_64' *> (Join-Path $accRoot 'configure.log')
$LASTEXITCODE | Set-Content (Join-Path $accRoot 'configure.exit.txt')
if($LASTEXITCODE -ne 0) { throw 'configure failed' }
cmake --build (Join-Path $accRoot 'build') --config Release --target ird_units_audit *> (Join-Path $accRoot 'build.log')
$LASTEXITCODE | Set-Content (Join-Path $accRoot 'build.exit.txt')
if($LASTEXITCODE -ne 0) { throw 'build failed' }
$env:PATH='D:/10_Source_Repos/21_robot/RobWork/build/RobWork/bin/Release;D:/10_Source_Repos/21_robot/RobWork/build/RobWork/libs/Release;D:/10_Source_Repos/21_robot/RobWork/vcpkg/installed/x64-windows/bin;'+$env:PATH
New-Item -ItemType Directory -Path (Join-Path $accRoot 'run') -Force | Out-Null
& (Join-Path $accRoot 'build/Release/ird_units_audit.exe') ('--gtest_output=xml:'+ (Join-Path $accRoot 'run/gtest.xml')) ('--ird_report='+ (Join-Path $accRoot 'run/ird-test-report.json')) *> (Join-Path $accRoot 'run/stdout.log')
$LASTEXITCODE | Set-Content (Join-Path $accRoot 'run/exit.txt')
