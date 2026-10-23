param([string]$EvidenceRoot)
$ErrorActionPreference='Stop'
$repo=Join-Path $EvidenceRoot 'repo'
& 'D:/software/Microsoft Visual Studio/2022/Community/Common7/Tools/Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64 -SkipAutomaticLocation *> (Join-Path $EvidenceRoot 'vs.log')
$env:QT_QPA_PLATFORM='windows'
$env:PATH='D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWork/libs/release;D:/10_Source_Repos/21_robot/RobWork/vcpkg/installed/x64-windows/bin;D:/software/Qt/6.11.1/msvc2022_64/bin;'+$env:PATH
Get-ChildItem Env: | Where-Object Name -Match '^(QT_|QML_|VSCMD_ARG_|VCTools)' | ConvertTo-Json > (Join-Path $EvidenceRoot 'environment.json')
$source=Join-Path $repo 'RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01/probe-r18'
$build=Join-Path $EvidenceRoot 'fresh-build'
& cmake -S $source -B $build -G 'Visual Studio 17 2022' -A x64 '-DRobWork_DIR=D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWork/cmake' '-DCMAKE_PREFIX_PATH=D:/10_Source_Repos/21_robot/RobWork/vcpkg/installed/x64-windows;D:/software/Qt/6.11.1/msvc2022_64' *> (Join-Path $EvidenceRoot 'configure.log')
$code=$LASTEXITCODE; Set-Content (Join-Path $EvidenceRoot 'configure.exit.txt') $code; if($code -ne 0){exit $code}
& cmake --build $build --config Release --target ird_policy_boundary_audit *> (Join-Path $EvidenceRoot 'build.log')
$code=$LASTEXITCODE; Set-Content (Join-Path $EvidenceRoot 'build.exit.txt') $code; if($code -ne 0){exit $code}
$exe=Join-Path $build 'Release/ird_policy_boundary_audit.exe'
Get-FileHash -Algorithm SHA256 $exe | ConvertTo-Json > (Join-Path $EvidenceRoot 'executable.json')
& $exe '--gtest_filter=ReusePolicyBoundary.*' "--gtest_output=xml:$(Join-Path $EvidenceRoot 'gtest.xml')" "--ird_report=$(Join-Path $EvidenceRoot 'ird-test-report.json')" *> (Join-Path $EvidenceRoot 'stdout.log')
$code=$LASTEXITCODE; Set-Content (Join-Path $EvidenceRoot 'run.exit.txt') $code; Write-Output "run exit=$code"
