param([string]$EvidenceRoot=$PSScriptRoot)
$ErrorActionPreference='Stop'
$clone=Join-Path $EvidenceRoot 'clone'
Set-Location $clone
$head='e5c58aa0b6b3958b90e3a22ef42c36e4c9a4dc7e'
$base='88cae2102d16a338c5f0a7725d080f1c0d802f95'
$task='RobWork/doc/industrial-robot-design/tasks/REUSE-T01.json'
if((& git rev-parse HEAD)-ne$head){throw 'Frozen clone HEAD differs'}
& 'D:/software/Microsoft Visual Studio/2022/Community/Common7/Tools/Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64 -SkipAutomaticLocation *> (Join-Path $EvidenceRoot 'vs-environment.log')
& cmake -S RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01/batch33 -B (Join-Path $EvidenceRoot 'build') -G 'Visual Studio 17 2022' -A x64 '-DRobWork_DIR=D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWork/cmake' '-DRWSIM_TARGETS_DIR=D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkSim/cmake/targets' '-DCMAKE_PREFIX_PATH=D:/10_Source_Repos/21_robot/RobWork/vcpkg/installed/x64-windows;D:/software/Qt/6.11.1/msvc2022_64' *> (Join-Path $EvidenceRoot 'configure.log')
$LASTEXITCODE|Set-Content (Join-Path $EvidenceRoot 'configure.exit.txt')
if($LASTEXITCODE-ne0){throw 'Configure failed'}
& cmake --build (Join-Path $EvidenceRoot 'build') --config Release --target ird_diagnostic_schema_audit *> (Join-Path $EvidenceRoot 'build.log')
$LASTEXITCODE|Set-Content (Join-Path $EvidenceRoot 'build.exit.txt')
if($LASTEXITCODE-ne0){throw 'Build failed'}
$env:PATH='D:/10_Source_Repos/21_robot/RobWork/build/RobWork/bin/Release;D:/10_Source_Repos/21_robot/RobWork/build/RobWork/libs/Release;D:/10_Source_Repos/21_robot/RobWork/vcpkg/installed/x64-windows/bin;'+$env:PATH
$env:QT_QPA_PLATFORM='windows'
New-Item -ItemType Directory -Force (Join-Path $EvidenceRoot 'run')|Out-Null
& (Join-Path $EvidenceRoot 'build/Release/ird_diagnostic_schema_audit.exe') ('--gtest_output=xml:'+(Join-Path $EvidenceRoot 'run/gtest.xml')) ('--ird_report='+(Join-Path $EvidenceRoot 'run/ird-test-report.json')) *> (Join-Path $EvidenceRoot 'run/stdout.log')
$LASTEXITCODE|Set-Content (Join-Path $EvidenceRoot 'run/exit.txt')
& pwsh -NoProfile -File RobWork/scripts/industrialrobot/validate-task.ps1 -TaskFile $task *> (Join-Path $EvidenceRoot 'task.log')
$LASTEXITCODE|Set-Content (Join-Path $EvidenceRoot 'task.exit.txt')
& pwsh -NoProfile -File RobWork/scripts/industrialrobot/validate-docs.ps1 *> (Join-Path $EvidenceRoot 'docs.log')
$LASTEXITCODE|Set-Content (Join-Path $EvidenceRoot 'docs.exit.txt')
& pwsh -NoProfile -File RobWork/scripts/industrialrobot/validate-reuse.ps1 -TaskFile $task -Head $head *> (Join-Path $EvidenceRoot 'reuse.log')
$LASTEXITCODE|Set-Content (Join-Path $EvidenceRoot 'reuse.exit.txt')
& pwsh -NoProfile -File RobWork/scripts/industrialrobot/verify-task-scope.ps1 -TaskFile $task -Base $base -Head $head *> (Join-Path $EvidenceRoot 'scope.log')
$LASTEXITCODE|Set-Content (Join-Path $EvidenceRoot 'scope.exit.txt')
& pwsh -NoProfile -File RobWork/scripts/industrialrobot/commit-lint.ps1 -Range ($base+'..'+$head) *> (Join-Path $EvidenceRoot 'commit-lint.log')
$LASTEXITCODE|Set-Content (Join-Path $EvidenceRoot 'commit-lint.exit.txt')
& git -c core.whitespace=cr-at-eol diff --check $base $head *> (Join-Path $EvidenceRoot 'diff-check-cr-at-eol.log')
$LASTEXITCODE|Set-Content (Join-Path $EvidenceRoot 'diff-check-cr-at-eol.exit.txt')
& pwsh -NoProfile -File (Join-Path $EvidenceRoot 'verify-bindings.ps1')
