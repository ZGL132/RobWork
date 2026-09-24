# ird_gates 命中集归一化（UI-T18 留痕辅助；沿 v1.13-r2 登记的规则口径）：
#   UTF-8 读入 → 抽取含 IRD-GATE 的行 → 行内多命中拆分 → 截掉回显前缀噪声 →
#   扫描根剥离 → 折叠空白 → 排序去重。
# 用法：pwsh -File normalize-gate-log.ps1 -Log <gate 日志> -ScanRoot <worktree 根> -Out <输出文件>
param(
    [Parameter(Mandatory = $true)][string]$Log,
    [Parameter(Mandatory = $true)][string]$ScanRoot,
    [Parameter(Mandatory = $true)][string]$Out
)
$lines = [System.IO.File]::ReadAllLines($Log, [System.Text.Encoding]::UTF8)
$hits = New-Object System.Collections.Generic.HashSet[string]
foreach ($line in $lines) {
    if ($line -notmatch 'IRD-GATE-') { continue }
    foreach ($piece in ($line -split '(?=IRD-GATE-)')) {
        $p = $piece.Trim()
        if (-not $p.StartsWith('IRD-GATE-')) { continue }
        $idx = $p.IndexOf('[ird_gates]')
        if ($idx -gt 0) { $p = $p.Substring(0, $idx).TrimEnd() }
        $p = $p.TrimEnd('-', ' ')
        $p = $p.Replace($ScanRoot, '<ROOT>').Replace('\', '/')
        $p = ($p -split '\s+') -join ' '
        if ($p.Length -gt 0) { [void]$hits.Add($p) }
    }
}
$sorted = $hits | Sort-Object
$sorted | Set-Content -Path $Out -Encoding UTF8
Write-Output ("unique hits: " + $sorted.Count + " -> " + $Out)
