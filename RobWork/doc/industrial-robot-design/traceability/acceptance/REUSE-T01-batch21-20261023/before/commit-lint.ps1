# 提交信息门禁：检查新增提交的合法前缀、有信息量的标题及长度。
# 根 AGENTS.md §6.3（2026-10-22 精简）：不再要求四段正文；验证结论仍须真实。
# -Range 默认 origin/redesign-main..HEAD；-Message 用于预检；AllowWip 保留调用兼容性。
# 历史提交不回写。中文 Git 输出统一 UTF-8，避免 Windows 编码误判。
param(
  [string]$Range = "",
  [string]$RepoRoot = "",
  [string[]]$Message = @(),
  [switch]$AllowWip
)

# ---------- 原生命令输出解码口径（F-938）----------
# git 输出（提交信息、哈希）恒为 UTF-8 字节，而 PowerShell 按
# [Console]::OutputEncoding 解码原生命令 stdout——中文 Windows 控制台缺省
# GBK（CP936），四段中文标记被解码成乱码，-Range 模式恒误报 FAIL
# （2026-10-18 d4d3ab56 合规提交被误判实测）。此处显式统一为 UTF-8；
# 无控制台宿主（CI 重定向）下 setter 可能抛错，包裹 try/catch 不阻塞门禁。
try { [Console]::OutputEncoding = [Text.Encoding]::UTF8 } catch { }

# ---------- 仓库根解析（与其余治理脚本同一套三段逻辑）----------
$scriptDir = $PSScriptRoot
if (-not $scriptDir -or -not (Test-Path $scriptDir)) { $scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path }
if (-not $RepoRoot) { $RepoRoot = (Resolve-Path (Join-Path $scriptDir "..\..\..")).Path }
if (-not (Test-Path (Join-Path $RepoRoot "RobWork/doc/industrial-robot-design"))) {
  throw "repo root resolution failed: doc base not found under '$RepoRoot'（请用 -RepoRoot 显式指定仓库根）"
}
$repoTop = (& git -C $RepoRoot rev-parse --show-toplevel 2>$null)
if (-not $repoTop) { throw "not a git repository: $RepoRoot" }
$repoTop = (Resolve-Path $repoTop).Path

# 前缀白名单：与 AGENTS §6.2 的提交前缀体系一一对应（新增前缀须先改根规则，
# 不在脚本里私开）。合并提交（Merge ...）不是人工提交，整条检查豁免。
$prefixRe = '^\[(WP-\d{2}-T\d{2}|DTB|docs|units|ARCH|governance|ACC|PIPE|CCP)\]'
$genericRe = '^(修复\s*bug|优化代码|更新文件|修改文件|update|fix)[。.!！]?$'

function Test-CommitMessage([string]$Text, [string]$Id) {
  $errors = @()
  $lines = @($Text -split "`r?`n")
  $title = ''
  foreach ($l in $lines) { if ($l.Trim()) { $title = $l.Trim(); break } }
  if (-not $title) { return @('empty commit message') }
  if ($title -match '^Merge ') { return @() }  # 合并提交豁免

  if ($title -notmatch $prefixRe) {
    $errors += "标题缺少合法前缀（[WP-nn-Tkk]/[DTB]/[docs]/[units]/[ARCH]/[governance]/[ACC]/[PIPE]/[CCP]）: $title"
  }
  $afterPrefix = $title -replace $prefixRe, ''
  $afterPrefix = $afterPrefix -replace '^\[WIP\]\s*', ''
  $afterPrefix = $afterPrefix.Trim()
  if (-not $afterPrefix) { $errors += "标题前缀后为空: $title" }
  if ($afterPrefix -match $genericRe) { $errors += "标题无信息量（禁用空话）: $title" }
  if ($title.Length -gt 80) { $errors += "标题过长（>80 字符，AGENTS 要求 ≤50 字左右）: $($title.Length)" }

  # 根 AGENTS §6.3：保留可追溯前缀与有信息量标题，取消四段正文形式检查。
  # 原因/验证/影响由实际改动决定篇幅；AllowWip 参数保留兼容已有调用。
  return $errors
}

# ---------- 取提交清单 ----------
$commits = @()
if ($Message.Count -gt 0) {
  $i = 0
  foreach ($m in $Message) { $commits += [pscustomobject]@{ Id = "message[$i]"; Text = $m }; $i++ }
} else {
  if (-not $Range) { $Range = 'origin/redesign-main..HEAD' }
  $ids = @(& git -C $repoTop log --format=%H $Range 2>$null)
  if ($LASTEXITCODE -ne 0) { throw "git log failed for range '$Range'（检查范围语法与远端引用）" }
  foreach ($id in $ids) {
    if (-not $id) { continue }
    $text = (& git -C $repoTop show -s --format=%B $id 2>$null) -join "`n"
    $commits += [pscustomobject]@{ Id = "$id".Substring(0, 12); Text = $text }
  }
}
if ($commits.Count -eq 0) {
  Write-Output "commit-lint: PASS（范围 '$Range' 无新提交——无可检查对象）"
  exit 0
}

# ---------- 逐条判定 ----------
$failed = 0
foreach ($c in $commits) {
  $errs = @(Test-CommitMessage $c.Text $c.Id)
  if ($errs.Count -gt 0) {
    $failed++
    Write-Output "  [FAIL] $($c.Id)"
    foreach ($e in $errs) { Write-Output "         - $e" }
  } else {
    Write-Output "  [PASS] $($c.Id)"
  }
}
if ($failed -gt 0) {
  Write-Output "commit-lint: FAIL —— $failed/$($commits.Count) 个提交不合规（历史提交按 F-012 豁免，不回写；新提交请按 AGENTS §6.3 修正后重提）"
  exit 1
}
Write-Output "commit-lint: PASS（$($commits.Count) 个提交全部合规）"
