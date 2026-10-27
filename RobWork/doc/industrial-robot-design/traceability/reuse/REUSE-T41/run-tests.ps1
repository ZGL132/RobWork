# REUSE-T41 逐 exe 测试运行器（沿用 REUSE-T23 同款机制：绝对路径、单 exe、
# gtest XML＋per-exe ird-test-report.json、完整 stdout 落日志）。
# 用法：pwsh -NoProfile -File run-tests.ps1 -RepoRoot <仓库根> -Label <证据标签> [-Filter <gtest过滤>]
param(
    [string]$RepoRoot = 'D:\10_Source_Repos\21_robot\RobWork',
    [Parameter(Mandatory = $true)][string]$Label,
    [string]$Filter = ''
)
$ErrorActionPreference = 'Stop'
$evidence = Join-Path $RepoRoot 'RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T41'
$env:QT_QPA_PLATFORM = 'windows'
# PATH 前置＝T23/UI-T23 验收同款四段（Miniconda 供 python313.dll〔F-428 处置〕、
# Qt bin、vcpkg installed bin〔F-320 传递依赖〕、框架 RobWork 产物）。
$env:PATH = "D:\software\Miniconda3;D:\software\Qt\6.11.1\msvc2022_64\bin;$RepoRoot\vcpkg\installed\x64-windows\bin;$RepoRoot\build\RobWork\bin\Release;" + $env:PATH
$exe = Join-Path $RepoRoot "build/RobWorkStudio/bin/Release/$Label.exe"
if (-not (Test-Path $exe)) { Write-Output "NOT RUN: $exe 不存在"; exit 3 }
$args = @("--gtest_output=xml:$evidence/$Label.xml", "--ird_report=$evidence/$Label-report.json")
if ($Filter -ne '') { $args += "--gtest_filter=$Filter" }
& $exe @args *> "$evidence/$Label.log"
$testExit = $LASTEXITCODE
Get-Content "$evidence/$Label.log" -Tail 6
exit $testExit
