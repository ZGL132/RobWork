# 上下文包校验器（治理 Task 6，2026-10-17）。
#
# 职责：校验 context/packs/ 下上下文包的新鲜度与纪律——
#   ① 结构：生成物标记＋指纹段存在（防手改伪装成生成物）；
#   ② 新鲜度：逐行重算指纹表 SHA-256，源文件变化即报 STALE（过期）；
#   ③ 契约状态漂移：包内"任务状态（契约）"与当前契约 JSON 不一致 → 提示重生成；
#   ④ PIPE 权威红线（评审修正 3）：包内不得出现流水线状态字段副本（phase/heartbeat/
#     queue 的 JSON 形态）——上下文包只指引读 state.json 本体，绝不复制状态。
#
# 退出码：0＝全部新鲜；2＝存在过期/漂移（重跑 build-context-pack.ps1 刷新）；1＝结构错误或红线违例。
# 用法：pwsh -File validate-context-pack.ps1 -All            （默认全量）
#       pwsh -File validate-context-pack.ps1 -TaskId CORE-T01（单个）
param(
  [string]$TaskId = "",
  [switch]$All,
  [string]$RepoRoot = ""
)
$scriptDir = $PSScriptRoot
if (-not $scriptDir -or -not (Test-Path $scriptDir)) { $scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path }
if (-not $RepoRoot) { $RepoRoot = (Resolve-Path (Join-Path $scriptDir "..\..")).Path }
$base = Join-Path $RepoRoot 'doc/industrial-robot-design'
if (-not (Test-Path $base)) { throw "repo root resolution failed: doc base not found under '$RepoRoot'（请用 -RepoRoot 显式指定 RobWork 目录）" }
$repoTop = Split-Path -Parent $RepoRoot
$packsDir = Join-Path $base 'context/packs'
if (-not (Test-Path $packsDir)) { Write-Output 'validate-context-pack: no packs dir (nothing to validate)'; exit 0 }

if ($TaskId) { $packs = @(Get-Item (Join-Path $packsDir "$TaskId.md") -ErrorAction SilentlyContinue) }
else { $packs = @(Get-ChildItem $packsDir -Filter '*.md' -ErrorAction SilentlyContinue) }
if (-not $packs.Count) { Write-Output 'validate-context-pack: no packs found (nothing to validate)'; exit 0 }

$structural = @(); $stale = @(); $fresh = 0
foreach ($p in $packs) {
  $t = Get-Content $p.FullName -Raw -Encoding UTF8
  # ① 结构检查
  if ($t -notmatch 'build-context-pack\.ps1') { $structural += "pack missing generator marker: $($p.Name)" }
  if ($t -notmatch '源文件指纹') { $structural += "pack missing SHA-256 fingerprint section: $($p.Name)" }
  # ④ PIPE 权威红线：包内禁流水线状态字段副本（JSON 形态匹配，误报面窄）
  foreach ($marker in '"phase"\s*:', '"heartbeat"', '"queue"\s*:') {
    if ($t -match $marker) { $structural += "pack violates PIPE authority red line (state copy '$($marker.Trim())'): $($p.Name)" }
  }
  # ② 新鲜度：解析指纹表（| 相对路径 | git blob SHA |），逐行重算——指纹用
  # git hash-object（经 clean 过滤，行尾无关）：字节级 SHA-256 在 core.autocrlf 下
  # 随检出行尾漂移，干净树上全量误报 STALE（演练验收 F-909 教训）
  $inTable = $false; $rowSeen = 0
  foreach ($line in ($t -split '\r?\n')) {
    if ($line -match '^\|\s*源文件\s*\|') { $inTable = $true; continue }
    if ($inTable -and $line -match '^\|\s*---') { continue }
    if ($inTable -and $line -match '^\|\s*([^|]+?)\s*\|\s*([0-9a-fA-F]{40})\s*\|\s*$') {
      $rel = $Matches[1].Trim(); $oldSha = $Matches[2].ToLowerInvariant()
      $rowSeen++
      $abs = Join-Path $repoTop $rel
      if (-not (Test-Path -LiteralPath $abs)) { $stale += "MISSING source: $rel (pack $($p.Name))——重跑 build-context-pack.ps1 -TaskId $tid 刷新"; continue }
      $newSha = (git -C $repoTop hash-object $abs 2>$null | Select-Object -First 1)
      if (-not $newSha) { $stale += "MISSING git-object: $rel (pack $($p.Name))"; continue }
      if ("$newSha".ToLowerInvariant() -ne $oldSha) { $stale += "STALE source: $rel (pack $($p.Name))——重跑 build-context-pack.ps1 -TaskId $tid 刷新" }
    } elseif ($inTable -and $line -match '^\|' -and $line -notmatch '^\|\s*---') {
      # 表内出现非 40 位十六进制行——结构可疑（旧版 64 位字节哈希包同理判旧）
      if ($rowSeen -gt 0 -or $line -notmatch '^\|\s*源文件') { }
    } elseif ($inTable) {
      $inTable = $false
    }
  }
  if ($rowSeen -eq 0) { $structural += "pack fingerprint table empty/unparsable: $($p.Name)" }
  # ③ 契约状态漂移：包内登记的契约 status vs 当前契约 JSON
  if ($t -match '\|\s*任务状态（契约）\s*\|\s*(\S+)\s*\|') {
    $packedStatus = $Matches[1]
    $tid = [IO.Path]::GetFileNameWithoutExtension($p.Name)
    $contractFile = @(Get-ChildItem (Join-Path $base 'tasks') -Recurse -Filter "$tid.json" | Select-Object -First 1)
    if ($contractFile.Count) {
      $cx = Get-Content $contractFile[0].FullName -Raw -Encoding UTF8 | ConvertFrom-Json
      if ("$($cx.status)" -ne "$packedStatus") { $stale += "DRIFT contract status: pack=$packedStatus current=$($cx.status) ($tid)" }
    }
  }
  if (-not ($stale | Where-Object { $_ -match [regex]::Escape($p.Name) })) { $fresh++ }
}
foreach ($s in $structural) { Write-Error "validate-context-pack: $s" }
foreach ($s in $stale) { Write-Warning "validate-context-pack: $s" }
Write-Output "validate-context-pack: $($packs.Count) pack(s), fresh=$fresh, stale/drift=$(@($stale).Count), structural=$(@($structural).Count)"
if ($structural.Count) { exit 1 }
if ($stale.Count) { exit 2 }
exit 0
