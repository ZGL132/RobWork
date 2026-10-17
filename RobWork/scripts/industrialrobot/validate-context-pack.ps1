# 上下文包校验器（治理 Task 6，2026-10-17）。
#
# 职责：校验 context/packs/ 下上下文包的新鲜度与纪律——
#   ① 结构：生成物标记＋指纹段存在（防手改伪装成生成物）；
#   ② 新鲜度：逐行重算指纹表（git blob SHA-1，40 位），源文件变化即报 STALE（过期）；
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
if (-not $packs.Count) {
  # 审查 P2 对策（2026-10-17）：指定 TaskId 而包不存在＝结构错误（先跑 build-context-pack
  # 生成），不得与"全量模式无包可查"的成功态混为一谈
  if ($TaskId) { Write-Error "validate-context-pack: pack not found for TaskId: $TaskId（先运行 build-context-pack.ps1 -TaskId $TaskId 生成）"; exit 1 }
  Write-Output 'validate-context-pack: no packs found (nothing to validate)'; exit 0
}

$structural = @(); $stale = @(); $fresh = 0
foreach ($p in $packs) {
  $t = Get-Content $p.FullName -Raw -Encoding UTF8
  # $tid 在循环体开头取本包任务 ID——STALE 提示要指向当前包（若只在漂移块内定义，
  # 多包循环时会沿用上一包的值，提示指向错误包：attempt 2 复审 suggestion 实测）
  $tid = [IO.Path]::GetFileNameWithoutExtension($p.Name)
  # per-pack 新鲜标记（审查 P2 对策，2026-10-17）：按包名匹配 stale 数组会把只含
  # 任务 ID 的 DRIFT 行漏算，出现 stale>0 而 fresh=包总数的误导统计
  $packHasStale = $false
  # ① 结构检查（2026-10-17 二轮审查 P2 对策：契约存在性/身份一致性移出状态行解析
  # 之外——伪造包删掉"任务状态（契约）"行不应绕过契约核对；指纹表头必须显式
  # 'git blob SHA-1'——防 SHA-256 误标口径的包混入）
  if ($t -notmatch 'build-context-pack\.ps1') { $structural += "pack missing generator marker: $($p.Name)" }
  if ($t -notmatch '源文件指纹') { $structural += "pack missing blob-SHA fingerprint section: $($p.Name)" }
  if ($t -notmatch '(?m)^\|\s*源文件\s*\|\s*git blob SHA-1\s*\|') { $structural += "pack fingerprint table header must be 'git blob SHA-1': $($p.Name)" }
  if ($t -notmatch '\|\s*任务状态（契约）\s*\|') { $structural += "pack missing contract status row: $($p.Name)" }
  # 契约存在性与身份一致性（无条件执行，不依赖状态行存在）
  $contractFile = @(Get-ChildItem (Join-Path $base 'tasks') -Recurse -Filter "$tid.json" | Select-Object -First 1)
  $cxPack = $null
  if ($contractFile.Count) { $cxPack = Get-Content $contractFile[0].FullName -Raw -Encoding UTF8 | ConvertFrom-Json }
  else { $structural += "pack references unknown task (contract JSON not found): $($p.Name)" }
  if ($t -match '(?m)^#\s*上下文包：(\S+?)(?:\s|——)') {
    $packTid = $Matches[1]
    if ($packTid -ne $tid) { $structural += "pack heading taskId mismatch: heading=$packTid file=$tid ($($p.Name))" }
    if ($cxPack -and "$($cxPack.taskId)" -ne $tid) { $structural += "contract taskId mismatch: contract=$($cxPack.taskId) file=$tid ($($p.Name))" }
    if ($t -match '(?m)^#\s*上下文包：\S+\s*——\s*(.+?)\s*$') {
      if ($cxPack -and ("$($cxPack.title)".Trim() -ne $Matches[1])) { $structural += "pack heading title mismatch with contract: ($($p.Name))" }
    }
  } else { $structural += "pack heading '上下文包：<taskId> —— <title>' unparsable: $($p.Name)" }
  # ④ PIPE 权威红线：包内禁流水线状态字段副本（JSON 形态匹配，误报面窄）
  foreach ($marker in '"phase"\s*:', '"heartbeat"', '"queue"\s*:') {
    if ($t -match $marker) { $structural += "pack violates PIPE authority red line (state copy '$($marker.Trim())'): $($p.Name)" }
  }
  # ② 新鲜度：解析指纹表（| 相对路径 | git blob SHA |），逐行重算——指纹用
  # git hash-object（经 clean 过滤，行尾无关）：字节级哈希在 core.autocrlf 下
  # 随检出行尾漂移，干净树上全量误报 STALE（演练验收 F-909 教训）
  $inTable = $false; $rowSeen = 0
  foreach ($line in ($t -split '\r?\n')) {
    if ($line -match '^\|\s*源文件\s*\|') { $inTable = $true; continue }
    if ($inTable -and $line -match '^\|\s*---') { continue }
    if ($inTable -and $line -match '^\|\s*([^|]+?)\s*\|\s*([0-9a-fA-F]{40})\s*\|\s*$') {
      $rel = $Matches[1].Trim(); $oldSha = $Matches[2].ToLowerInvariant()
      $rowSeen++
      $abs = Join-Path $repoTop $rel
      if (-not (Test-Path -LiteralPath $abs)) { $stale += "MISSING source: $rel (pack $($p.Name))——重跑 build-context-pack.ps1 -TaskId $tid 刷新"; $packHasStale = $true; continue }
      $newSha = (git -C $repoTop hash-object $abs 2>$null | Select-Object -First 1)
      if (-not $newSha) { $stale += "MISSING git-object: $rel (pack $($p.Name))"; $packHasStale = $true; continue }
      if ("$newSha".ToLowerInvariant() -ne $oldSha) { $stale += "STALE source: $rel (pack $($p.Name))——重跑 build-context-pack.ps1 -TaskId $tid 刷新"; $packHasStale = $true }
    } elseif ($inTable -and $line -match '^\|' -and $line -notmatch '^\|\s*---') {
      # 表内出现非 40 位十六进制行——结构可疑（旧版 64 位字节哈希包同理判旧）
      if ($rowSeen -gt 0 -or $line -notmatch '^\|\s*源文件') { }
    } elseif ($inTable) {
      $inTable = $false
    }
  }
  if ($rowSeen -eq 0) { $structural += "pack fingerprint table empty/unparsable: $($p.Name)" }
  # ③ 契约状态漂移：状态行缺失已在 ① 报结构错；两者齐备时比对（复用 ① 的 $cxPack）
  if ($cxPack -and $t -match '\|\s*任务状态（契约）\s*\|\s*(\S+)\s*\|') {
    $packedStatus = $Matches[1]
    if ("$($cxPack.status)" -ne "$packedStatus") { $stale += "DRIFT contract status: pack=$packedStatus current=$($cxPack.status) ($tid) (pack $($p.Name))"; $packHasStale = $true }
  }
  if (-not $packHasStale) { $fresh++ }
}
foreach ($s in $structural) { Write-Error "validate-context-pack: $s" }
foreach ($s in $stale) { Write-Warning "validate-context-pack: $s" }
Write-Output "validate-context-pack: $($packs.Count) pack(s), fresh=$fresh, stale/drift=$(@($stale).Count), structural=$(@($structural).Count)"
if ($structural.Count) { exit 1 }
if ($stale.Count) { exit 2 }
exit 0
