$ErrorActionPreference='Stop'
$reviewRoot='C:/Users/zgl18/AppData/Local/Temp/REUSE-T01-B20-accept-c9879b77baeb4cb884d2c0e3f1091277'
$repo=Join-Path $reviewRoot 'repo-isolated'
& 'D:/software/Microsoft Visual Studio/2022/Community/Common7/Tools/Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64 -SkipAutomaticLocation *> (Join-Path $reviewRoot 'vs.log')
@{VSCMD_ARG_TGT_ARCH=$env:VSCMD_ARG_TGT_ARCH;VSCMD_ARG_HOST_ARCH=$env:VSCMD_ARG_HOST_ARCH;VisualStudioVersion=$env:VisualStudioVersion;VSINSTALLDIR=$env:VSINSTALLDIR} | ConvertTo-Json | Set-Content (Join-Path $reviewRoot 'vs-environment.json')
$build=Join-Path $reviewRoot 'build'
& cmake -S (Join-Path $repo 'RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01/probe-r20') -B $build -G 'Visual Studio 17 2022' -A x64 '-DRobWork_DIR=D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWork/cmake' '-DCMAKE_PREFIX_PATH=D:/10_Source_Repos/21_robot/RobWork/vcpkg/installed/x64-windows;D:/software/Qt/6.11.1/msvc2022_64' *> (Join-Path $reviewRoot 'configure.log')
$configureExit=$LASTEXITCODE
Set-Content (Join-Path $reviewRoot 'configure.exit.txt') $configureExit
if($configureExit -ne 0){exit $configureExit}
& cmake --build $build --config Release --target ird_project_boundary_audit *> (Join-Path $reviewRoot 'build.log')
$buildExit=$LASTEXITCODE
Set-Content (Join-Path $reviewRoot 'build.exit.txt') $buildExit
if($buildExit -ne 0){exit $buildExit}
$env:PATH='D:/10_Source_Repos/21_robot/RobWork/build/RobWork/bin/Release;D:/10_Source_Repos/21_robot/RobWork/vcpkg/installed/x64-windows/bin;' + $env:PATH
$exe=Join-Path $build 'Release/ird_project_boundary_audit.exe'
Get-FileHash -Algorithm SHA256 $exe | Select-Object Path,Hash | ConvertTo-Json | Set-Content (Join-Path $reviewRoot 'executable.json')
& $exe '--gtest_filter=ReuseProjectBoundary.*' ('--gtest_output=xml:' + (Join-Path $reviewRoot 'gtest.xml')) ('--ird_report=' + (Join-Path $reviewRoot 'ird-test-report.json')) *> (Join-Path $reviewRoot 'stdout.log')
$runExit=$LASTEXITCODE
Set-Content (Join-Path $reviewRoot 'run.exit.txt') $runExit
Write-Output "configure=$configureExit build=$buildExit run=$runExit"
