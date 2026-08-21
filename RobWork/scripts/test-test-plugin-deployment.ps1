param(
    [string]$RootBuildPath = "D:\10_Source_Repos\21_robot\RobWork\RobWork\build\codex-vs-debug5",
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$ConfigName = "Debug"
)

$ErrorActionPreference = "Stop"

# 插件清单与 Windows 运行库必须部署到同一个输出目录。
if (-not (Test-Path "${RootBuildPath}\RobWork\bin\${ConfigName}\test_plugin.rwplugin.xml")) {
    Write-Error "test plugin manifest is missing"
    exit 1
}
if (-not (Test-Path "${RootBuildPath}\RobWork\bin\${ConfigName}\test_plugin.rwplugin.dll")) {
    Write-Error "test plugin runtime is missing"
    exit 1
}

Write-Output "test plugin manifest deployed"
Write-Output "test plugin runtime deployed"
exit 0
