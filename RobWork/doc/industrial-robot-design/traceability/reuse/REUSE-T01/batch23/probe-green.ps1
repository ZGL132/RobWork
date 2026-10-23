$ErrorActionPreference='Stop'
$b23Root=(Get-Location).Path
& 'D:/software/Microsoft Visual Studio/2022/Community/Common7/Tools/Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64 -SkipAutomaticLocation *> .cache/b23-vs.log
$env:PATH='D:/10_Source_Repos/21_robot/RobWork/build/RobWork/bin/Release;D:/10_Source_Repos/21_robot/RobWork/vcpkg/installed/x64-windows/bin;'+$env:PATH
$b23Out=Join-Path $b23Root '.cache/b23-probe-green'
[IO.Directory]::CreateDirectory($b23Out) | Out-Null
$b23Script=Join-Path $b23Root 'RobWork/scripts/industrialrobot/run-flake-probe.ps1'
$b23Exe=Join-Path $b23Root '.cache/b22-execution/Release/ird_execution_boundary_audit.exe'
Push-Location $b23Out
try {
    foreach($b23Case in @(@{name='zero-tests';filter='ReuseNoSuchTestSuite.*'},@{name='one-real-test';filter='RunRegistryAdmissionTest.RegisterRejectsCallerContractViolations'})) {
        $b23CaseOut=Join-Path $b23Out $b23Case.name
        & pwsh -NoProfile -File $b23Script -TestExe $b23Exe -Repeat 1 -OutDir $b23CaseOut -GtestFilter $b23Case.filter *> (Join-Path $b23Out ($b23Case.name+'.log'))
        [IO.File]::WriteAllText((Join-Path $b23Out ($b23Case.name+'.exit.txt')),[string]$LASTEXITCODE)
    }
} finally {Pop-Location}
Get-Content (Join-Path $b23Out 'zero-tests.log') -Tail 4
Get-Content (Join-Path $b23Out 'one-real-test.log') -Tail 4
