param([string]$Mode='test',[string]$Name='modeling_gui',[string]$Filter='*',[string]$Tag='baseline')
$ErrorActionPreference='Stop'
$taskRoot='C:/Users/zgl18/.codex/worktrees/486b/RobWork'
$taskOut="$taskRoot/build/modeling-interaction-20261021"
Import-Module 'D:/software/Microsoft Visual Studio/2022/Community/Common7/Tools/Microsoft.VisualStudio.DevShell.dll'
Enter-VsDevShell -VsInstallPath 'D:/software/Microsoft Visual Studio/2022/Community' -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
$env:CL="$env:CL /DNOMINMAX"
$env:QT_QPA_PLATFORM='windows'
$env:QT_QPA_PLATFORM_PLUGIN_PATH='D:/software/Qt/6.11.1/msvc2022_64/plugins/platforms'
$env:PATH="$taskRoot/build/RobWorkStudio/bin/Release;$taskRoot/build/RobWorkStudio/libs/Release;$taskRoot/build/RobWork/libs/Release;$taskRoot/build/RobWorkSim/libs/Release;D:/10_Source_Repos/21_robot/RobWork/vcpkg/installed/x64-windows/bin;D:/software/Qt/6.11.1/msvc2022_64/bin;D:/software/Miniconda3;"+$env:PATH
$env:IRD_MODELING_GUI_EVIDENCE_OUT="$taskOut/$Tag-shots"
Set-Location $taskRoot
if($Mode -eq 'runtime-build'){
 & cmake --build "$taskRoot/build" --config Release --parallel 6 --target sdurws_ird_runtime_test *> "$taskOut/$Tag-runtime-build.log"
 Get-Content "$taskOut/$Tag-runtime-build.log" -Tail 16;exit $LASTEXITCODE
}
if($Mode -eq 'io-build'){
 & cmake --build "$taskRoot/build" --config Release --parallel 6 --target sdurws_ird_io_test *> "$taskOut/$Tag-io-build.log"
 Get-Content "$taskOut/$Tag-io-build.log" -Tail 16;exit $LASTEXITCODE
}
if($Mode -eq 'build'){
 & cmake --build "$taskRoot/build" --config Release --parallel 6 --target sdurws_ird_modeling_gui_test sdurws_ird_modeling_test sdurws_ird_ui_gui_test sdurws_ird_ui_test *> "$taskOut/$Tag-build.log"
 Get-Content "$taskOut/$Tag-build.log" -Tail 16;exit $LASTEXITCODE
}
if($Mode -eq 'full-build'){
 & cmake --build "$taskRoot/build" --config Release --parallel 6 *> "$taskOut/$Tag-build.log"
 Get-Content "$taskOut/$Tag-build.log" -Tail 16;exit $LASTEXITCODE
}
if($Mode -eq 'gates'){
 & cmake --build "$taskRoot/build" --config Release --target ird_gates *> "$taskOut/$Tag-gates.log"
 Get-Content "$taskOut/$Tag-gates.log" -Tail 16;exit $LASTEXITCODE
}
if($Mode -eq 'standalone-build'){
 & cmake --build "$taskRoot/build/audit-closure-20261021/standalone" --config Release --parallel 6 *> "$taskOut/$Tag-standalone-build.log"
 Get-Content "$taskOut/$Tag-standalone-build.log" -Tail 16;exit $LASTEXITCODE
}
# 同桌面窗口验证串行，绝不结束其他会话的进程。
$other=@(Get-CimInstance Win32_Process | Where-Object {$_.Name -match '^(RobWorkStudio|sdurws_ird_(.*gui.*|studio|modeling_app))\.exe$'})
if($other.Count -gt 0){$other|Select-Object ProcessId,ExecutablePath|ConvertTo-Json;exit 71}
if($Mode -eq 'tour'){
 $env:IRD_UI_PLUGIN_SMOKE=$Name;$env:IRD_UI_PLUGIN_SMOKE_OUT="$taskOut/$Tag-tour"
 $exe="$taskRoot/build/RobWorkStudio/bin/Release/sdurws_ird_studio.exe";$arguments=@()
}else{
 if($Name.StartsWith('standalone_')){
  $shortName=$Name.Substring(11);$unit=$shortName.Split('_')[0]
  $exe="$taskRoot/build/audit-closure-20261021/standalone/$unit/Release/sdurws_ird_${shortName}_test.exe"
 }else{$exe="$taskRoot/build/RobWorkStudio/bin/Release/sdurws_ird_${Name}_test.exe"}
 $arguments=@("--gtest_filter=$Filter","--gtest_output=xml:$taskOut/$Tag-$Name.xml","--ird_report=$taskOut/$Tag-$Name-report.json")
}
$launch=@{FilePath=$exe;WorkingDirectory=$taskRoot;PassThru=$true;WindowStyle='Hidden';RedirectStandardOutput="$taskOut/$Tag-$Name.log";RedirectStandardError="$taskOut/$Tag-$Name.stderr.log"}
if($arguments.Count -gt 0){$launch.ArgumentList=$arguments}
$p=Start-Process @launch
$deadline=[DateTime]::UtcNow.AddSeconds(240)
while(!$p.HasExited){
 if([DateTime]::UtcNow -gt $deadline){Stop-Process -Id $p.Id;Write-Output "仅结束本轮自有超时进程 $($p.Id)";exit 73}
 Start-Sleep -Milliseconds 250;$p.Refresh()
}
Get-Content "$taskOut/$Tag-$Name.log" -Tail 60
if(Test-Path "$taskOut/$Tag-$Name.stderr.log"){Get-Content "$taskOut/$Tag-$Name.stderr.log" -Tail 12}
exit $p.ExitCode

