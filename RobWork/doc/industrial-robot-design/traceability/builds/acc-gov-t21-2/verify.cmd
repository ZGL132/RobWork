@echo off
call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
set "PATH=C:\Users\zgl18\.cache\codex-runtimes\codex-primary-runtime\dependencies\native\powershell;D:\software\CMake\bin;%PATH%"
cd /d D:\10_Source_Repos\21_robot\acc-gov-t21-r2-test
"C:\Users\zgl18\.cache\codex-runtimes\codex-primary-runtime\dependencies\native\powershell\pwsh.exe" -NoProfile -File RobWork/scripts/industrialrobot/verify-task.ps1 -TaskFile RobWork/doc/industrial-robot-design/tasks/GOV-T21.json
