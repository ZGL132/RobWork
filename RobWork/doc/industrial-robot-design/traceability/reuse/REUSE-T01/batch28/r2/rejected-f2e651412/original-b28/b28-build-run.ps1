$ErrorActionPreference='Stop'
& 'D:/software/Microsoft Visual Studio/2022/Community/Common7/Tools/Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64 -SkipAutomaticLocation *> .cache/b28-vs.log
cmake --build .cache/t01-probe-export --config Release --target ird_reuse_audit_probe *> .cache/b28-build.log
if($LASTEXITCODE){ Get-Content .cache/b28-build.log -Tail 15; exit $LASTEXITCODE }
$env:PATH='D:/10_Source_Repos/21_robot/RobWork/build/RobWork/bin/Release;D:/10_Source_Repos/21_robot/RobWork/vcpkg/installed/x64-windows/bin;'+$env:PATH
$b28Out=Join-Path $PWD '.cache/b28-run'
[IO.Directory]::CreateDirectory($b28Out) | Out-Null
& 'C:/Users/zgl18/.codex/worktrees/reuse-audit/RobWork/.cache/t01-probe-export/Release/ird_reuse_audit_probe.exe' '--gtest_filter=ReuseCoreAudit.*' "--gtest_output=xml:$b28Out/gtest.xml" "--ird_report=$b28Out/ird-test-report.json" *> "$b28Out/stdout.log"
$b28Exit=$LASTEXITCODE
[IO.File]::WriteAllText("$b28Out/exit.txt",[string]$b28Exit)
Get-Content "$b28Out/stdout.log" -Tail 18
exit $b28Exit
