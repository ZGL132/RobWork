# 建模面板开发验证 harness 启动脚本（sdurws_ird_modeling_app）。
# 前置：集成模式已构建——cmake --build build --config Release --target sdurws_ird_modeling_app
# 用法：pwsh -File run-modeling-app.ps1
# 产物：运行目录（%TEMP%\ird-modeling-app-run）下 console-*.log（命令回显）。
# 语义约定（WP-13-T15 增量，owner 指示 2026-09-26）：命令仅回显不进管线；
# 就绪条为演示条目；L-1/L-2/L-7 为真实域路径，可手动点验。
$root = 'D:\10_Source_Repos\21_robot\RobWork'
$runDir = Join-Path $env:TEMP 'ird-modeling-app-run'
New-Item -ItemType Directory -Force -Path $runDir | Out-Null

# PATH 前缀（F-320 同款清单）：构建树两 bin 目录＋vcpkg＋Qt——非链接面
# 共享库（RobWork 数学库等）的解析兜底；Qt DLL 已由 POST_BUILD 复制到
# exe 旁（qt.conf 钉前缀），此处为冗余保险。
$pathPre = "$root\build\RobWorkStudio\bin\Release;$root\build\RobWork\bin\Release;$root\vcpkg\installed\x64-windows\bin;D:\software\Qt\6.11.1\msvc2022_64\bin;"
$env:PATH = $pathPre + $env:PATH

$exe = "$root\build\RobWorkStudio\bin\Release\sdurws_ird_modeling_app.exe"
if (-not (Test-Path $exe)) {
    Write-Error "未找到 $exe ——请先执行: cmake --build build --config Release --target sdurws_ird_modeling_app"
    exit 1
}
$p = Start-Process -FilePath $exe -WorkingDirectory $runDir -PassThru -RedirectStandardOutput "$runDir\console-stdout.log" -RedirectStandardError "$runDir\console-stderr.log"
Write-Output ("PID=" + $p.Id)
Write-Output ("RUNDIR=" + $runDir)
