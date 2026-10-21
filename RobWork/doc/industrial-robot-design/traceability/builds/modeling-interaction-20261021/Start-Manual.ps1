# 本机第1批人工验收启动器；不自动打开或修改任何工程。
$ErrorActionPreference='Stop'
$taskRoot='C:/Users/zgl18/.codex/worktrees/486b/RobWork'
$others=@(Get-CimInstance Win32_Process | Where-Object {$_.Name -match '^(RobWorkStudio|sdurws_ird_(.*gui.*|studio|modeling_app))\.exe$'})
if($others.Count){Write-Host '请先保存并关闭已有IRD/RobWork窗口，再启动本批程序。';exit 1}
Import-Module 'D:/software/Microsoft Visual Studio/2022/Community/Common7/Tools/Microsoft.VisualStudio.DevShell.dll'
Enter-VsDevShell -VsInstallPath 'D:/software/Microsoft Visual Studio/2022/Community' -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
$env:QT_QPA_PLATFORM='windows'
$env:QT_QPA_PLATFORM_PLUGIN_PATH='D:/software/Qt/6.11.1/msvc2022_64/plugins/platforms'
$env:PATH="$taskRoot/build/RobWorkStudio/bin/Release;$taskRoot/build/RobWorkStudio/libs/Release;D:/10_Source_Repos/21_robot/RobWork/vcpkg/installed/x64-windows/bin;D:/software/Qt/6.11.1/msvc2022_64/bin;D:/software/Miniconda3;"+$env:PATH
Remove-Item Env:IRD_UI_PLUGIN_SMOKE -ErrorAction SilentlyContinue
Remove-Item Env:IRD_UI_PLUGIN_SMOKE_OUT -ErrorAction SilentlyContinue
Start-Process -FilePath "$taskRoot/build/RobWorkStudio/bin/Release/sdurws_ird_studio.exe" -WorkingDirectory $taskRoot -WindowStyle Hidden
