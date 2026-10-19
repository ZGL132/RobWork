param([ValidateSet('build','full-build','standalone-build','gates','verify','studio-build','tour','compile','test','ik')][string]$Mode='test',[string]$Name='requirements',[string]$Filter='*')
$ErrorActionPreference='Stop'
$auditRoot='C:/Users/zgl18/.codex/worktrees/486b/RobWork'
$auditOut=Join-Path $auditRoot 'build/requirements-close-20261019'
Import-Module 'D:/software/Microsoft Visual Studio/2022/Community/Common7/Tools/Microsoft.VisualStudio.DevShell.dll'
Enter-VsDevShell -VsInstallPath 'D:/software/Microsoft Visual Studio/2022/Community' -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
# 上游RWSim插件未消费已有RWS_DEFINITIONS中的NOMINMAX，重新配置后的
# 全量编译会把KDTree的min/max误展开为Windows宏。只补编译环境，不改框架。
$env:CL="$env:CL /DNOMINMAX"
$env:QT_QPA_PLATFORM='windows'
$env:QT_QPA_PLATFORM_PLUGIN_PATH='D:/software/Qt/6.11.1/msvc2022_64/plugins/platforms'
$env:PATH="$auditRoot/build/RobWorkStudio/bin/Release;$auditRoot/build/RobWorkStudio/libs/Release;$auditRoot/build/RobWork/libs/Release;$auditRoot/build/RobWorkSim/libs/Release;D:/10_Source_Repos/21_robot/RobWork/vcpkg/installed/x64-windows/bin;D:/software/Qt/6.11.1/msvc2022_64/bin;D:/software/Miniconda3;"+$env:PATH
& cl.exe "@C:/Users/zgl18/.codex/worktrees/486b/RobWork/build/requirements-close-followup-20261020/mixed_probe.rsp" *> 'C:/Users/zgl18/.codex/worktrees/486b/RobWork/build/requirements-close-followup-20261020/build.log'
if($LASTEXITCODE -ne 0){Get-Content 'C:/Users/zgl18/.codex/worktrees/486b/RobWork/build/requirements-close-followup-20261020/build.log' -Tail 18;exit $LASTEXITCODE}
& 'C:/Users/zgl18/.codex/worktrees/486b/RobWork/build/requirements-close-followup-20261020/mixed_probe.exe' '--gtest_filter=RequirementsClosureFollowup.*' '--gtest_output=xml:C:/Users/zgl18/.codex/worktrees/486b/RobWork/build/requirements-close-followup-20261020/mixed.xml' '--ird_report=C:/Users/zgl18/.codex/worktrees/486b/RobWork/build/requirements-close-followup-20261020/mixed-report.json' *> 'C:/Users/zgl18/.codex/worktrees/486b/RobWork/build/requirements-close-followup-20261020/mixed.log'
$reqExit=$LASTEXITCODE
Get-Content 'C:/Users/zgl18/.codex/worktrees/486b/RobWork/build/requirements-close-followup-20261020/mixed.log' -Tail 25
exit $reqExit