# 任务改动范围校验器（治理脚本，2026-10-18 Agent-native 治理优化批）。
#
# 职责：把验收协议（acceptance-protocol.md，ACC）§4.1「改动范围」从纯人工比对
# 升级为机器可判——给定任务契约与 base..head 提交范围，逐一核对 diff 里的每个
# 路径是否落在契约 allowedFiles 内、是否命中 forbiddenFiles。退出码 0＝全部在
# 范围内；1＝有越界/禁止面命中/输入或环境错误。
#
# 为什么需要它（背景）：
#   ACC §4.1 一直靠验收者人工比对 `git diff --name-only` 与 allowedFiles。360 份
#   契约、单批常见 20+ 文件，人工比对既慢又会漏；而"范围越界"是阻断级 FAIL 项，
#   此前却没有任何脚本会因此变红（rule-enforcement.md 曾把该条标为 review 层）。
#   本脚本是该条款的机器执行面（findings F-928 消账），人工复核仍保留——机器只
#   负责"每个路径都在允许面内"这一事实判定，语义是否越界仍归验收者。
#
# 契约 allowedFiles 的现实形态（2026-10-18 全量 360 契约/1540 条实测）：
#   - 精确文件路径，绝大多数不带通配；
#   - 目录通配 `dir/**`、单层 `dir/*`、文件名前缀 `dir/kin-*`；
#   - 9 条在路径末尾带全角括号说明（如"…CMakeLists.txt（ctest 注册面…）"）——
#     匹配前剥离尾部注记，否则会被当成路径的一部分而误报越界；
#   - 无"纯目录"条目、无 `、`/`;` 枚举条目（实测核过，不为不存在的形态写分支）。
#
# 匹配口径（宁可报错让人补契约，也不做宽松包含）：
#   先剥离尾部全角括号注记 → 统一斜杠 → 转正则；`**` 匹配任意层级、`*` 只匹配
#   单层内任意字符、`?` 匹配单字符。大小写不敏感（Windows 检出面）。
#
# 用法：
#   pwsh -File verify-task-scope.ps1 -TaskFile <契约路径> [-Base <sha>] [-Head <sha>] [-RepoRoot <仓库根>]
#   -Base 仅在实际任务分支有非空提交范围时可缺省，取 git merge-base redesign-main
#   <head>（本地分支不存在时只读回退 origin/redesign-main，不同步远程）。主线现场、
#   主线契约或 merge-base 等于 head 时必须显式传 Base；-Head 缺省＝HEAD。
#   典型用法：实施者提交前自检 `-Base <base SHA> -Head HEAD`；验收者在冻结 SHA 上
#   复跑 `-Base <base> -Head <headSha>`（送验对象冻结后范围结论即为验收 4.1 证据）。
#
# 追溯：ACC §3/§4.1（机器预检）、ACC §5（逐项证据）；rule-enforcement.md §2 执法者
# 清单；findings F-928。

param(
  [Parameter(Mandatory=$true)][string]$TaskFile,
  [string]$Base = "",
  [string]$Head = "HEAD",
  [string]$RepoRoot = ""
)

# ---------- 仓库根解析（与 validate-task.ps1 / verify-task.ps1 同一套三段逻辑）----------
# 显式参数 > $PSScriptRoot 上三级 > MyInvocation 回退；解析失败显性报错，绝不
# 静默归约到盘根（CORE-T01 验收 G-1 教训：脚本默认值求值时机不同会归约到 D:\）。
if (-not (Test-Path $TaskFile)) { throw "task file not found: $TaskFile" }
$scriptDir = $PSScriptRoot
if (-not $scriptDir -or -not (Test-Path $scriptDir)) { $scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path }
if (-not $RepoRoot) { $RepoRoot = (Resolve-Path (Join-Path $scriptDir "..\..\..")).Path }
if (-not (Test-Path (Join-Path $RepoRoot "RobWork/doc/industrial-robot-design"))) {
  throw "repo root resolution failed: doc base not found under '$RepoRoot'（请用 -RepoRoot 显式指定仓库根）"
}
# git 仓库顶（worktree 场景下是 worktree 根，而非公共 .git 目录所在处）：
# 契约路径（RobWork/...）以仓库顶为基准，diff 输出亦然，两者必须同源。
$prevEnc = [Console]::OutputEncoding
try {
  [Console]::OutputEncoding = [System.Text.Encoding]::UTF8
  $repoTop = (& git -C $RepoRoot rev-parse --show-toplevel 2>$null)
} finally { [Console]::OutputEncoding = $prevEnc }
if (-not $repoTop -or -not (Test-Path $repoTop)) { throw "not a git repository: $RepoRoot" }
$repoTop = (Resolve-Path $repoTop).Path

# ---------- 契约解析 ----------
$task = Get-Content $TaskFile -Raw -Encoding UTF8 | ConvertFrom-Json
if (-not $task.taskId) { throw "invalid task file (missing taskId): $TaskFile" }
if ("$($task.status)" -ne 'ready' -and "$($task.status)" -ne 'done') {
  # planned/blocked 契约的范围面尚未定稿，机器判定没有意义；显式提示而不是静默给 PASS。
  Write-Warning "task status is '$($task.status)' (expected ready/done): $($task.taskId) —— 范围面可能仍会变，结论仅供参考"
}

# ---------- 提交范围解析 ----------
# 先做 rev-parse --verify：非法/歧义 SHA 在 diff 阶段只会得到空输出，会被误判成
# "零改动 PASS"——这是最危险的假绿，必须前置拦截。
function Resolve-Commit([string]$Rev, [string]$Label) {
  $sha = (& git -C $repoTop rev-parse --verify --quiet "$Rev^{commit}" 2>$null)
  if (-not $sha) { throw "cannot resolve $Label commit: $Rev（检查 SHA/分支名是否存在于本仓库）" }
  return "$sha".Trim()
}
$headSha = Resolve-Commit $Head 'head'
if (-not $Base) {
  # GOV-T23-R1-01：旧契约的 branch 仅用于溯源，不能代表当前检出分支。
  # 主线串行任务必须显式给基点，避免旧 codex 契约绕过检查后与自身比较。
  $currentBranch = (& git -C $repoTop rev-parse --abbrev-ref HEAD 2>$null)
  if ($LASTEXITCODE -ne 0) { throw 'cannot determine current branch: pass -Base <sha> explicitly' }
  if ($task.branch -eq 'redesign-main' -or $currentBranch -eq 'redesign-main') {
    throw 'serial mainline task requires explicit -Base <start SHA>'
  }
  # 缺省基点＝与 redesign-main 的分叉点。任务分支从 redesign-main 切出时，这恰好
  # 是该任务引入的全部提交；若分支被 rebase 过，以显式 -Base 为准。
  $baseRef = 'redesign-main'
  $probe = (& git -C $repoTop rev-parse --verify --quiet "refs/heads/$baseRef^{commit}" 2>$null)
  if (-not $probe) { $baseRef = 'origin/redesign-main' }
  $probe = (& git -C $repoTop rev-parse --verify --quiet "$baseRef^{commit}" 2>$null)
  if (-not $probe) { throw "cannot determine base: pass -Base <sha> explicitly（找不到 redesign-main/origin/redesign-main）" }
  $baseSha = (& git -C $repoTop merge-base $baseRef $headSha 2>$null)
  if (-not $baseSha) { throw "cannot compute merge-base between $baseRef and $headSha" }
  # detached 冻结现场或已在主线的旧任务同样可能算出 head 本身，不能将此解释为无改动。
  if ($baseSha -eq $headSha) { throw 'scope with empty implicit range requires explicit -Base <start SHA>' }
} else {
  $baseSha = Resolve-Commit $Base 'base'
}

# ---------- diff 清单 ----------
# -z：文件名以 NUL 分隔，规避中文/空格路径的引号与八进制转义（F-908 同族教训）。
try {
  [Console]::OutputEncoding = [System.Text.Encoding]::UTF8
  $diffRaw = @(& git -C $repoTop -c core.quotepath=off diff --name-only -z "$baseSha..$headSha" 2>$null)
} finally { [Console]::OutputEncoding = $prevEnc }
$diffText = [string]::Join('', $diffRaw)
$changed = @($diffText -split "`0" | Where-Object { $_ -ne '' } | ForEach-Object { $_.Replace('\','/') })

# ---------- 允许面/禁止面匹配 ----------
# 契约里 9 条带尾部全角括号注记（"路径（说明）"）——注记不是路径的一部分。
# 只在字符串**末尾**剥离，避免误伤路径中间可能出现的括号。
function ConvertTo-ScopeRegex([string]$Raw) {
  $p = $Raw.Trim()
  $p = [regex]::Replace($p, '\s*（[^）]*）\s*$', '')
  $p = $p.Replace('\','/')
  $p = $p -replace '^\./',''
  $p = $p.TrimEnd('/')
  if (-not $p) { return $null }
  $e = [regex]::Escape($p)
  # 顺序敏感：先 `***`（实测存在的三连星）再 `**` 再 `*`，否则 `**` 会被拆成两次单星。
  $e = $e.Replace('\*\*\*', '.*').Replace('\*\*', '.*').Replace('\*', '[^/]*').Replace('\?', '[^/]')
  return '^' + $e + '$'
}
function New-ScopeRule([string]$Raw) {
  $regex = ConvertTo-ScopeRegex $Raw
  if (-not $regex) { return $null }
  return [pscustomobject]@{ Raw = $Raw.Trim(); Regex = $regex }
}
$allowedRules = @()
foreach ($a in @($task.allowedFiles)) { if ("$a".Trim()) { $r = New-ScopeRule "$a"; if ($r) { $allowedRules += $r } } }
$forbiddenRules = @()
foreach ($f in @($task.forbiddenFiles)) { if ("$f".Trim()) { $r = New-ScopeRule "$f"; if ($r) { $forbiddenRules += $r } } }
if ($allowedRules.Count -eq 0) { throw "contract has empty allowedFiles: $($task.taskId)" }

function Find-ScopeHit([string]$Path, $Rules) {
  foreach ($r in $Rules) {
    if ([regex]::IsMatch($Path, $r.Regex, [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)) { return $r }
  }
  return $null
}

# ---------- 逐文件判定 ----------
# 判定顺序：先 forbidden（命中即阻断，不论是否在 allowed 内——禁止面优先），
# 再 allowed。删除/重命名同样计入 diff 名字（重命名给新路径），一视同仁。
$violations = 0
Write-Output ("verify-task-scope: {0} base={1} head={2} changed={3} allowedRules={4} forbiddenRules={5}" -f `
  $task.taskId, $baseSha.Substring(0,12), $headSha.Substring(0,12), $changed.Count, $allowedRules.Count, $forbiddenRules.Count)
foreach ($path in $changed) {
  $fh = Find-ScopeHit $path $forbiddenRules
  if ($fh) {
    Write-Output "  [forbidden] $path  <= $($fh.Raw)"
    $violations++
    continue
  }
  $ah = Find-ScopeHit $path $allowedRules
  if (-not $ah) {
    Write-Output "  [out-of-scope] $path（未命中任何 allowedFiles 模式）"
    $violations++
  }
}
if ($violations -gt 0) {
  Write-Output "verify-task-scope: FAIL $($task.taskId) —— $violations 个路径越界/命中禁止面（补契约 allowedFiles 或撤回改动，不得带病提交）"
  exit 1
}
Write-Output "verify-task-scope: PASS $($task.taskId) —— $($changed.Count) 个路径全部在允许面内"
