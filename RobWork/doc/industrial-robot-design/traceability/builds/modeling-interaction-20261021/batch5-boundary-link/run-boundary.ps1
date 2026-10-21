param([string]$Mode='build',[string]$Tag='batch5-red',[string]$Filter='HostCompilePipelineTest.NameMapPort_BoundaryLinksResolveExistingFrames_MDL07_T30')
$ErrorActionPreference='Stop'
$taskRoot='C:/Users/zgl18/.codex/worktrees/486b/RobWork'
Import-Module 'D:/software/Microsoft Visual Studio/2022/Community/Common7/Tools/Microsoft.VisualStudio.DevShell.dll'
Enter-VsDevShell -VsInstallPath 'D:/software/Microsoft Visual Studio/2022/Community' -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
$env:CL="$env:CL /DNOMINMAX"
$env:PATH="$taskRoot/build/RobWorkStudio/bin/Release;$taskRoot/build/RobWorkStudio/libs/Release;$taskRoot/build/RobWork/libs/Release;D:/10_Source_Repos/21_robot/RobWork/vcpkg/installed/x64-windows/bin;D:/software/Qt/6.11.1/msvc2022_64/bin;D:/software/Miniconda3;"+$env:PATH
Set-Location $taskRoot
if($Mode -eq 'build'){
 & cmake --build build --config Release --parallel 6 --target sdurws_ird_ui_test *> "build/modeling-interaction-20261021/$Tag-build.log"
 $taskExit=$LASTEXITCODE; Get-Content "build/modeling-interaction-20261021/$Tag-build.log" -Tail 12;exit $taskExit
}
# 本目标使用QCoreApplication且不创建窗口，允许用户继续操作其正式GUI。
& "$taskRoot/build/RobWorkStudio/bin/Release/sdurws_ird_ui_test.exe" "--gtest_filter=$Filter" "--gtest_output=xml:$taskRoot/build/modeling-interaction-20261021/$Tag-ui.xml" "--ird_report=$taskRoot/build/modeling-interaction-20261021/$Tag-ui-report.json" *> "build/modeling-interaction-20261021/$Tag-ui.log"
$taskExit=$LASTEXITCODE;Get-Content "build/modeling-interaction-20261021/$Tag-ui.log" -Tail 32;exit $taskExit
