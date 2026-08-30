# Shared helpers for industrial-robot PowerShell scripts (WP-01-T01).
# Scope limited to path resolution and logging per the task card; build/package
# data flow helpers arrive with WP-01-T02/T03.

function Get-IndustrialRobotRepoRoot {
    # Scripts live at <repo>\RobWork\scripts\industrial-robot; the repository root
    # is two levels up so that every script resolves paths identically.
    param([string]$ScriptRoot)
    return (Resolve-Path -LiteralPath (Join-Path $ScriptRoot '..\..')).Path
}

function New-IndustrialRobotLogDir {
    # Create out/logs/industrial-robot/<timestamp>/ and return its full path.
    # 运行日志与 out/test-evidence 同级，位于外层仓库根（内层 RobWork/out 被 .gitignore 忽略，
    # 证据必须可提交供独立验证复查）。
    param([string]$RepoRoot, [string]$TimeStamp)
    $evidenceRoot = Split-Path -Parent $RepoRoot
    $dir = Join-Path $evidenceRoot ("out\logs\industrial-robot\" + $TimeStamp)
    New-Item -ItemType Directory -Path $dir -Force | Out-Null
    return $dir
}

function Write-IndustrialRobotLog {
    param([string]$LogFile, [string]$Line)
    Add-Content -LiteralPath $LogFile -Value $Line -Encoding UTF8
}
