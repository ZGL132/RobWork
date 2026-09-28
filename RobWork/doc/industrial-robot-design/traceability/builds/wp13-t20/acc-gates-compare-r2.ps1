# 验收侧 ird_gates base↔head 归一化对比（WP-13-T20 attempt 2）
# 归一化口径：取 [ird_gates] IRD-GATE- 行，去前缀，源码绝对路径前缀统一替换为 IRD:/
param()
$ErrorActionPreference = 'Stop'
function Norm([string]$path) {
    Get-Content $path |
        Where-Object { $_ -match '\[ird_gates\] IRD-GATE-' } |
        ForEach-Object {
            $line = $_ -replace '^\[ird_gates\] ', ''
            $line = $line -replace 'D:/10_Source_Repos/21_robot/\S*?/industrialrobot/', 'IRD:/'
            $line
        }
}
$base = Norm 'D:/10_Source_Repos/21_robot/acc-gates-base-r2.log'
$head = Norm 'D:/10_Source_Repos/21_robot/acc-gates-head-r2.log'
"base hits: $($base.Count)  head hits: $($head.Count)"
$bs = $base | Sort-Object -Unique
$hs = $head | Sort-Object -Unique
"base unique: $($bs.Count)  head unique: $($hs.Count)"
$onlyB = @($bs | Where-Object { $hs -notcontains $_ })
$onlyH = @($hs | Where-Object { $bs -notcontains $_ })
"only-in-base: $($onlyB.Count)"
$onlyB | Select-Object -First 5
"only-in-head: $($onlyH.Count)"
$onlyH | Select-Object -First 5
