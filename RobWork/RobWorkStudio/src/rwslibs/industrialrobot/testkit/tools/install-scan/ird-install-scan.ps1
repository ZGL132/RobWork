# =====================================================================
# ird-install-scan.ps1 —— 安装树分发红线扫描（建议稿）
#
# 设计依据：
#   - units/testkit.md §3.6（安装/打包排除：industrialrobot 安装规则不得包含
#     sdurws_ird_testkit* 目标、testdata/、tools/testdata；CI 安装树扫描断言
#     零命中——脚本随 TK-T10 交付建议，门禁实施归 WP-01/WP-24）
#   - 需求 NFR-SEC-05（依赖清单不因测试目标膨胀——测试设施不入产品分发面）
#   - 任务契约 tasks/foundation/TK-T10.json acceptance③（本文件即"扫描脚本
#     建议交付物"）
#
# 定位：**建议稿**——脚本体随 testkit 交付留痕，正式门禁（CI 步骤接入、
# 安装规则约束）由 WP-01/WP-24 按各自构建体系裁决落位；本脚本不进入任何
# 产品安装包（自证：脚本自身位于 testkit/tools/ 下，同属排除对象）。
#
# 用法：
#   pwsh -File ird-install-scan.ps1 -InstallRoot <安装树根目录>
# 退出码：0＝零命中（分发红线合规）；1＝发现违禁内容；2＝参数/环境错误。
# =====================================================================

param(
    [Parameter(Mandatory = $true)]
    [string]$InstallRoot          # 待扫描的产品安装树根目录（绝对或相对路径）
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $InstallRoot -PathType Container)) {
    [Console]::Error.WriteLine("安装树根目录不存在或不是目录: $InstallRoot")
    exit 2
}

# ---- 排除清单（§3.6 原文三项；正则大小写不敏感，覆盖目标名/目录名两种形态）----
$forbiddenPatterns = @(
    '(^|/)(lib)?sdurws_ird_testkit([_.][^/]*)?$', # 库/测试/工具产物名
    '(^|/)testdata(/|$)',                       # 黄金数据或工具目录组件
    '(^|/)include/sdurws/ird/testkit(/|$)'       # 公共测试头同属禁止分发面
)

# 扫描范围：安装树内全部文件与目录名（内容扫描不在此列——分发红线针对
# "产物存在性"，文件内容审读归代码审查）。
$hits = New-Object System.Collections.Generic.List[string]

try {
$scanRoot = (Get-Item -LiteralPath $InstallRoot).FullName
Get-ChildItem -LiteralPath $scanRoot -Recurse -Force | ForEach-Object {
    $relative = [IO.Path]::GetRelativePath($scanRoot, $_.FullName).Replace('\', '/')
    foreach ($pattern in $forbiddenPatterns) {
        if ($relative -match $pattern) {
            $hits.Add("$($_.FullName)  [命中: $pattern]") | Out-Null
        }
    }
}
} catch {
    [Console]::Error.WriteLine("安装树扫描失败: $($_.Exception.Message)")
    exit 2
}

# ---- 结论输出（机器可判读：命中清单逐行 + 汇总行）----
if ($hits.Count -gt 0) {
    Write-Output "ird-install-scan: FAIL（$($hits.Count) 处命中——分发红线违规）"
    $hits | ForEach-Object { Write-Output "  HIT  $_" }
    exit 1
}

Write-Output "ird-install-scan: PASS（安装树零命中——testkit/testdata/tools 未混入产品分发面）"
exit 0
