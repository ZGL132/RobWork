# =====================================================================
# run-gate-all.ps1 —— F-628 批次（gate-toolchain-param）gate-all 全量实测驱动脚本
#
# 用途：在无 junction 的独立工作树 gate-tc-work 中，以 -Toolchain 直引主仓
#       vcpkg 跑 gate-all 全量（集成配置＋ird_gates＋集成逐目标构建测试＋
#       冒烟配置构建测试）——验证 gate-all 新增 -Toolchain 参数可从根上
#       消除 worktree 对 junction 供给的依赖（findings F-628 验证口径）。
# 记录：启动时间戳（ISO 8601）与总耗时（分钟）随日志落盘，退出码原样传播
#       供调用方判定（0＝门禁全过）。
#
# fresh 树操作员前置（DTB §5.1 ①/③ 登记口径，与本脚本运行环境的关系）：
#   ① 框架 DLL＝构建 sdurws_ird_studio 后由其 POST_BUILD 复制到
#      build/RobWorkStudio/bin/Release（ui_test 等与 studio exe 同目录的
#      测试 exe 运行期解析）——本批由 studio-build.log 记录显式 --target 构建。
#   ③ python313.dll＝经系统 Miniconda PATH 解析（ui_test 依赖链）——
#      本脚本把 D:/software/miniconda3 前置进本进程 PATH（仅进程内生效，
#      不改机器环境）。
# =====================================================================
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
# DTB §5.1 ③：ui_test 链上的 python313.dll 经系统 Miniconda 解析——进程 PATH 前置
$env:PATH = 'D:/software/miniconda3;' + $env:PATH
Write-Host ("GATE_PYTHON313_CHECK=" + (Test-Path 'D:/software/miniconda3/python313.dll'))
$sw = [System.Diagnostics.Stopwatch]::StartNew()
Write-Host ("GATE_START_ISO=" + (Get-Date).ToString('o'))
Write-Host ("GATE_WORKTREE=C:/Users/zgl18/AppData/Local/Temp/gate-tc-work")
Write-Host ("GATE_JUNCTION_CHECK=vcpkg dir absent expected (no junction supplied)")
& 'C:/Users/zgl18/AppData/Local/Temp/gate-tc-work/RobWork/scripts/industrialrobot/gate-all.ps1' `
    -BuildDir  'C:/Users/zgl18/AppData/Local/Temp/gate-tc-work/build' `
    -SmokeDir  'C:/Users/zgl18/AppData/Local/Temp/gate-tc-work/smoke' `
    -QtPrefix  'D:/software/Qt/6.11.1/msvc2022_64' `
    -Toolchain 'D:/10_Source_Repos/21_robot/RobWork/vcpkg/scripts/buildsystems/vcpkg.cmake'
$ec = $LASTEXITCODE
$sw.Stop()
Write-Host ("GATE_EXIT=" + $ec)
Write-Host ("GATE_ELAPSED_MIN=" + [math]::Round($sw.Elapsed.TotalMinutes, 1))
Write-Host ("GATE_END_ISO=" + (Get-Date).ToString('o'))
exit $ec
