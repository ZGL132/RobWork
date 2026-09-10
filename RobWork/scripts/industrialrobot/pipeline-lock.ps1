# 流水线 tick 原子锁（治理脚本，PIPE v1.5 §0.1）。
#
# 职责：把"同一时刻至多一个 tick"从约定变成原子事实——state.json 的普通读写不是原子操作，
# 两个定时实例可同时读到 idle 后各自派工。本脚本用 .NET FileStream 的 CreateNew 模式
# （Windows O_EXCL 语义）实现原子 test-and-set：文件已存在即抛异常，不存在则独占创建成功。
#
# fencing token（v1.5，三轮审查 P0 对策）：acquire 返回的 tickId 即 fencing token——
# 每次成功获取（含陈锁接管）都生成新 token。release 与 renew 必须携带 -Token 并与锁内
# tickId 核对：不匹配（典型场景＝旧 tick 租约过期被接管后复活，试图释放/续租新 tick 的锁）
# 一律拒绝（退出码 4），绝不删除他人锁。失去租约的旧 tick 不得再写状态或释放锁。
#
# 锁文件位置：<repo>/.git/ird-pipeline-tick.lock——放 .git 内部：天然不进 git status、
# 不污染工作树"脏"判定，且生命周期与仓库绑定（clone 后自然不存在）。
#
# 租约语义：锁内容含 acquiredAt/leaseExpiresAt。acquire 遇到已有锁时：
#   - 未到期 → 输出持有者信息并以退出码 3 退出（本次 tick 放弃，只汇报）；
#   - 已过期（陈锁：持有它的 tick 会话已死亡）→ 把陈锁改名 .stale-<时间戳> 留证后接管；
#   - 内容损坏（无法解析）→ 同陈锁处理。
#
# 用法：
#   pwsh -File pipeline-lock.ps1 -Action acquire -LeaseMinutes 120            # tick 开始，返回 token
#   pwsh -File pipeline-lock.ps1 -Action renew   -Token <tickId> -LeaseMinutes 120  # 长等待前续租
#   pwsh -File pipeline-lock.ps1 -Action release -Token <tickId>              # tick 结束（异常路径也必须执行）
#   pwsh -File pipeline-lock.ps1 -Action status                               # 只读查询
# 输出：单行 JSON；acquire 冲突退出码 3；token 不匹配退出码 4；其余失败退出码 1。
param(
  [Parameter(Mandatory=$true)][ValidateSet("acquire","release","renew","status")][string]$Action,
  [int]$LeaseMinutes = 30,
  [string]$Token = "",
  [string]$RepoRoot = ""
)
if ($LeaseMinutes -lt 1) { throw "LeaseMinutes must be >= 1" }
if (($Action -eq "release" -or $Action -eq "renew") -and -not $Token) { throw "-Token (fencing token = acquire 返回的 tickId) is required for $Action" }
$scriptDir = $PSScriptRoot
if (-not $scriptDir -or -not (Test-Path $scriptDir)) { $scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path }
if (-not $RepoRoot) { $RepoRoot = (Resolve-Path (Join-Path $scriptDir "..\..\..")).Path }
if (-not (Test-Path (Join-Path $RepoRoot ".git"))) { throw "repo root resolution failed: .git not found under '$RepoRoot'" }
$lockPath = Join-Path $RepoRoot ".git/ird-pipeline-tick.lock"
$fmt = "yyyy-MM-ddTHH:mm:sszzz"   # 与 state.json ISO 口径一致（秒精度＋本地偏移）

function Read-Lock([string]$path) {
  # pwsh7 的 ConvertFrom-Json 会把秒精度 ISO 解析为 DateTime——统一归一化回字符串，
  # 保证输出与比较在 pwsh7/PS5.1 两种 shell 下形态一致；文件缺失时静默返回 null（-ErrorAction
  # SilentlyContinue：renew 无锁路径不应向调用方喷错误噪音）
  try {
    $l = Get-Content $path -Raw -Encoding UTF8 -ErrorAction SilentlyContinue | ConvertFrom-Json -ErrorAction SilentlyContinue
    if ($null -ne $l) {
      foreach ($k in @("acquiredAt","leaseExpiresAt")) {
        if ($l.$k -is [datetime]) { $l.$k = $l.$k.ToString("yyyy-MM-ddTHH:mm:sszzz") }
      }
    }
    return $l
  } catch { return $null }
}

# fencing 核对：锁缺失/损坏＝幂等成功（无锁可争）；token 不匹配＝拒绝（退出码 4）
function Test-Fencing([object]$l, [string]$path) {
  if (-not (Test-Path $path)) { return "absent" }
  if ($null -eq $l) { return "absent" }   # 损坏锁视同无主，交由 acquire 的接管路径清理
  if ("$($l.tickId)" -ne $Token) {
    Write-Output ('{{"ok":false,"state":"not-owner","holder":"{0}","message":"fencing token mismatch; this tick lost its lease and must not touch the lock (PIPE §0.1)"}}' -f $l.tickId)
    exit 4
  }
  return "owner"
}

if ($Action -eq "status") {
  if (-not (Test-Path $lockPath)) { Write-Output '{"ok":true,"state":"none"}'; exit 0 }
  $l = Read-Lock $lockPath
  if ($null -eq $l) { Write-Output '{"ok":true,"state":"corrupt"}'; exit 0 }
  $now = [DateTime]::Now
  $exp = [DateTime]::Parse("$($l.leaseExpiresAt)")
  $st = if ($exp -lt $now) { "stale" } else { "locked" }
  Write-Output ('{{"ok":true,"state":"{0}","tickId":"{1}","workerId":"{2}","leaseExpiresAt":"{3}"}}' -f $st, $l.tickId, $l.workerId, $l.leaseExpiresAt)
  exit 0
}

if ($Action -eq "release") {
  $l = Read-Lock $lockPath
  $fence = Test-Fencing $l $lockPath
  if ($fence -eq "owner" -or $fence -eq "absent") { if (Test-Path $lockPath) { Remove-Item $lockPath -Force }; Write-Output '{"ok":true,"state":"released"}'; exit 0 }
  exit 4
}

if ($Action -eq "renew") {
  $l = Read-Lock $lockPath
  $fence = Test-Fencing $l $lockPath
  if ($fence -eq "absent") { Write-Output '{"ok":false,"state":"no-lock","message":"cannot renew: lock absent/corrupt"}'; exit 1 }
  $l.leaseExpiresAt = [DateTime]::Now.AddMinutes($LeaseMinutes).ToString($fmt)
  $l | ConvertTo-Json -Compress | Set-Content $lockPath -Encoding UTF8 -NoNewline
  Write-Output ('{{"ok":true,"state":"renewed","tickId":"{0}","leaseExpiresAt":"{1}"}}' -f $l.tickId, $l.leaseExpiresAt)
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
    Write-Output ('{{"ok":false,"state":"locked","tickId":"{0}","workerId":"{1}","leaseExpiresAt":"{2}","message":"tick already running; this tick must report and exit (PIPE §0.1)"}}' -f $l.tickId, $l.workerId, $l.leaseExpiresAt)
    exit 3
  }
  # 陈锁接管：改名留证（审计用），不静默删除
  $stamp = $now.ToString("yyyyMMdd-HHmmss")
  Move-Item $lockPath "$lockPath.stale-$stamp" -Force
  Write-Output ('{{"ok":true,"state":"took-over-stale","previousTickId":"{0}"}}' -f $(if ($l) { $l.tickId } else { "corrupt" }))
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
