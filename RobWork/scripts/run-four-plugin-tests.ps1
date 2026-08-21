param(
    [string]$BuildDirectory = "D:\10_Source_Repos\21_robot\RobWork\RobWork\build\codex-vs-debug5",
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Configuration = "Debug",
    [string]$Regex = "^sdurws_(robotmodelbuilder|engineeringrequirements|kinematicanalysis|structureoptimizer)_"
)

$ErrorActionPreference = "Stop"

# 统一校验多配置构建目录，避免把空路径传给 CTest 后产生误导性结果。
if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    [Console]::Error.WriteLine("BuildDirectory must not be empty")
    exit 2
}
$resolvedBuildDirectory = (Resolve-Path -LiteralPath $BuildDirectory -ErrorAction Stop).Path

# Qt Widgets 测试必须使用 Windows 平台；同时优先搜索当前配置的 DLL。
$env:QT_QPA_PLATFORM = "windows"
$dllDirectories = @(
    (Join-Path $resolvedBuildDirectory "RobWork\bin\$Configuration"),
    (Join-Path $resolvedBuildDirectory "RobWorkStudio\bin\$Configuration")
)
$existingDllDirectories = $dllDirectories | Where-Object { Test-Path -LiteralPath $_ }
if ($existingDllDirectories.Count -gt 0) {
    $env:PATH = (($existingDllDirectories -join ";") + ";" + $env:PATH)
}

Write-Output "Configuration=$Configuration"
Write-Output "BuildDirectory=$resolvedBuildDirectory"
Write-Output "Regex=$Regex"

# CTest 必须显式指定多配置生成器的配置，且串行运行以保持日志和资源确定性。
& ctest --test-dir $resolvedBuildDirectory -C $Configuration -j1 --output-on-failure -R $Regex
$exitCode = $LASTEXITCODE
if ($exitCode -ne 0) {
    [Console]::Error.WriteLine("CTest failed with exit code $exitCode")
}
exit $exitCode
