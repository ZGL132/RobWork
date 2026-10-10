# =====================================================================
# acc-run.ps1 —— 验收者独立复现驱动（acc/gate-toolchain-param/1）
# 无 junction 前提自检＋gate-all 全量＋时间戳/耗时落盘，退出码原样传播。
# =====================================================================
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
# DTB §5.1 ③：ui_test 链上的 python313.dll 经系统 Miniconda 解析——进程 PATH 前置
$env:PATH = 'D:/software/Miniconda3;' + $env:PATH
Write-Host ("ACC_PYTHON313_CHECK=" + (Test-Path 'D:/software/Miniconda3/python313.dll'))
# 本批验证主旨：复现树内必须无 junction、无 vcpkg 目录
Write-Host ("ACC_VCPKG_DIR_PRESENT(expect False)=" + (Test-Path 'C:/Users/zgl18/AppData/Local/Temp/acc-gtp-repro/vcpkg'))
Write-Host ("ACC_MAIN_VCPKG_TOOLCHAIN_PRESENT(expect True)=" + (Test-Path 'D:/10_Source_Repos/21_robot/RobWork/vcpkg/scripts/buildsystems/vcpkg.cmake'))
$sw = [System.Diagnostics.Stopwatch]::StartNew()
Write-Host ("ACC_START_ISO=" + (Get-Date).ToString('o'))
& 'C:/Users/zgl18/AppData/Local/Temp/acc-gtp-repro/RobWork/scripts/industrialrobot/gate-all.ps1' `
    -BuildDir  'C:/Users/zgl18/AppData/Local/Temp/acc-gtp-repro/build' `
    -SmokeDir  'C:/Users/zgl18/AppData/Local/Temp/acc-gtp-repro/smoke' `
    -QtPrefix  'D:/software/Qt/6.11.1/msvc2022_64' `
    -Toolchain 'D:/10_Source_Repos/21_robot/RobWork/vcpkg/scripts/buildsystems/vcpkg.cmake'
$ec = $LASTEXITCODE
$sw.Stop()
Write-Host ("ACC_EXIT=" + $ec)
Write-Host ("ACC_ELAPSED_MIN=" + [math]::Round($sw.Elapsed.TotalMinutes, 1))
Write-Host ("ACC_END_ISO=" + (Get-Date).ToString('o'))
exit $ec
