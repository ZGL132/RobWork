# 本轮隔离探针的重放入口；只写本 C: 工作树的评审临时目录。
$ErrorActionPreference='Stop'
$reviewRepo='C:\Users\zgl18\.codex\worktrees\core-review-fixes\RobWork'
$reviewOut=Join-Path $reviewRepo 'build\modeling-review-20261028'
Set-Location -LiteralPath $reviewRepo
$reviewDev='D:\software\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat'
cmd /d /s /c "`"$reviewDev`" -no_logo -arch=x64 -host_arch=x64 && set" | ForEach-Object {
    if($_ -match '^([^=]+)=(.*)$'){
        [Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process')
    }
}
$env:PATH="$reviewRepo\build\RobWorkStudio\bin\Release;D:\software\QT\6.11.1\msvc2022_64\bin;D:\10_Source_Repos\21_robot\RobWork\vcpkg\installed\x64-windows\bin;"+$env:PATH
# 执行时指定新的日志名，保留原始评审输出。
& cl.exe "@$reviewOut\probe-r4.rsp" *> "$reviewOut\replay-build.log"
if($LASTEXITCODE -ne 0){throw '隔离探针编译失败，见 replay-build.log'}
& "$reviewOut\probe.exe" "--gtest_output=xml:$reviewOut\replay.xml" "--ird_report=$reviewOut\replay.ird-test-report.json" *> "$reviewOut\replay.log"
$reviewBehaviorExit=$LASTEXITCODE
& cl.exe "@$reviewOut\header-coinclude.rsp" *> "$reviewOut\replay-header.log"
$reviewHeaderExit=$LASTEXITCODE
Write-Output "behaviorExit=$reviewBehaviorExit; headerSyntaxExit=$reviewHeaderExit（当前缺陷版本分别预期 1、2）"
