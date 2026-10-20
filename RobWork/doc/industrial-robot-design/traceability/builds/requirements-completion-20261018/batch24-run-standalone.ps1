# WP-14-T12 A-15独立回归：仅一次VS环境初始化，所有exe按绝对路径串行。
$ErrorActionPreference = 'Stop'
Import-Module 'D:/software/Microsoft Visual Studio/2022/Community/Common7/Tools/Microsoft.VisualStudio.DevShell.dll'
Enter-VsDevShell -VsInstallPath 'D:/software/Microsoft Visual Studio/2022/Community' -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
$env:QT_QPA_PLATFORM = 'windows'
$env:QT_QPA_PLATFORM_PLUGIN_PATH = 'D:/software/Qt/6.11.1/msvc2022_64/plugins/platforms'
$env:PATH = "C:/Users/zgl18/.codex/worktrees/486b/RobWork/build/RobWorkStudio/bin/Release;C:/Users/zgl18/.codex/worktrees/486b/RobWork/build/RobWorkStudio/libs/Release;C:/Users/zgl18/.codex/worktrees/486b/RobWork/build/RobWork/libs/Release;C:/Users/zgl18/.codex/worktrees/486b/RobWork/build/RobWorkSim/libs/Release;D:/10_Source_Repos/21_robot/RobWork/vcpkg/installed/x64-windows/bin;D:/software/Qt/6.11.1/msvc2022_64/bin;D:/software/Miniconda3;" + $env:PATH
$tree = (Resolve-Path 'RobWork/doc/industrial-robot-design/traceability/builds/requirements-audit-20261018/standalone').Path
$out = Join-Path $PSScriptRoot 'batch24-final-standalone'
New-Item -ItemType Directory -Force -Path $out | Out-Null
foreach ($unit in @('runtime','modeling','requirements','ui','execution','trajectory','dynamics','selection','core')) {
    $targets = @("sdurws_ird_${unit}_test", "sdurws_ird_${unit}_contract_test", "sdurws_ird_${unit}_gui_test")
    foreach ($target in $targets) {
        $exe = Join-Path $tree "$unit/Release/$target.exe"
        # 各单元目标清单不同，只执行该构建树实际注册生成的exe；缺契约不是通过项。
        if (!(Test-Path -LiteralPath $exe)) { Write-Host "$target NOT REGISTERED"; continue }
        $dir = Join-Path $out $target
        New-Item -ItemType Directory -Force -Path $dir | Out-Null
        Push-Location -LiteralPath $dir
        try {
            & $exe "--gtest_output=xml:$dir/gtest.xml" *> "$dir/console.log"
            $code = $LASTEXITCODE
            Write-Host "$target exit=$code"
            if ($code -ne 0) { throw "$target failed: exit=$code" }
        } finally { Pop-Location }
    }
}
