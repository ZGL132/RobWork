# =====================================================================
# smoke-config-evidence-probe.ps1 —— F-628 批次冒烟配置行证据探针
#
# 背景：gate-all.ps1 第 4 步的冒烟配置命令输出经 | Out-Null 吞掉，且全绿后
#       冒烟目录被清理（failing 时才保留现场）——门禁日志中没有配置命令行
#       与 CMakeCache 落痕。本探针在**门禁全绿之后**、于工作树之外独立复现
#       与 gate-all.ps1 冒烟配置完全相同的命令行（同一 -S/-B/-G/-A 参数、
#       同一 -Toolchain 值、同一 -DCMAKE_PREFIX_PATH 值），把配置输出与
#       CMakeCache 中记录的 toolchain 路径留痕于此，证明「冒烟配置吃到的
#       toolchain 仅来自 -Toolchain 参数」（工作树无 vcpkg/、无 junction，
#       无其他供给可能）。
# 口径：本探针为复现证据（reproduction probe），与 run 3 门禁日志中的
#       「冒烟树配置 PASS」步互为印证——后者证明在无 junction 工作树内
#       冒烟配置成功只能依赖参数供给的 toolchain。
# =====================================================================
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$IndustrialSrc = 'C:/Users/zgl18/AppData/Local/Temp/gate-tc-work/RobWork/RobWorkStudio/src/rwslibs/industrialrobot'
$ProbeDir      = 'C:/Users/zgl18/AppData/Local/Temp/gate-tc-smoke-probe'
$Toolchain     = 'D:/10_Source_Repos/21_robot/RobWork/vcpkg/scripts/buildsystems/vcpkg.cmake'
$QtPrefix      = 'D:/software/Qt/6.11.1/msvc2022_64'

Write-Host ("PROBE_START_ISO=" + (Get-Date).ToString('o'))
Write-Host ("命令行（与 gate-all.ps1 冒烟配置步一致）：")
Write-Host ("  cmake -S $IndustrialSrc -B $ProbeDir -G 'Visual Studio 17 2022' -A x64 -DCMAKE_TOOLCHAIN_FILE=$Toolchain -DCMAKE_PREFIX_PATH=$QtPrefix")

& cmake -S $IndustrialSrc -B $ProbeDir -G 'Visual Studio 17 2022' -A x64 `
        "-DCMAKE_TOOLCHAIN_FILE=$Toolchain" "-DCMAKE_PREFIX_PATH=$QtPrefix"
Write-Host ("PROBE_CONFIG_EXIT=" + $LASTEXITCODE)

Write-Host "`nCMakeCache 记录的 toolchain："
Select-String -Path "$ProbeDir/CMakeCache.txt" -Pattern 'CMAKE_TOOLCHAIN_FILE' |
    ForEach-Object { Write-Host ("  " + $_.Line) }
Write-Host ("PROBE_END_ISO=" + (Get-Date).ToString('o'))
exit $LASTEXITCODE
