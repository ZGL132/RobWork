param([string]$Mode='test', [string]$Target='sdurws_ird_trajectory_gui_test', [string]$Tag='verify', [string]$Filter='*', [string]$BuildTree='build')
$ErrorActionPreference='Stop'
$taskRoot=(Resolve-Path "$PSScriptRoot/../../../../../..").Path
Import-Module 'D:/software/Microsoft Visual Studio/2022/Community/Common7/Tools/Microsoft.VisualStudio.DevShell.dll'
Enter-VsDevShell -VsInstallPath 'D:/software/Microsoft Visual Studio/2022/Community' -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
$env:CL="$env:CL /DNOMINMAX"
$env:QT_QPA_PLATFORM='windows'
$env:QT_QPA_PLATFORM_PLUGIN_PATH='D:/software/Qt/6.11.1/msvc2022_64/plugins/platforms'
$env:PATH="$taskRoot/build/RobWork/bin/Release;$taskRoot/build/RobWorkStudio/bin/Release;$taskRoot/build/RobWorkSim/bin/Release;D:/10_Source_Repos/21_robot/RobWork/vcpkg/installed/x64-windows/bin;D:/software/Qt/6.11.1/msvc2022_64/bin;D:/software/Miniconda3;"+$env:PATH
Set-Location $taskRoot
$taskBuildRoot=(Resolve-Path (Join-Path $taskRoot $BuildTree)).Path
if($Mode -eq 'build') {
    & cmake --build $taskBuildRoot --config Release --target $Target -- /m:2 *> "$PSScriptRoot/$Tag-build.log"
    $taskCode=$LASTEXITCODE
    Get-Content "$PSScriptRoot/$Tag-build.log" -Tail 18
    exit $taskCode
}
# 同一桌面只运行一个真实 GUI；仅结束本脚本启动且超时的进程。
$taskOther=@(Get-CimInstance Win32_Process | Where-Object {$_.Name -match '^(sdurws_ird_.*gui.*|sdurws_ird_studio|RobWorkStudio)\.exe$'})
if($taskOther.Count -gt 0){$taskOther | Select-Object ProcessId,ExecutablePath | ConvertTo-Json; exit 71}
$env:IRD_TRAJECTORY_GUI_EVIDENCE_OUT="$PSScriptRoot/$Tag-shots"
[void](New-Item -ItemType Directory -Force -Path $env:IRD_TRAJECTORY_GUI_EVIDENCE_OUT)
$taskArgs=@("--gtest_filter=$Filter","--gtest_output=xml:$PSScriptRoot/$Tag.xml","--ird_report=$PSScriptRoot/$Tag-report.json")
# Windows会复用PID。部分旧测试以PID命名临时目录，前一轮遗留文件可能
# 让“只写一份报告”的断言误失败；给子进程独立TEMP，保留原失败证据。
$taskTempRoot=Join-Path ([IO.Path]::GetTempPath()) ('ird-trj-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
[void](New-Item -ItemType Directory -Path $taskTempRoot)
$env:TEMP=$taskTempRoot
$env:TMP=$taskTempRoot
[IO.File]::WriteAllText("$PSScriptRoot/$Tag-environment.json",(@{temp=$taskTempRoot;platform=$env:QT_QPA_PLATFORM;buildTree=$taskBuildRoot}|ConvertTo-Json))
if($Mode -eq 'host') {
    $env:IRD_UI_PLUGIN_SMOKE=$Filter
    $env:IRD_UI_PLUGIN_SMOKE_OUT="$PSScriptRoot/$Tag-shots"
    $taskArgs=@()
}
$taskExecutable=Join-Path $taskBuildRoot "RobWorkStudio/bin/Release/$Target.exe"
if(!(Test-Path -LiteralPath $taskExecutable)) {
    # 独立树没有宿主部署步骤。显式提供Qt前缀配置并沿单元输出目录定位，
    # 与集成程序分别留痕；独立测试不包含框架数值计算的条件源，不能混算。
    $taskExecutable=Join-Path $taskBuildRoot "trajectory/Release/$Target.exe"
    if(!(Test-Path -LiteralPath $taskExecutable)){throw "验证程序不存在: $taskExecutable"}
    [IO.File]::WriteAllText((Join-Path (Split-Path $taskExecutable) 'qt.conf'),"[Paths]`nPrefix=D:/software/Qt/6.11.1/msvc2022_64`nPlugins=plugins`n")
}
$taskLaunch=@{FilePath=$taskExecutable;WorkingDirectory=$taskRoot;PassThru=$true;WindowStyle='Hidden';RedirectStandardOutput="$PSScriptRoot/$Tag.log";RedirectStandardError="$PSScriptRoot/$Tag-stderr.log"}
if($taskArgs.Count -gt 0){$taskLaunch.ArgumentList=$taskArgs}
$taskProcess=Start-Process @taskLaunch
$taskDeadline=[DateTime]::UtcNow.AddSeconds(240)
while(!$taskProcess.HasExited) {
    if([DateTime]::UtcNow -gt $taskDeadline){Stop-Process -Id $taskProcess.Id;exit 73}
    Start-Sleep -Milliseconds 250
    $taskProcess.Refresh()
}
Get-Content "$PSScriptRoot/$Tag.log" -Tail 30
Get-Content "$PSScriptRoot/$Tag-stderr.log" -Tail 12
exit $taskProcess.ExitCode
