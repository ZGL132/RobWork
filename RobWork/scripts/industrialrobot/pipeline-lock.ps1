# 流水线 tick 原子锁（治理脚本，PIPE v1.4 §0.1）。
#
# 职责：把"同一时刻至多一个 tick"从约定变成原子事实——state.json 的普通读写不是原子操作，
# 两个定时实例可同时读到 idle 后各自派工（审核结论 P0-1）。本脚本用 .NET FileStream 的
# CreateNew 模式（Windows O_EXCL 语义）实现原子 test-and-set：文件已存在即抛异常，
# 不存在则独占创建成功——创建与检查是一个不可分割的系统调用，无双读窗口。
#
# 锁文件位置：<repo>/.git/ird-pipeline-tick.lock——放 .git 内部：天然不进 git status、
# 不污染工作树"脏"判定，且生命周期与仓库绑定（clone 后自然不存在）。
#
# 租约语义：锁内容含 acquiredAt/leaseExpiresAt。acquire 遇到已有锁时：
#   - 未到期 → 输出持有者信息并以退出码 3 退出（本次 tick 放弃，只汇报）；
#   - 已过期（陈锁：持有它的 tick 会话已死亡）→ 把陈锁改名 .stale-<时间戳> 留证后接管；
#   - 内容损坏（无法解析）→ 同陈锁处理。
# release 删除锁文件（幂等）；status 只读报告。
#
# 用法：
#   pwsh -File pipeline-lock.ps1 -Action acquire -LeaseMinutes 30   # tick 开始
#   pwsh -File pipeline-lock.ps1 -Action release                    # tick 结束（异常路径也必须执行）
#   pwsh -File pipeline-lock.ps1 -Action status                     # 只读查询
# 输出：单行 JSON（{"ok":true,...}）；acquire 冲突时退出码 3，其余失败退出码 1。
param(
  [Parameter(Mandatory=$true)][ValidateSet("acquire","release","status")][string]$Action,
  [int]$LeaseMinutes = 30,
  [string]$RepoRoot = ""
)
if ($LeaseMinutes -lt 1) { throw "LeaseMinutes must be >= 1" }
$scriptDir = $PSScriptRoot
if (-not $scriptDir -or -not (Test-Path $scriptDir)) { $scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path }
if (-not $RepoRoot) { $RepoRoot = (Resolve-Path (Join-Path $scriptDir "..\..\..")).Path }
if (-not (Test-Path (Join-Path $RepoRoot ".git"))) { throw "repo root resolution failed: .git not found under '$RepoRoot'" }
$lockPath = Join-Path $RepoRoot ".git/ird-pipeline-tick.lock"
$fmt = "yyyy-MM-ddTHH:mm:sszzz"   # 与 state.json ISO 口径一致（秒精度＋本地偏移）

function Read-Lock([string]$path) {
  # pwsh7 的 ConvertFrom-Json 会把秒精度 ISO 解析为 DateTime——统一归一化回字符串，
  # 保证输出与比较在 pwsh7/PS5.1 两种 shell 下形态一致
  try {
    $l = Get-Content $path -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($null -ne $l) {
      foreach ($k in @("acquiredAt","leaseExpiresAt")) {
        if ($l.$k -is [datetime]) { $l.$k = $l.$k.ToString("yyyy-MM-ddTHH:mm:sszzz") }
      }
    }
    return $l
  } catch { return $null }
}

if ($Action -eq "status") {
  if (-not (Test-Path $lockPath)) { Write-Output '{"ok":true,"state":"none"}'; exit 0 }
  $l = Read-Lock $lockPath
  if ($null -eq $l) { Write-Output '{"ok":true,"state":"corrupt"}'; exit 0 }
  $now = [DateTime]::Now
  $exp = [DateTime]::Parse("$($l.leaseExpiresAt)")
  $st = if ($exp -lt $now) { "stale" } else { "locked" }
  Write-Output ('{{"ok":true,"state":"{0}","workerId":"{1}","leaseExpiresAt":"{2}"}}' -f $st, $l.workerId, $l.leaseExpiresAt)
  exit 0
}

if ($Action -eq "release") {
  if (Test-Path $lockPath) { Remove-Item $lockPath -Force }
  Write-Output '{"ok":true,"state":"released"}'
  exit 0
}

# ---------- acquire ----------
$now = [DateTime]::Now
if (Test-Path $lockPath) {
  $l = Read-Lock $lockPath
  $stale = ($null -eq $l)
  if (-not $stale) {
    try { $stale = ([DateTime]::Parse("$($l.leaseExpiresAt)") -lt $now) } catch { $stale = $true }
  }
  if (-not $stale) {
    Write-Output ('{{"ok":false,"state":"locked","workerId":"{0}","leaseExpiresAt":"{1}","message":"tick already running; this tick must report and exit (PIPE §0.1)"}}' -f $l.workerId, $l.leaseExpiresAt)
    exit 3
  }
  # 陈锁接管：改名留证（审计用），不静默删除
  $stamp = $now.ToString("yyyyMMdd-HHmmss")
  Move-Item $lockPath "$lockPath.stale-$stamp" -Force
  Write-Output ('{{"ok":true,"state":"took-over-stale","previousWorkerId":"{0}"}}' -f $(if ($l) { $l.workerId } else { "corrupt" }))
}
$payload = @{
  tickId         = [guid]::NewGuid().ToString()
  workerId       = [guid]::NewGuid().ToString()
  acquiredAt     = $now.ToString($fmt)
  leaseExpiresAt = $now.AddMinutes($LeaseMinutes).ToString($fmt)
} | ConvertTo-Json -Compress
# 原子创建：CreateNew＝O_EXCL，文件已存在抛 IOException（并发双开的败者走这里）
try {
  $fs = [System.IO.File]::Open($lockPath, [System.IO.FileMode]::CreateNew, [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)
  try {
    $bytes = [System.Text.Encoding]::UTF8.GetBytes($payload)
    $fs.Write($bytes, 0, $bytes.Length)
  } finally { $fs.Close() }
} catch [System.IO] {
  Write-Output '{"ok":false,"state":"lost-race","message":"another tick acquired the lock concurrently"}'
  exit 3
}
Write-Output $payload
exit 0
