# 归一化 ird_gates 命中行（WP-13-T20 口径：行内多命中拆分＋扫描根剥离＋排序去重）。
# 用法：pwsh -File gate_normalize.ps1 <raw 输入> <hitset 输出>
param(
    [Parameter(Mandatory=$true)][string]$Src,
    [Parameter(Mandatory=$true)][string]$Dst
)
$ErrorActionPreference = 'Stop'
$lines = Get-Content -Encoding UTF8 $Src
$hits = New-Object System.Collections.Generic.HashSet[string]
foreach ($ln in $lines) {
    $matches = [regex]::Matches($ln, 'IRD-GATE-(LIB|R1|R3|R4|R5|SUB|T1|T2)[^:：]*[:：]\s*(.+)')
    foreach ($m in $matches) {
        $code = $m.Groups[1].Value
        $rest = $m.Groups[2].Value
        $parts = $rest -split '\s{2,}|,|；|;'
        foreach ($p in $parts) {
            $t = $p.Trim()
            if ($t -eq '') { continue }
            $t = $t -replace '^.*industrialrobot[\\/]', ''
            [void]$hits.Add("$code|$t")
        }
    }
}
$sorted = $hits | Sort-Object
Set-Content -Encoding UTF8 -Path $Dst -Value $sorted
Write-Output ("hits: " + $hits.Count)
