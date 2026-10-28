param([string[]]$Units=@('drivetrain','evidence','workflow','selection'),[string]$Suffix='green',[string]$Filter='*',[string]$BinRoot='build/RobWorkStudio/bin/Release')
$ErrorActionPreference='Stop'
$repo='D:/10_Source_Repos/21_robot/RobWork'
# 模型测试逐exe执行；GUI消费者另按Qt Windows规则执行。
cmd /d /s /c '"D:\software\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && set' | ForEach-Object {
    if($_ -match '^([^=]+)=(.*)$'){[Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process')}
}
$env:PATH="$repo/vcpkg/installed/x64-windows/bin;D:/software/QT/6.11.1/msvc2022_64/bin;$env:PATH"
$env:QT_QPA_PLATFORM='windows'
foreach($unit in $Units){
    $exe="$repo/$BinRoot/sdurws_ird_${unit}_test.exe"
    $stem="$repo/build/design-fix-t01/$unit-$Suffix"
    & $exe "--gtest_filter=$Filter" "--gtest_output=xml:$stem.xml" "--ird_report=$stem-report.json" *> "$stem.log"
    $result=$LASTEXITCODE
    "${unit}: exit=$result"
    Get-Content "$stem.log" -Tail 6
}
