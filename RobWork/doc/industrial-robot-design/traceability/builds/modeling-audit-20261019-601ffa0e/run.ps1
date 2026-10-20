param([ValidateSet('test','list','build','tour')][string]$Mode='test',[string]$Name='modeling',[string]$Filter='*',[string]$Executable='')
$ErrorActionPreference='Stop'
$root='C:/Users/zgl18/.codex/worktrees/486b/RobWork'
$out=Join-Path $root 'build/modeling-audit-20261019'
Import-Module 'D:/software/Microsoft Visual Studio/2022/Community/Common7/Tools/Microsoft.VisualStudio.DevShell.dll'
Enter-VsDevShell -VsInstallPath 'D:/software/Microsoft Visual Studio/2022/Community' -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
$env:QT_QPA_PLATFORM='windows'
$env:QT_QPA_PLATFORM_PLUGIN_PATH='D:/software/Qt/6.11.1/msvc2022_64/plugins/platforms'
$env:PATH="$root/build/RobWorkStudio/bin/Release;$root/build/RobWorkStudio/libs/Release;$root/build/RobWork/libs/Release;$root/build/RobWorkSim/libs/Release;D:/10_Source_Repos/21_robot/RobWork/vcpkg/installed/x64-windows/bin;D:/software/Qt/6.11.1/msvc2022_64/bin;D:/software/Miniconda3;"+$env:PATH
if($Mode -eq 'build') {
  & cmake --build "$root/build" --config Release --target sdurws_ird_modeling_test sdurws_ird_modeling_contract_test sdurws_ird_modeling_gui_test sdurws_ird_ui_gui_test sdurws_ird_ui_test sdurws_ird_studio sdurws_ird_runtime_test sdurws_ird_dynamics_test sdurws_ird_drivetrain_test sdurws_ird_kinematics_test sdurws_ird_trajectory_test sdurws_ird_selection_test sdurws_ird_reporting_test *> "$out/build.log"
} elseif($Mode -eq 'tour') {
  $env:IRD_UI_PLUGIN_SMOKE='modeling-tour'
  $env:IRD_UI_PLUGIN_SMOKE_OUT="$out/tour-r2"
  $process=Start-Process -FilePath "$root/build/RobWorkStudio/bin/Release/sdurws_ird_studio.exe" -WorkingDirectory $root -PassThru -Wait -WindowStyle Hidden -RedirectStandardOutput "$out/tour-r2.log" -RedirectStandardError "$out/tour-r2.stderr.log"
  $global:LASTEXITCODE=$process.ExitCode
} else {
  $exe="$root/build/RobWorkStudio/bin/Release/sdurws_ird_${Name}_test.exe"
  if($Executable) { $exe=$Executable }
  if($Mode -eq 'list') { & $exe --gtest_list_tests *> "$out/$Name-list.txt" }
  else { & $exe "--gtest_filter=$Filter" "--gtest_output=xml:$out/$Name.xml" "--ird_report=$out/$Name-report.json" *> "$out/$Name.log" }
}
$code=$LASTEXITCODE
if($Mode -eq 'build') { Get-Content "$out/build.log" -Tail 16 }
elseif($Mode -eq 'tour') { Get-Content "$out/tour-r2.log" -Tail 38 }
elseif($Mode -eq 'test') { Get-Content "$out/$Name.log" -Tail 20 }
Write-Output "Exit=$code"
exit $code
