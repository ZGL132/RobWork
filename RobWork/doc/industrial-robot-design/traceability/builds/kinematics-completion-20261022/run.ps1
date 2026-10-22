param([string]$Mode='build', [string]$Tree='standalone', [string]$Name='kinematics_gui', [string]$Filter='*', [string]$Label=(Get-Date -Format 'HHmmssfff'))
$ErrorActionPreference='Stop'
$repo=(& git -C $PSScriptRoot rev-parse --show-toplevel).Trim()
Set-Location $repo
Import-Module 'D:/software/Microsoft Visual Studio/2022/Community/Common7/Tools/Microsoft.VisualStudio.DevShell.dll'
Enter-VsDevShell -VsInstallPath 'D:/software/Microsoft Visual Studio/2022/Community' -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
$env:CL="$env:CL /DNOMINMAX"
$env:QT_QPA_PLATFORM='windows'
$env:QT_QPA_PLATFORM_PLUGIN_PATH='D:/software/Qt/6.11.1/msvc2022_64/plugins/platforms'
$env:PATH="$repo/build/RobWorkStudio/bin/Release;$repo/build/RobWorkStudio/libs/Release;$repo/build/RobWork/libs/Release;$repo/build/RobWorkSim/libs/Release;$repo/vcpkg/installed/x64-windows/bin;D:/software/Qt/6.11.1/msvc2022_64/bin;D:/software/Miniconda3;"+$env:PATH
$out=Join-Path $PSScriptRoot 'evidence'
New-Item -ItemType Directory -Force $out | Out-Null
$buildTree=if($Tree -eq 'standalone'){'build/kin-current-audit'}else{'build'}
$log=Join-Path $out "$Mode-$Tree-$Name-$Label.log"
if($Mode -eq 'build') {
 $target=if($Name -eq 'studio'){'sdurws_ird_studio'}else{"sdurws_ird_${Name}_test"}
 & cmake --build $buildTree --config Release --target $target *> $log
} elseif($Mode -eq 'gates') {
 & cmake --build build --config Release --target ird_gates *> $log
} elseif($Mode -eq 'test') {
 $exe=if($Tree -eq 'standalone'){"$repo/$buildTree/kinematics/Release/sdurws_ird_${Name}_test.exe"}else{"$repo/build/RobWorkStudio/bin/Release/sdurws_ird_${Name}_test.exe"}
 $other=@(Get-CimInstance Win32_Process | Where-Object {$_.Name -match '^sdurws_ird_(.*gui.*|studio|.*_app)\.exe$'})
 if($other.Count){throw '存在其他 GUI 测试/宿主进程，禁止并发执行'}
 $env:IRD_KIN_GUI_EVIDENCE_OUT="$out/screens-$Tree-$Label"
 & $exe "--gtest_filter=$Filter" "--gtest_output=xml:$out/$Tree-$Name-$Label.xml" "--ird_report=$out/$Tree-$Name-$Label-report.json" *> $log
} else {throw "未知模式 $Mode"}
$code=$LASTEXITCODE
Get-Content $log -Tail 28
exit $code
