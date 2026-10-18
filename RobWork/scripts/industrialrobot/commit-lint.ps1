# 提交信息门禁（commit-lint，2026-10-18 治理批）。
#
# 职责：把 AGENTS.md §6.3 的提交信息约定（前缀体系＋标题动词开头＋正文四段）
# 从"行为自觉"升级为可执行检查。**只对新提交生效**——历史提交不可改写
# （§6.3 红线），存量风格债由 findings F-012 整族销案，不回写历史。
#
# 背景（为什么现在做）：
#   ACC §4.9 与 AGENTS §6.3 一直要求四段式正文与规范标题，但此前没有任何
#   脚本会因此变红；F-012（DOC-T04 标题非动词开头）与 F-926（验证段计数
#   "84/84"掩盖 1 项 skip）都是同一类"写的时候没人查"的漂移。本脚本提供
#   `-Range`（CI 只查本次推送/PR 的新提交）与 `-Message`（自测/预检）两种
#   入口，做到止增而不追改历史。
#
# 检查规则（任一失败即 exit 1）：
#   ① 标题前缀 ∈ 白名单：[WP-nn-Tkk] / [DTB] / [docs] / [units] / [ARCH] /
#      [governance] / [ACC] / [PIPE] / [CCP]（AGENTS §6.2）；合并提交豁免。
#   ② 标题在去掉前缀后非空、不是无信息量文案（"修复 bug/优化代码/更新文件/
#      update/fix"），长度 ≤ 80 字符（AGENTS "≤ 50 字左右"的机器上界）。
#   ③ 正文包含四段标记：改动内容（What）/ 改动原因（Why）/ 验证（How）/
#      影响面（含"影响面与兼容性"写法）。WIP 提交（标题或正文显式 WIP）豁免
#      四段，但前缀与标题仍须合规——半成品允许，含糊不允许。
#   ④ 验证段若出现 "N/N" 形式且同一段含"跳过/skipped"，不在此处做语义判分
#      （无法稳定机器判定"是否掩盖"）；F-926 的口径要求由 AGENTS §6.3 文本
#      承载，review 抽查。脚本只保证四段在、标题可追溯。
#
# 用法：
#   pwsh -File commit-lint.ps1 [-Range <base>..<head>] [-RepoRoot <仓库根>]
#   pwsh -File commit-lint.ps1 -Message "标题`n`n改动内容（What）：..."   # 自测/预检
#   -Range 缺省＝origin/redesign-main..HEAD（本地提交相对主线的新增面）。
#
# 追溯：AGENTS §6.3；ACC §4.9；findings F-012（历史豁免）/F-926（计数口径）；
# rule-enforcement.md §2 执法者清单；DTB §5.1 脚本清单。

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
$sectionMarkers = @('改动内容（What）', '改动原因（Why）', '验证（How）', '影响面')

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

  # WIP 提交：允许半成品，但必须显式声明（AGENTS §6.3：WIP 需标注"缺 xxx"）。
  $isWip = ($AllowWip -or $title -match '\[WIP\]' -or $Text -match '(?m)^WIP[：:]')
  if (-not $isWip) {
    foreach ($m in $sectionMarkers) {
      if ($Text -notmatch [regex]::Escape($m)) { $errors += "正文缺少段标记: $m" }
    }
  }
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
