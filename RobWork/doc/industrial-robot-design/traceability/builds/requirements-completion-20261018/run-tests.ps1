# WP-14-T12：当前工作树专用测试入口。GUI必须在VS x64、windows平台串行运行。
param([string]$BinDir, [string]$OutputDir, [string[]]$Targets = @('sdurws_ird_requirements_test', 'sdurws_ird_requirements_contract_test', 'sdurws_ird_requirements_gui_test'))
$ErrorActionPreference = 'Stop'
Import-Module 'D:/software/Microsoft Visual Studio/2022/Community/Common7/Tools/Microsoft.VisualStudio.DevShell.dll'
Enter-VsDevShell -VsInstallPath 'D:/software/Microsoft Visual Studio/2022/Community' -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
$env:QT_QPA_PLATFORM = 'windows'
$env:QT_QPA_PLATFORM_PLUGIN_PATH = 'D:/software/Qt/6.11.1/msvc2022_64/plugins/platforms'
$env:PATH = "$BinDir;C:/Users/zgl18/.codex/worktrees/486b/RobWork/build/RobWorkStudio/libs/Release;C:/Users/zgl18/.codex/worktrees/486b/RobWork/build/RobWork/libs/Release;C:/Users/zgl18/.codex/worktrees/486b/RobWork/build/RobWorkSim/libs/Release;D:/10_Source_Repos/21_robot/RobWork/vcpkg/installed/x64-windows/bin;D:/software/Qt/6.11.1/msvc2022_64/bin;D:/software/Miniconda3;" + $env:PATH
$BinDir = (Resolve-Path -LiteralPath $BinDir).Path
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$OutputDir = (Resolve-Path -LiteralPath $OutputDir).Path
foreach ($target in $Targets) {
    $exe = Join-Path $BinDir "$target.exe"
    $caseDir = Join-Path $OutputDir $target
    New-Item -ItemType Directory -Force -Path $caseDir | Out-Null
    Push-Location -LiteralPath $caseDir
    try {
        & $exe "--gtest_output=xml:$caseDir/gtest.xml" *> "$caseDir/console.log"
        $code = $LASTEXITCODE
        Write-Host "$target exit=$code"
        if ($code -ne 0) { throw "$target failed: exit=$code" }
    } finally { Pop-Location }
}
