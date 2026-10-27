$ErrorActionPreference = 'Stop'
$reviewRoot = 'C:/Users/zgl18/AppData/Local/Temp/REUSE-T01-B17-independent-7eb99134'
& 'D:/software/Microsoft Visual Studio/2022/Community/Common7/Tools/Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64 -SkipAutomaticLocation *> (Join-Path $reviewRoot 'vs.log')
@{ VSCMD_ARG_TGT_ARCH=$env:VSCMD_ARG_TGT_ARCH; VSCMD_ARG_HOST_ARCH=$env:VSCMD_ARG_HOST_ARCH; VCINSTALLDIR=$env:VCINSTALLDIR; compiler=(Get-Command cl.exe).Source; cmake=(Get-Command cmake.exe).Source } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $reviewRoot 'vs-environment.json') -Encoding utf8
$sourcePath = Join-Path $reviewRoot 'repo/RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01'
$buildPath = Join-Path $reviewRoot 'build'
cmake -S $sourcePath -B $buildPath -G 'Visual Studio 17 2022' -A x64 '-DRobWork_DIR=D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWork/cmake' '-DCMAKE_PREFIX_PATH=D:/10_Source_Repos/21_robot/RobWork/vcpkg/installed/x64-windows;D:/software/Qt/6.11.1/msvc2022_64' '-DRWSIM_TARGETS_DIR=D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkSim/cmake/targets' *> (Join-Path $reviewRoot 'configure.log')
$LASTEXITCODE | Set-Content -LiteralPath (Join-Path $reviewRoot 'configure.exit.txt')
if ($LASTEXITCODE -ne 0) { throw 'configure failed; see configure.log' }
cmake --build $buildPath --config Release --target ird_reuse_audit_probe *> (Join-Path $reviewRoot 'build.log')
$LASTEXITCODE | Set-Content -LiteralPath (Join-Path $reviewRoot 'build.exit.txt')
if ($LASTEXITCODE -ne 0) { throw 'build failed; see build.log' }
'configure/build exit0'
