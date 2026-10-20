param([ValidateSet('build','full-build','standalone-build','gates','verify','studio-build','tour','compile','test','ik')][string]$Mode='test',[string]$Name='requirements',[string]$Filter='*')
$ErrorActionPreference='Stop'
$auditRoot='C:/Users/zgl18/.codex/worktrees/486b/RobWork'
$auditOut=Join-Path $auditRoot 'build/modeling-completion-20261020-b3c'
Import-Module 'D:/software/Microsoft Visual Studio/2022/Community/Common7/Tools/Microsoft.VisualStudio.DevShell.dll'
Enter-VsDevShell -VsInstallPath 'D:/software/Microsoft Visual Studio/2022/Community' -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
# 上游RWSim插件未消费已有RWS_DEFINITIONS中的NOMINMAX，重新配置后的
# 全量编译会把KDTree的min/max误展开为Windows宏。只补编译环境，不改框架。
$env:CL="$env:CL /DNOMINMAX"
$env:QT_QPA_PLATFORM='windows'
$env:IRD_MODELING_GUI_EVIDENCE_OUT="$auditOut/gui"
$env:QT_QPA_PLATFORM_PLUGIN_PATH='D:/software/Qt/6.11.1/msvc2022_64/plugins/platforms'
$env:PATH="$auditRoot/build/RobWorkStudio/bin/Release;$auditRoot/build/RobWorkStudio/libs/Release;$auditRoot/build/RobWork/libs/Release;$auditRoot/build/RobWorkSim/libs/Release;D:/10_Source_Repos/21_robot/RobWork/vcpkg/installed/x64-windows/bin;D:/software/Qt/6.11.1/msvc2022_64/bin;D:/software/Miniconda3;"+$env:PATH
if($Mode -eq 'full-build') {
 & cmake --build "$auditRoot/build" --config Release *> "$auditOut/full-build.log"
} elseif($Mode -eq 'build') {
 & cmake --build "$auditRoot/build" --config Release --target sdurws_ird_modeling_test sdurws_ird_modeling_gui_test sdurws_ird_ui_test *> "$auditOut/build.log"
} elseif($Mode -eq 'standalone-build') {
 & cmake --build "$auditRoot/build/modeling-audit-20261019/standalone" --config Release --target sdurws_ird_modeling_test sdurws_ird_modeling_contract_test sdurws_ird_modeling_gui_test sdurws_ird_kinematics_test sdurws_ird_kinematics_contract_test sdurws_ird_kinematics_gui_test sdurws_ird_ui_test sdurws_ird_ui_contract_test sdurws_ird_ui_gui_test sdurws_ird_requirements_test sdurws_ird_requirements_contract_test sdurws_ird_requirements_gui_test *> "$auditOut/standalone-build.log"
} elseif($Mode -eq 'gates') {
 & cmake --build "$auditRoot/build" --config Release --target ird_gates *> "$auditOut/gates.log"
} elseif($Mode -eq 'verify') {
 & pwsh -NoProfile -File "$auditRoot/RobWork/scripts/industrialrobot/verify-task.ps1" -TaskFile "$auditRoot/RobWork/doc/industrial-robot-design/tasks/foundation/WP-13-T24.json" *> "$auditOut/verify-task.log"
} elseif($Mode -eq 'studio-build') {
 & cmake --build "$auditRoot/build" --config Release --target sdurws_ird_studio sdurws_ird_ui_gui_test sdurws_ird_ui_contract_test sdurws_ird_kinematics_contract_test sdurws_ird_kinematics_gui_test *> "$auditOut/studio-build.log"
} elseif($Mode -eq 'tour') {
 $env:IRD_UI_PLUGIN_SMOKE=$Name
 $env:IRD_UI_PLUGIN_SMOKE_OUT="$auditOut/$Name"
 $tourProcess=Start-Process -FilePath "$auditRoot/build/RobWorkStudio/bin/Release/sdurws_ird_studio.exe" -WorkingDirectory $auditRoot -PassThru -Wait -WindowStyle Hidden -RedirectStandardOutput "$auditOut/$Name.log" -RedirectStandardError "$auditOut/$Name.stderr.log"
 $global:LASTEXITCODE=$tourProcess.ExitCode
} elseif($Mode -eq 'compile') {
 foreach($probeName in @('ik_probe','gui_probe','ui_probe','residual_probe')) {
  # 保留本轮冻结的探针与链接响应文件；重跑不得以旧审核探针覆盖新增边界用例。
  if(!(Test-Path -LiteralPath "$auditOut/$probeName.rsp")){throw "缺少探针响应文件：$probeName"}
  & cl.exe "@$auditOut/$probeName.rsp" *> "$auditOut/$probeName-build.log"
  if($LASTEXITCODE -ne 0){Get-Content "$auditOut/$probeName-build.log" -Tail 20;exit $LASTEXITCODE}
 }
} elseif($Mode -eq 'ik') {
 & "$auditOut/ik_probe.exe" *> "$auditOut/ik-probe.log"
} else {
 $exe=if($Name -in @('gui_probe','ui_probe','residual_probe')){"$auditOut/$Name.exe"}elseif($Name.StartsWith('standalone_')){"$auditRoot/build/modeling-audit-20261019/standalone/"+$Name.Substring(11).Split('_')[0]+"/Release/sdurws_ird_"+$Name.Substring(11)+"_test.exe"}else{"$auditRoot/build/RobWorkStudio/bin/Release/sdurws_ird_${Name}_test.exe"}
 & $exe "--gtest_filter=$Filter" "--gtest_output=xml:$auditOut/$Name.xml" "--ird_report=$auditOut/$Name-report.json" *> "$auditOut/$Name.log"
}
$result=$LASTEXITCODE
if($Mode -eq 'compile'){'Probes compiled'}elseif($Mode -eq 'ik'){Get-Content "$auditOut/ik-probe.log"}elseif($Mode -in @('build','full-build','standalone-build','studio-build','gates')){Get-Content "$auditOut/$Mode.log" -Tail 12}elseif($Mode -eq 'verify'){Get-Content "$auditOut/verify-task.log" -Tail 12}else{Get-Content "$auditOut/$Name.log" -Tail 24}
"Exit=$result"
exit $result


