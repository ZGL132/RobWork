# =====================================================================
# gate-all.ps1 —— 本地一键门禁（WP-01-T02，契约 tasks/WP-01-T02.json）
#
# 设计依据：
#   - ARCHITECTURE.md §11.2 第 2 条（构建红线门禁随首批源码启用、CI 常驻）
#     与 §11.2 第 9 条（单元依赖表＝R-1/R-2 门禁白名单数据源）
#   - development-task-breakdown.md §5.5（gtest 接入定稿：vcpkg＋
#     find_package(GTest CONFIG REQUIRED)）与 §5.1（双模式构建约定）
#   - 任务契约 acceptance 第 1 条：ird_gates＋双模式构建＋全部
#     _test/_contract_test 目标一键执行；CI 建成前本脚本为强制门槛
#
# 零 Python 约束（契约 acceptance 第 2 条）：本脚本只用 pwsh＋cmake＋ctest＋git。
#
# 用法（仓库根或任意子目录）：
#   pwsh -File RobWork/scripts/industrialrobot/gate-all.ps1
#   可选参数：
#     -BuildDir <路径>    集成模式构建树（默认 <仓库根>/build）
#     -SmokeDir <路径>    冒烟模式临时构建目录（默认 $env:TEMP/ird-gate-smoke；
#                         成功后删除，失败保留现场供排查）
#     -QtPrefix <路径>    仅当集成树需要重新配置时使用（默认取环境变量
#                         CMAKE_PREFIX_PATH；全新树配置口径见 findings F-007：
#                         需 vcpkg toolchain＋Qt 前缀＋双 ini 模板，模板供给
#                         已由 ci/ 目录的 CI 模板内嵌，本地全新树需操作员补齐）
#
# 退出码：0＝全部门禁与测试通过；非 0＝存在失败项（逐项输出定位）。
# =====================================================================
[CmdletBinding()]
param(
    [string]$BuildDir = "",
    [string]$SmokeDir = "",
    [string]$QtPrefix = ""
)
$ErrorActionPreference = 'Continue'   # 门禁脚本要收集全部失败再汇总，不在第一个错误中断

# ---- 仓库根三段式自检（与 validate-docs 等治理脚本同口径，findings F-001 修复） ----
$RepoRoot = if (Test-Path variable:RepoRootOverride) { $RepoRootOverride }
            elseif ($PSScriptRoot) { Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) }
            else { (Get-Location).Path }
if (-not (Test-Path "$RepoRoot/RobWork/scripts/industrialrobot")) {
    # $PSScriptRoot 归约失败（罕见调用形态）时按当前目录向上探测
    $probe = (Get-Location).Path
    while ($probe -and -not (Test-Path "$probe/RobWork/scripts/industrialrobot")) {
        $probe = Split-Path -Parent $probe
    }
    if (-not $probe) { Write-Error "无法定位仓库根（缺少 RobWork/scripts/industrialrobot）"; exit 2 }
    $RepoRoot = $probe
}
if (-not $BuildDir) { $BuildDir = "$RepoRoot/build" }
$IndustrialSrc = "$RepoRoot/RobWork/RobWorkStudio/src/rwslibs/industrialrobot"
$Toolchain = "$RepoRoot/vcpkg/scripts/buildsystems/vcpkg.cmake"

# 门禁结果收集：每步一行结论，最后汇总（结论二元可判——契约 verify 口径）
$script:Results = [System.Collections.Generic.List[string]]::new()
$script:Failed = 0
function Step([string]$Name, [scriptblock]$Body) {
    Write-Host "`n=== [gate] $Name ===" -ForegroundColor Cyan
    & $Body
    if ($LASTEXITCODE -ne 0 -or $?) {
        # 上一命令组内已显式登记失败则不在此重复计数；此处只看脚本级约定：
        # 各步内部失败时调用 Fail 并返回 1
    }
}
function Ok([string]$Name)   { $script:Results.Add("PASS  $Name"); Write-Host "  -> PASS" -ForegroundColor Green }
function Fail([string]$Name, [string]$Detail) {
    $script:Results.Add("FAIL  $Name : $Detail"); $script:Failed++
    Write-Host "  -> FAIL : $Detail" -ForegroundColor Red
}

# ---- 第 0 步：环境自检（守卫 7 口径） ----
if (-not (Test-Path $Toolchain)) { Fail "环境自检" "vcpkg toolchain 不存在: $Toolchain" }

# ---- 第 1 步：集成构建树确认/配置 ----
$cacheFile = "$BuildDir/CMakeCache.txt"
$needConf = -not (Test-Path $cacheFile)
if (-not $needConf) {
    $flag = (Select-String -Path $cacheFile -Pattern 'RWS_BUILD_INDUSTRIALROBOT:BOOL=ON' -ErrorAction SilentlyContinue)
    if (-not $flag) { $needConf = $true }
}
if ($needConf) {
    # 全新树配置口径（findings F-007）：toolchain 必带；Qt 前缀取参数/环境变量
    $qt = $QtPrefix; if (-not $qt) { $qt = $env:CMAKE_PREFIX_PATH }
    $args_ = @('-S', "$RepoRoot/RobWork", '-B', $BuildDir, '-G', 'Visual Studio 17 2022', '-A', 'x64',
               '-DRWS_BUILD_INDUSTRIALROBOT=ON',
               "-DCMAKE_TOOLCHAIN_FILE=$Toolchain")
    if ($qt) { $args_ += "-DCMAKE_PREFIX_PATH=$qt" }
    & cmake @args_ | Out-Null
    if ($LASTEXITCODE -ne 0) { Fail "集成树配置" "cmake 配置失败（全新树口径见 F-007：Qt 前缀/ini 模板）"; }
    else { Ok "集成树配置（RWS_BUILD_INDUSTRIALROBOT=ON）" }
} else { Ok "集成树缓存确认（RWS_BUILD_INDUSTRIALROBOT=ON 已在位）" }

# ---- 第 2 步：ird_gates 依赖红线门禁（WP-01-T01 交付的构建期门禁） ----
& cmake --build $BuildDir --config Release --target ird_gates 2>&1 | Select-Object -Last 5 | Write-Host
if ($LASTEXITCODE -eq 0) { Ok "ird_gates（依赖红线/补丁门禁）" } else { Fail "ird_gates" "门禁目标构建失败（退出码 $LASTEXITCODE）" }

# ---- 测试目标发现：递归解析 industrialrobot 构建子树的 CTestTestfile ----
# 设计说明：add_test 由各单元 ird_add_gtest 注册于单元构建目录（叶子），
# 父目录 CTest 链不传递（enable_testing 位于单元作用域），因此以文件扫描
# 发现全部 _test/_contract_test 注册名——与目标名一致（add_test NAME=目标名）。
function Find-TestTargets([string]$Root) {
    $map = @{}
    if (-not (Test-Path $Root)) { return $map }
    Get-ChildItem -Path $Root -Recurse -Filter 'CTestTestfile.cmake' -ErrorAction SilentlyContinue | ForEach-Object {
        $dir = $_.DirectoryName
        (Get-Content $_.FullName) | ForEach-Object {
            if ($_ -match 'add_test\s*\(\s*([A-Za-z0-9_\-]+)\s') {
                $map[$Matches[1]] = $dir   # 名→注册目录（运行时 ctest 指定该目录）
            }
        }
    }
    return $map
}

# ---- 第 3 步：集成模式——逐测试目标构建＋执行 ----
$intTests = Find-TestTargets "$BuildDir/RobWorkStudio/src/rwslibs/industrialrobot"
if ($intTests.Count -eq 0) {
    Fail "集成模式测试发现" "CTestTestfile 扫描为空（构建子树损坏或未配置）"
} else {
    Ok "集成模式测试发现（$($intTests.Count) 个注册: $(($intTests.Keys | Sort-Object) -join ', ')）"
    foreach ($name in ($intTests.Keys | Sort-Object)) {
        & cmake --build $BuildDir --config Release --target $name 2>&1 | Select-Object -Last 2 | Write-Host
        if ($LASTEXITCODE -ne 0) { Fail "集成构建 $name" "目标构建失败"; continue }
        & ctest --test-dir $intTests[$name] -C Release -R "^$name`$" --output-on-failure 2>&1 | Select-Object -Last 4 | Write-Host
        if ($LASTEXITCODE -eq 0) { Ok "集成测试 $name" } else { Fail "集成测试 $name" "ctest 失败（退出码 $LASTEXITCODE）" }
    }
}

# ---- 第 4 步：冒烟模式——独立配置（带 toolchain，AGENTS §4.1/F-002 口径）＋构建＋测试 ----
if (-not $SmokeDir) { $SmokeDir = Join-Path $env:TEMP 'ird-gate-smoke' }
if (Test-Path $SmokeDir) { Remove-Item -Recurse -Force $SmokeDir -ErrorAction SilentlyContinue }
& cmake -S $IndustrialSrc -B $SmokeDir -G 'Visual Studio 17 2022' -A x64 "-DCMAKE_TOOLCHAIN_FILE=$Toolchain" | Out-Null
if ($LASTEXITCODE -ne 0) {
    Fail "冒烟树配置" "独立冒烟配置失败（必须带 vcpkg toolchain——DTB §5.5/F-002）"
} else {
    Ok "冒烟树配置（standalone smoke，toolchain 在位）"
    $smokeTests = Find-TestTargets "$SmokeDir"
    if ($smokeTests.Count -eq 0) {
        Fail "冒烟模式测试发现" "CTestTestfile 扫描为空"
    } else {
        Ok "冒烟模式测试发现（$($smokeTests.Count) 个注册）"
        foreach ($name in ($smokeTests.Keys | Sort-Object)) {
            & cmake --build $SmokeDir --config Release --target $name 2>&1 | Select-Object -Last 2 | Write-Host
            if ($LASTEXITCODE -ne 0) { Fail "冒烟构建 $name" "目标构建失败"; continue }
            & ctest --test-dir $smokeTests[$name] -C Release -R "^$name`$" --output-on-failure 2>&1 | Select-Object -Last 4 | Write-Host
            if ($LASTEXITCODE -eq 0) { Ok "冒烟测试 $name" } else { Fail "冒烟测试 $name" "ctest 失败（退出码 $LASTEXITCODE）" }
        }
    }
    # 全绿才清理临时目录；失败保留现场（排查冒烟配置差异）
    if ($script:Failed -eq 0) { Remove-Item -Recurse -Force $SmokeDir -ErrorAction SilentlyContinue }
    else { Write-Host "冒烟现场保留: $SmokeDir" -ForegroundColor Yellow }
}

# ---- 汇总（结论二元：exit 0/1） ----
Write-Host "`n================ gate-all 汇总 ================" -ForegroundColor Cyan
$script:Results | ForEach-Object { Write-Host $_ }
Write-Host "================================================"
$total = $script:Results.Count; $pass = $total - $script:Failed
Write-Host "结果: $pass/$total 通过"
if ($script:Failed -gt 0) { Write-Host "存在失败项——本地一键门槛未过，禁止提交（契约 acceptance 1）" -ForegroundColor Red; exit 1 }
exit 0
