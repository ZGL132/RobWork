param([string]$EvidenceRoot)
$ErrorActionPreference='Stop'
& 'D:/software/Microsoft Visual Studio/2022/Community/Common7/Tools/Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64 -SkipAutomaticLocation *> (Join-Path $EvidenceRoot 'rerun-vs.log')
$env:QT_QPA_PLATFORM='windows'
$env:PATH='D:/10_Source_Repos/21_robot/RobWork/build/RobWork/bin/Release;D:/10_Source_Repos/21_robot/RobWork/vcpkg/installed/x64-windows/bin;D:/software/Qt/6.11.1/msvc2022_64/bin;'+$env:SDURWS_IRD_PYTHON_DIR+';'+$env:PATH
@{QT_QPA_PLATFORM=$env:QT_QPA_PLATFORM;VSCMD_ARG_TGT_ARCH=$env:VSCMD_ARG_TGT_ARCH;PATH=$env:PATH;PythonDir=$env:SDURWS_IRD_PYTHON_DIR}|ConvertTo-Json > (Join-Path $EvidenceRoot 'run-environment.json')
$exe=Join-Path $EvidenceRoot 'fresh-build/Release/ird_policy_boundary_audit.exe'
& $exe '--gtest_filter=ReusePolicyBoundary.*' "--gtest_output=xml:$(Join-Path $EvidenceRoot 'gtest.xml')" "--ird_report=$(Join-Path $EvidenceRoot 'ird-test-report.json')" *> (Join-Path $EvidenceRoot 'stdout.log')
$code=$LASTEXITCODE; Set-Content (Join-Path $EvidenceRoot 'run.exit.txt') $code
Get-Content (Join-Path $EvidenceRoot 'stdout.log'); Write-Output "exit=$code"
