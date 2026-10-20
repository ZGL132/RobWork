param([ValidateSet('build','compile','test','ik')][string]$Mode='test',[string]$Name='requirements',[string]$Filter='*')
$ErrorActionPreference='Stop'
$auditRoot='C:/Users/zgl18/.codex/worktrees/486b/RobWork'
$auditOut=Join-Path $auditRoot 'build/requirements-recheck-20261019'
Import-Module 'D:/software/Microsoft Visual Studio/2022/Community/Common7/Tools/Microsoft.VisualStudio.DevShell.dll'
Enter-VsDevShell -VsInstallPath 'D:/software/Microsoft Visual Studio/2022/Community' -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
$env:QT_QPA_PLATFORM='windows'
$env:QT_QPA_PLATFORM_PLUGIN_PATH='D:/software/Qt/6.11.1/msvc2022_64/plugins/platforms'
$env:PATH="$auditRoot/build/RobWorkStudio/bin/Release;$auditRoot/build/RobWorkStudio/libs/Release;$auditRoot/build/RobWork/libs/Release;$auditRoot/build/RobWorkSim/libs/Release;D:/10_Source_Repos/21_robot/RobWork/vcpkg/installed/x64-windows/bin;D:/software/Qt/6.11.1/msvc2022_64/bin;D:/software/Miniconda3;"+$env:PATH
if($Mode -eq 'build') {
 & cmake --build "$auditRoot/build" --config Release --target sdurws_ird_requirements_test sdurws_ird_requirements_contract_test sdurws_ird_requirements_gui_test sdurws_ird_kinematics_test sdurws_ird_ui_test *> "$auditOut/build.log"
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
 $exe=if($Name -in @('gui_probe','ui_probe','residual_probe')){"$auditOut/$Name.exe"}else{"$auditRoot/build/RobWorkStudio/bin/Release/sdurws_ird_${Name}_test.exe"}
 & $exe "--gtest_filter=$Filter" "--gtest_output=xml:$auditOut/$Name.xml" "--ird_report=$auditOut/$Name-report.json" *> "$auditOut/$Name.log"
}
$result=$LASTEXITCODE
if($Mode -eq 'compile'){'Probes compiled'}elseif($Mode -eq 'ik'){Get-Content "$auditOut/ik-probe.log"}elseif($Mode -eq 'build'){Get-Content "$auditOut/build.log" -Tail 10}else{Get-Content "$auditOut/$Name.log" -Tail 24}
"Exit=$result"
exit $result
