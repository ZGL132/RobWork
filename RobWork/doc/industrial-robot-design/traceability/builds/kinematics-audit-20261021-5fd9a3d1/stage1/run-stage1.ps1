param([string]$Mode='configure',[string]$Name='kinematics',[string]$Filter='*',[string]$Label=(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
$ErrorActionPreference='Stop'
$auditRoot=(& git -C $PSScriptRoot rev-parse --show-toplevel).Trim()
$auditOut=Join-Path $auditRoot 'build/kinematics-audit-20261021'
Set-Location $auditRoot
Import-Module 'D:/software/Microsoft Visual Studio/2022/Community/Common7/Tools/Microsoft.VisualStudio.DevShell.dll'
Enter-VsDevShell -VsInstallPath 'D:/software/Microsoft Visual Studio/2022/Community' -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
$env:CL="$env:CL /DNOMINMAX"
$env:QT_QPA_PLATFORM='windows'
$env:QT_QPA_PLATFORM_PLUGIN_PATH='D:/software/Qt/6.11.1/msvc2022_64/plugins/platforms'
$env:PATH="$auditRoot/build/RobWorkStudio/bin/Release;$auditRoot/build/RobWorkStudio/libs/Release;$auditRoot/build/RobWork/libs/Release;$auditRoot/build/RobWorkSim/libs/Release;D:/10_Source_Repos/21_robot/RobWork/vcpkg/installed/x64-windows/bin;D:/software/Qt/6.11.1/msvc2022_64/bin;D:/software/Miniconda3;"+$env:PATH
$log=Join-Path $auditOut "$Mode-$Name-$Label.log"
if($Mode -eq 'configure') {
 & cmake -S RobWork -B build -G 'Visual Studio 17 2022' -A x64 -DRWS_BUILD_INDUSTRIALROBOT=ON -DCMAKE_TOOLCHAIN_FILE=D:/10_Source_Repos/21_robot/RobWork/vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_PREFIX_PATH=D:/software/Qt/6.11.1/msvc2022_64 *> $log
} elseif($Mode -eq 'build') {
 & cmake --build build --config Release --target sdurws_ird_kinematics_test sdurws_ird_kinematics_contract_test sdurws_ird_kinematics_gui_test sdurws_ird_studio *> $log
} elseif($Mode -eq 'gui-build') {
 & cmake --build build --config Release --target sdurws_ird_kinematics_gui_test *> $log
} elseif($Mode -eq 'standalone-configure') {
 & cmake -S RobWork/RobWorkStudio/src/rwslibs/industrialrobot -B "$auditOut/standalone" -G 'Visual Studio 17 2022' -A x64 -DCMAKE_TOOLCHAIN_FILE=D:/10_Source_Repos/21_robot/RobWork/vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_PREFIX_PATH=D:/software/Qt/6.11.1/msvc2022_64 *> $log
} elseif($Mode -eq 'standalone-build') {
 & cmake --build "$auditOut/standalone" --config Release --target sdurws_ird_kinematics_test sdurws_ird_kinematics_contract_test sdurws_ird_kinematics_gui_test *> $log
} elseif($Mode -eq 'test') {
 $env:IRD_KIN_GUI_EVIDENCE_OUT="$auditOut/gui-$Name-$Label"
 $exe="$auditRoot/build/RobWorkStudio/bin/Release/sdurws_ird_${Name}_test.exe"
 if($Name.StartsWith('standalone_')) {
  $exe="$auditOut/standalone/kinematics/Release/sdurws_ird_"+$Name.Substring(11)+"_test.exe"
 }
 $other=@(Get-CimInstance Win32_Process | Where-Object {$_.Name -match '^sdurws_ird_(.*gui.*|studio|.*_app)\.exe$'})
 if($other.Count -gt 0){$other | Select-Object ProcessId,ExecutablePath; exit 71}
 $testProcess=Start-Process -FilePath $exe -ArgumentList @("--gtest_filter=$Filter","--gtest_output=xml:$auditOut/$Name-$Label.xml","--ird_report=$auditOut/$Name-$Label-report.json") -PassThru -WindowStyle Hidden -RedirectStandardOutput $log -RedirectStandardError "$auditOut/$Name-$Label.stderr.log"
 $deadline=[DateTime]::UtcNow.AddSeconds(240)
 while(!$testProcess.HasExited) {
  $overlap=@(Get-CimInstance Win32_Process | Where-Object {$_.ProcessId -ne $testProcess.Id -and $_.Name -match '^sdurws_ird_(.*gui.*|studio|.*_app)\.exe$'})
  if($overlap.Count -gt 0) {$overlap | Select-Object ProcessId,ExecutablePath | ConvertTo-Json | Set-Content "$auditOut/$Name-$Label-concurrency-invalid.json"; Stop-Process -Id $testProcess.Id; exit 72}
  if([DateTime]::UtcNow -gt $deadline) {Stop-Process -Id $testProcess.Id; '本轮自有测试超时' | Set-Content "$auditOut/$Name-$Label-timeout.log"; exit 73}
  Start-Sleep -Milliseconds 300
  $testProcess.Refresh()
 }
 $global:LASTEXITCODE=$testProcess.ExitCode
} elseif($Mode -eq 'gates') {
 & cmake --build build --config Release --target ird_gates *> $log
} else {throw "未知模式 $Mode"}
$result=$LASTEXITCODE
Get-Content $log -Tail 18
exit $result
