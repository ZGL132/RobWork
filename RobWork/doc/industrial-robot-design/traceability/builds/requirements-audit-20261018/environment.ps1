# 本批验证的进程级环境：不修改系统设置；所有 GUI 目标在 VS2022 x64 下串行运行。
$ErrorActionPreference = 'Stop'
Import-Module 'D:/software/Microsoft Visual Studio/2022/Community/Common7/Tools/Microsoft.VisualStudio.DevShell.dll'
Enter-VsDevShell -VsInstallPath 'D:/software/Microsoft Visual Studio/2022/Community' -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
$env:QT_QPA_PLATFORM = 'windows'
$env:QT_QPA_PLATFORM_PLUGIN_PATH = 'D:/software/Qt/6.11.1/msvc2022_64/plugins/platforms'
$env:PATH = 'D:/10_Source_Repos/21_robot/RobWork/build/RobWorkStudio/bin/Release;D:/10_Source_Repos/21_robot/RobWork/build/RobWorkStudio/libs/Release;D:/10_Source_Repos/21_robot/RobWork/build/RobWork/bin/Release;D:/10_Source_Repos/21_robot/RobWork/build/RobWorkSim/bin/Release;D:/10_Source_Repos/21_robot/RobWork/vcpkg/installed/x64-windows/bin;D:/software/Qt/6.11.1/msvc2022_64/bin;D:/software/Miniconda3;' + $env:PATH
$auditBin = 'D:/10_Source_Repos/21_robot/RobWork/build/RobWorkStudio/bin/Release'
$auditOut = $PSScriptRoot
