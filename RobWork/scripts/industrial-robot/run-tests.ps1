# 工业机械臂重构·统一测试入口（WP-01-T03）
# 职责：CTest 调用（默认 -j1 单进程）、-Regex 精确筛选（先以 ctest -N 预检，零匹配即
#       非零并报告）、GUI 规则（testing-contract §5、WP-01 计划 §6）：检测到继承
#       QT_*/QML_* 冲突变量先报告并停止；通过后为测试子进程强制 QT_QPA_PLATFORM=windows。
#       输出与退出码写入 <LogDirectory>\test.log 并透传；失败保留日志，不删除构建目录。
# 允许依赖：common.ps1、ctest.exe、VS x64 工具链；禁止：虚拟平台设置、多 GUI 进程并行、
#       自动修复源码或用户环境变量（强制 windows 平台为 testing-contract §5 冻结规则）。
# 公共所有者：WP-01；契约：work-packages/WP-01 §6；architecture/testing-contract.md §5。
# Task ID：WP-01-T03；运行环境：Windows PowerShell 5.1+（无 PS7 专属语法）、VS x64。
#
# ★ 2026-10-16 CI 激活适配（gov-ci-activation，所有者裁决"按建议激活"）：以 eb777e2
#   参考实现为基线做三处适配，GUI 规则面逐字保留：
#   1) 测试执行由"构建根单次 ctest -R"改为"叶子 CTestTestfile 发现＋逐目标 ctest"——
#      今日树 enable_testing 位于单元作用域、add_test 注册于叶子目录，构建根的 ctest
#      看不到叶子测试（8 月参考运行时 core_test 注册于根作用域故可见；与
#      gate-all.ps1「测试目标发现」同源结论）。零发现/零匹配语义保持"不猜测、
#      不降级"：发现 0 个或任一目标失败都非零退出。
#   2) 新增 -CTestTimeoutSec（默认 600 s）逐目标 ctest --timeout——F-728 防线对齐
#      gate-all：单测试挂住被有界终止，不再无限期阻塞整个 CI job（hosted runner
#      单 job 上限 6 小时，挂住即烧满）。
#   3) 新增依赖 DLL 运行时供给：CI 依赖经 vcpkg x64-windows（动态 triplet）装入
#      CMAKE_PREFIX_PATH 指向的树，测试可执行文件运行期需要 <前缀>\bin 下的
#      Qt6/boost/zlib 等 DLL；CI 无 vcpkg toolchain 的 applocal 部署，故把各前缀
#      bin 目录前置注入 PATH（仅当目录真实存在）。本地操作员环境已有等效供给时
#      此注入为幂等空操作。
#   4) 移植 gate-all.ps1 的 F-715 SKIP 口径并按七跑实证收窄：sdurws_ird_ui_test 经
#      框架 sdurws 静态库接口面链入 python313.dll（→zlib.dll 同目录相依），该 DLL
#      不随构建部署、只能由 PATH 供给；不可达时加载失败。分诊以输出内容为准：
#      ***Timeout（F-728 有界超时）与 [  FAILED  ]（gtest 断言失败）一律 FAIL；
#      仅"非超时、无 gtest 输出、0xC0000135 族（ctest 对测试失败一律返回 8，
#      run 38953336225 实证旧判据'退出码 8'会误放行任意失败）且 python313.dll
#      不可达"才记 SKIP＋归因不计失败。补 -PythonDir/SDURWS_IRD_PYTHON_DIR 即
#      可转真实执行。
#   登记依据：ci/README.md「2026-10-16 激活」节＋patches/PATCHES.md 0003 号。
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [string]$BuildDirectory = '',
    [string]$SourceDirectory = '',
    [string]$Generator = 'Visual Studio 17 2022',
    [string]$Platform = 'x64',
    [switch]$NoConfigure,
    [string]$LogDirectory = '',
    [Parameter(Mandatory = $true)]
    [string]$Regex,
    # 逐目标 ctest --timeout 秒数（F-728 防线，2026-10-16 激活适配②——见文件头说明）；
    # 默认 600 s 与 gate-all.ps1 同值：健康目标实测秒级完成，余量一个数量级以上
    [int]$CTestTimeoutSec = 600,
    # python313.dll 所在目录显式供给（F-715 口径，与 gate-all.ps1 -PythonDir 同名同义）；
    # 空串＝未传参，走探测序列（见下方探测块）
    [string]$PythonDir = ''
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'common.ps1')

$repoRoot = Get-IndustrialRobotRepoRoot -ScriptRoot $PSScriptRoot
$repoOuter = Split-Path -Parent $repoRoot

# ---- 日志目录先行：任何失败路径都必须保留 test.log ----
if ([string]::IsNullOrWhiteSpace($LogDirectory)) {
    $logDir = New-IndustrialRobotLogDir -RepoRoot $repoRoot -TimeStamp (Get-IndustrialRobotTimeStamp)
}
else {
    $logDir = New-IndustrialRobotOutputDirectory -Path $LogDirectory -Default '' -Label '-LogDirectory'
}
if ($null -eq $logDir -or $logDir -eq '') { exit 1 }
$logFile = Join-Path $logDir 'test.log'

Write-IndustrialRobotLog -LogFile $logFile -Line ('==== run-tests.ps1（WP-01-T03） ' + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss') + ' ====')
Write-IndustrialRobotLog -LogFile $logFile -Line ('[参数] Configuration=' + $Configuration + ' Regex=' + $Regex + ' NoConfigure=' + $NoConfigure.IsPresent)
if ($NoConfigure) {
    Write-IndustrialRobotLog -LogFile $logFile -Line '[参数] -NoConfigure：本入口只消费上一步构建工件，开关按公共参数契约接受，行为不变。'
}

# ---- GUI 规则：冲突变量先报告并停止（优先于一切输入校验与 ctest 调用）----
$conflicts = @(Get-IndustrialRobotQtEnvironmentConflicts)
if ($conflicts.Count -gt 0) {
    Write-IndustrialRobotLog -LogFile $logFile -Line '[失败] 检测到继承的 QT_*/QML_* 冲突环境变量（testing-contract §5），先报告并停止：'
    foreach ($conflict in $conflicts) {
        Write-IndustrialRobotLog -LogFile $logFile -Line ('  冲突变量: ' + $conflict)
        [Console]::Error.WriteLine('[GUI 规则] 冲突变量: ' + $conflict)
    }
    Write-IndustrialRobotLog -LogFile $logFile -Line '[修复动作] 清理上述继承变量后重跑；Windows 测试平台固定为 windows，禁止虚拟平台与多 GUI 进程。'
    [Console]::Error.WriteLine('[GUI 规则] 检测到继承 QT_*/QML_* 冲突变量，已先报告并停止；清理后重跑。')
    [Console]::Error.WriteLine('[结果] run-tests 失败，退出码 1；日志保留: ' + $logFile)
    exit 1
}
if ($env:QT_QPA_PLATFORM -ine 'windows') {
    $env:QT_QPA_PLATFORM = 'windows'
    Write-IndustrialRobotLog -LogFile $logFile -Line '[GUI 规则] 已为测试子进程强制 QT_QPA_PLATFORM=windows（testing-contract §5）。'
}
Write-IndustrialRobotLog -LogFile $logFile -Line '[GUI 规则] 单进程串行：ctest -j 1；一次只运行一个测试可执行文件。'

# ---- 依赖 DLL 运行时供给（2026-10-16 激活适配③——缘由见文件头说明）----
# 只前置注入真实存在的 <前缀>\bin；已含于 PATH 的前缀跳过（幂等）。
if ($env:CMAKE_PREFIX_PATH) {
    foreach ($prefix in ($env:CMAKE_PREFIX_PATH -split ';')) {
        if ([string]::IsNullOrWhiteSpace($prefix)) { continue }
        $bin = Join-Path $prefix.TrimEnd('\', '/') 'bin'
        if ((Test-Path -LiteralPath $bin) -and ($env:PATH -notlike ('*' + $bin + '*'))) {
            $env:PATH = $bin + ';' + $env:PATH
            Write-IndustrialRobotLog -LogFile $logFile -Line ('[DLL 供给] 已前置注入 PATH: ' + $bin)
        }
    }
}

# ---- python 运行库前置探测（2026-10-16 激活适配④，F-715 口径——缘由见文件头说明）----
# 探测顺序与 gate-all.ps1 第 0.5 步一致：-PythonDir 参数 > 环境变量 SDURWS_IRD_PYTHON_DIR
# > 既有 PATH > conda/python 命令定位反推 > 常见安装位；命中即注入 PATH。全程不可达
# 时不失败——后续测试步遇 0xC0000135 族失败按 SKIP＋归因处置（见执行循环）。
$script:PythonDll = $null
$pythonCandidates = @()
if ($PythonDir) { $pythonCandidates += $PythonDir }
if ($env:SDURWS_IRD_PYTHON_DIR) { $pythonCandidates += $env:SDURWS_IRD_PYTHON_DIR }
$pythonCandidates += @($env:PATH -split ';' | Where-Object { $_ })
$condaCmd = Get-Command conda -ErrorAction SilentlyContinue
if ($condaCmd -and $condaCmd.Source) { $pythonCandidates += (Split-Path -Parent (Split-Path -Parent $condaCmd.Source)) }
$pyCmd = Get-Command python -ErrorAction SilentlyContinue
if ($pyCmd -and $pyCmd.Source) { $pythonCandidates += (Split-Path -Parent $pyCmd.Source) }
$pythonCandidates += @(
    (Join-Path $env:USERPROFILE 'miniconda3'),
    (Join-Path $env:USERPROFILE 'anaconda3'),
    (Join-Path $env:LOCALAPPDATA 'Programs\Python\Python313'),
    'C:\Python313'
)
foreach ($candidate in $pythonCandidates) {
    if ($candidate -and (Test-Path (Join-Path $candidate 'python313.dll'))) { $script:PythonDll = $candidate; break }
}
if ($script:PythonDll) {
    if ($env:PATH -notlike "*$script:PythonDll*") { $env:PATH = "$script:PythonDll;$env:PATH" }
    Write-IndustrialRobotLog -LogFile $logFile -Line ('[python 前置] python313.dll 已可达: ' + $script:PythonDll + '（已注入 PATH——F-715/TK-T12）')
} else {
    Write-IndustrialRobotLog -LogFile $logFile -Line '[python 前置] python313.dll 不可达——链入 python 运行库的目标（今 sdurws_ird_ui_test）将以 SKIP＋归因记录（F-715 口径），不计失败'
}

# ---- 输入工件校验：构建目录必须已配置（每一步只消费上一步成功工件）----
$sourceDir = Resolve-IndustrialRobotSourceDirectory -Path $SourceDirectory -Default $repoRoot -Label '-SourceDirectory'
if ($null -eq $sourceDir) { exit 1 }
$buildDir = Resolve-IndustrialRobotSourceDirectory -Path $BuildDirectory -Default (Join-Path $repoOuter 'out\build\industrial-robot') -Label '-BuildDirectory'
if ($null -eq $buildDir) { exit 1 }
if (-not (Test-Path -LiteralPath (Join-Path $buildDir 'CMakeCache.txt'))) {
    [Console]::Error.WriteLine(('[入口参数] -BuildDirectory 不是已配置的构建目录（缺 CMakeCache.txt）: ' + $buildDir))
    [Console]::Error.WriteLine('[结果] run-tests 失败，退出码 1；日志保留: ' + $logFile)
    Write-IndustrialRobotLog -LogFile $logFile -Line ('[失败] -BuildDirectory 不是已配置的构建目录（缺 CMakeCache.txt）: ' + $buildDir)
    exit 1
}
Write-IndustrialRobotLog -LogFile $logFile -Line ('[路径] BuildDirectory=' + $buildDir)
Write-IndustrialRobotLog -LogFile $logFile -Line ('[路径] LogDirectory=' + $logDir)

# ---- VS x64 环境发现与版本记录（WP-01 计划 §5.2，与 configure/build 同一口径）----
Write-IndustrialRobotLog -LogFile $logFile -Line '---- VS x64 环境发现 ----'
$vs = Initialize-IndustrialRobotVsEnvironment -LogFile $logFile
if (-not $vs.Success) {
    Write-IndustrialRobotLog -LogFile $logFile -Line ('[失败] ' + $vs.Diagnostic)
    [Console]::Error.WriteLine($vs.Diagnostic)
    [Console]::Error.WriteLine('[结果] run-tests 失败，退出码 1；日志保留: ' + $logFile)
    exit 1
}
Write-IndustrialRobotLog -LogFile $logFile -Line ('[环境] 发现方式=' + $vs.Via)
Write-IndustrialRobotLog -LogFile $logFile -Line ('[环境] MSVC=' + $vs.MsvcLine + '；Windows SDK=' + $vs.SdkVersion)
Write-IndustrialRobotLog -LogFile $logFile -Line ('[环境] ' + $vs.CMakeLine + '；' + $vs.CTestLine)

$ctest = (Get-Command -Name 'ctest.exe' -ErrorAction SilentlyContinue).Source

# ---- 测试目标发现（2026-10-16 激活适配①）：叶子 CTestTestfile 扫描＋Regex 筛选 ----
# 算法与 gate-all.ps1 第 3 步同源（缘由见文件头说明①）。发现后按 -Regex 精确筛选，
# 零命中保持参考实现"不猜测、不降级"语义：非零退出并报告。
function Find-IndustrialRobotTestTargets([string]$Root) {
    $map = @{}
    if (-not (Test-Path -LiteralPath $Root)) { return $map }
    Get-ChildItem -Path $Root -Recurse -Filter 'CTestTestfile.cmake' -ErrorAction SilentlyContinue | ForEach-Object {
        $dir = $_.DirectoryName
        (Get-Content $_.FullName) | ForEach-Object {
            if ($_ -match 'add_test\s*\(\s*([A-Za-z0-9_\-]+)\s') {
                $map[$Matches[1]] = $dir   # 名→注册目录（ctest --test-dir 指向该叶子目录）
            }
        }
    }
    return $map
}

Write-IndustrialRobotLog -LogFile $logFile -Line '---- 测试目标发现（叶子 CTestTestfile 扫描）----'
$irdBuildRoot = Join-Path $buildDir 'RobWorkStudio\src\rwslibs\industrialrobot'
$allTargets = Find-IndustrialRobotTestTargets -Root $irdBuildRoot
if ($allTargets.Count -eq 0) {
    $diagnostic = '[失败] industrialrobot 构建子树未发现任何注册测试目标（' + $irdBuildRoot + '）' + `
                  '——最常见原因：配置未带 -DRWS_BUILD_INDUSTRIALROBOT=ON，子树未进构建图。不猜测，停止。'
    Write-IndustrialRobotLog -LogFile $logFile -Line $diagnostic
    [Console]::Error.WriteLine($diagnostic)
    [Console]::Error.WriteLine('[结果] run-tests 失败，退出码 1；日志保留: ' + $logFile)
    exit 1
}
$selected = @($allTargets.Keys | Where-Object { $_ -match $Regex } | Sort-Object)
if ($selected.Count -eq 0) {
    $diagnostic = '[失败] Regex 零匹配：-R "' + $Regex + '" 未命中任何已发现测试（共发现 ' + $allTargets.Count + ' 项）。'
    Write-IndustrialRobotLog -LogFile $logFile -Line $diagnostic
    [Console]::Error.WriteLine($diagnostic)
    [Console]::Error.WriteLine('[结果] run-tests 失败，退出码 1；日志保留: ' + $logFile)
    exit 1
}
Write-IndustrialRobotLog -LogFile $logFile -Line ('[预检] 发现 ' + $allTargets.Count + ' 个注册测试，Regex 命中 ' + $selected.Count + ' 个: ' + ($selected -join ', '))

# ---- CTest 执行：逐目标、单进程（-j1）、有界超时，任一失败聚合后非零退出 ----
# 每目标一次 ctest（--test-dir 指向注册叶子目录，-R 锚定全名），失败不中断后续
# 目标——收集全部失败再汇总（与 gate-all"收集全部失败再汇总"口径一致），便于
# CI 单轮拿到完整失败清单而不是逐轮试错。
Write-IndustrialRobotLog -LogFile $logFile -Line '---- CTest 执行（逐目标）----'
$failed = New-Object System.Collections.Generic.List[string]
$skipped = New-Object System.Collections.Generic.List[string]
foreach ($name in $selected) {
    $run = Invoke-IndustrialRobotNative -FilePath $ctest `
        -ArgumentList @('--test-dir', $allTargets[$name], '-C', $Configuration,
                        '-R', ('^' + $name + '$'), '-j', '1',
                        '--timeout', [string]$CTestTimeoutSec, '--output-on-failure') `
        -LogFile $logFile -Label ('ctest -R ^' + $name + '$（超时 ' + $CTestTimeoutSec + ' s）')
    if ($run.ExitCode -eq 0) { continue }

    # ---- 失败分诊（顺序不可换；七跑实证口径修正，见文件头说明④）----
    # 实证：ctest 对"测试失败"一律返回退出码 8（超时终止、gtest 断言失败、加载失败
    # 三者同码），"退出码 8"单独作为 F-715 判据会把任意失败误归因 SKIP（run
    # 38953336225 三个 _gui_test 超时＋一个 perf 断言失败被误放行）。分诊以输出
    # 内容为准：
    $joinedOutput = ($run.Lines -join "`n")
    # ① F-728 超时优先：输出含 ***Timeout ＝ 挂住被有界终止——FAIL，不静默放行
    #    （gate-all 口径：ctest 是规范执行通道，其超时须排查闭环；CI 挂住多属
    #    环境差异如无交互桌面，须定位而非归因 python）
    if ($joinedOutput -match '\*\*\*Timeout') {
        $failed.Add($name)
        Write-IndustrialRobotLog -LogFile $logFile -Line ('[FAIL] ' + $name + '（ctest 超时 ' + $CTestTimeoutSec + ' s，挂住已终止——F-728 防线；排查 CI 环境挂起原因，勿归因 python 缺失）')
        continue
    }
    # ② gtest 实际执行失败的标志：[  FAILED  ] 横幅 ＝ 进程已启动、断言失败——FAIL
    if ($joinedOutput -match '\[\s+FAILED\s+\]') {
        $failed.Add($name)
        Write-IndustrialRobotLog -LogFile $logFile -Line ('[FAIL] ' + $name + '（gtest 断言失败——进程已实际执行，非环境前置缺失）')
        continue
    }
    # ③ F-715 SKIP（窄口径）：非超时、无 gtest 失败输出、0xC0000135 族退出码且
    #    python313.dll 探测不可达——进程根本没启动的加载失败，环境前置缺失不是
    #    被测面缺陷，记 SKIP＋归因不计失败（gate-all Invoke-GateTest 本意口径）
    if (-not $script:PythonDll -and ($run.ExitCode -eq 8 -or $run.ExitCode -eq -1073741515)) {
        $skipped.Add($name)
        Write-IndustrialRobotLog -LogFile $logFile -Line ('[SKIP] ' + $name + '（无 gtest 输出的加载失败 0xC0000135 族且 python313.dll 不可达——F-715 归因，不计失败）')
        continue
    }
    # ④ 其余一律 FAIL（退出码透传定位）
    $failed.Add($name)
    Write-IndustrialRobotLog -LogFile $logFile -Line ('[FAIL] ' + $name + '（退出码 ' + $run.ExitCode + '）')
}
if ($skipped.Count -gt 0) {
    Write-IndustrialRobotLog -LogFile $logFile -Line ('[结果] SKIP ' + $skipped.Count + ' 个: ' + ($skipped -join ', ') + '（F-715；补 -PythonDir/SDURWS_IRD_PYTHON_DIR 后重跑可转真实执行）')
}
if ($failed.Count -gt 0) {
    Write-IndustrialRobotLog -LogFile $logFile -Line ('[结果] 失败 ' + $failed.Count + '/' + $selected.Count + ': ' + ($failed -join ', '))
    [Console]::Error.WriteLine('[结果] run-tests 失败 ' + $failed.Count + '/' + $selected.Count + ' 个目标: ' + ($failed -join ', '))
    [Console]::Error.WriteLine('[结果] run-tests 退出码 1；日志保留: ' + $logFile)
    exit 1
}
$passSummary = '全部 ' + ($selected.Count - $skipped.Count) + ' 个目标通过'
if ($skipped.Count -gt 0) { $passSummary += '（另 SKIP ' + $skipped.Count + ' 个，F-715 归因）' }
Write-IndustrialRobotLog -LogFile $logFile -Line ('[结果] ' + $passSummary + '；日志: ' + $logFile)
exit 0
