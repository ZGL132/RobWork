# 证据索引生成器（治理 Task 7，2026-10-17）。
#
# 职责：扫描 traceability/ 全部证据文件，生成**摘要索引** evidence-index.json——
# 按需检索证据的可选快照（context/README §5），允许滞后，不能替代当前任务原件。
# 索引只存摘要，不嵌入文件内容；普通任务不需要先生成或加载全库索引。
#
# 每条索引：相对路径、类型（目录/扩展名映射）、关联任务 ID（文件名启发式）、
# 字节数、SHA-256（仅 ≤512KB 文件——大文件跳过并在 summary 计数）、生命周期。
#
# 生命周期判定规则（诚实口径，保守起步）：
#   current     ＝被 open finding 的 source 引用，或 pipeline 机器维护载体（state.json/b1 manifest）；
#   historical  ＝关联任务全部 done（已完成工作的历史留痕）；
#   superseded  ＝被取代——需人工标记，首轮一律不判；
#   unverified  ＝无法自动判定当前性（默认值）。
#
# 纪律：本脚本只读扫描＋生成索引，不移动/不删除/不修改任何证据文件（只增纪律）。
# 用法：pwsh -File index-evidence.ps1 [-RepoRoot <RobWork 目录>]
param([string]$RepoRoot = "")
$scriptDir = $PSScriptRoot
if (-not $scriptDir -or -not (Test-Path $scriptDir)) { $scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path }
if (-not $RepoRoot) { $RepoRoot = (Resolve-Path (Join-Path $scriptDir "..\..")).Path }
$base = Join-Path $RepoRoot 'doc/industrial-robot-design'
if (-not (Test-Path $base)) { throw "repo root resolution failed: doc base not found under '$RepoRoot'（请用 -RepoRoot 显式指定 RobWork 目录）" }
$traceDir = Join-Path $base 'traceability'
$indexPath = Join-Path $traceDir 'evidence-index.json'
$shaCapBytes = 512KB

# 内容口径：文本证据 CRLF/CR 规范化为 LF；二进制按原始字节。
# 共享文本扩展名白名单单源加载（F-912——与 validate-docs.ps1 同读 config/
# text-extensions.txt 一份口径，杜绝双份漂移；脚本相对解析，调用方式无关）。
$script:TextExtensions = @(Get-Content (Join-Path $PSScriptRoot 'config\text-extensions.txt') |
  ForEach-Object { $_.Trim() } | Where-Object { $_ -ne '' -and -not $_.StartsWith('#') })
function Get-IndexedContent([string]$LiteralPath, [long]$Cap) {
  $f = Get-Item -LiteralPath $LiteralPath
  [byte[]]$b = [IO.File]::ReadAllBytes($f.FullName)
  $isText = ($script:TextExtensions -contains $f.Extension.ToLowerInvariant()) -or $f.Name -eq '.gitattributes'
  if ($isText -and -not ($b -contains [byte]0)) {
    $enc = [Text.Encoding]::GetEncoding(28591)
    $text = $enc.GetString($b).Replace("`r`n", "`n").Replace("`r", "`n")
    $b = $enc.GetBytes($text)
  }
  $hash = $null
  if ($b.Length -le $Cap) { $sha = [Security.Cryptography.SHA256]::Create(); $hash = ([BitConverter]::ToString($sha.ComputeHash($b)).Replace('-','')).ToLowerInvariant(); $sha.Dispose() }
  return @{ Bytes = $b.Length; Sha256 = $hash }
}
# ---------- 任务契约状态表（lifecycle 判定用）----------
$taskStatus = @{}
foreach ($f in @(Get-ChildItem (Join-Path $base 'tasks') -Recurse -Filter '*.json' -ErrorAction SilentlyContinue | Where-Object { $_.Name -ne 'foundation-tasks.json' })) {
  try { $x = Get-Content $f.FullName -Raw -Encoding UTF8 | ConvertFrom-Json; if ($x -and $x.taskId) { $taskStatus["$($x.taskId)"] = "$($x.status)" } } catch { }
}
# ---------- open findings 引用的证据路径集合（current 判定用）----------
$openRefPaths = @{}
try {
  $fj = Get-Content (Join-Path $traceDir 'findings.json') -Raw -Encoding UTF8 | ConvertFrom-Json
  foreach ($fd in @($fj.findings)) {
    if ("$($fd.status)" -ne 'open') { continue }
    $src = "$($fd.source)" -replace '#.*$',''
    if ($src -match '^traceability/(.+)$') { $openRefPaths[$Matches[1].Replace('\','/')] = $true }
  }
} catch { }
# 机器维护的现行载体（引用不复制内容——PIPE 权威红线）
$machineCurrent = @('pipeline/state.json','pipeline/b1-host-integration-batch.json')

function Get-EvidenceKind([string]$rel, [string]$ext) {
  $top = ($rel -split '/')[0]
  switch ($top) {
    'acceptance' { return 'acceptance-record' }
    'manual-acceptance' { return 'manual-acceptance' }
    'gtest-reports' { return 'gtest-report' }
    'builds' { return 'build-log' }
    'benchmarks' { return 'benchmark' }
    'pipeline' { return 'pipeline-artifact' }
    'closeout' { return 'governance-record' }
    'reconciliation' { return 'governance-record' }
    'reviews' { return 'governance-record' }
    'engineering-polish' { return 'governance-record' }
  }
  if ($ext -in '.png','.jpg','.jpeg','.bmp') { return 'screenshot' }
  if ($ext -eq '.xml') { return 'xml-report' }
  if ($ext -eq '.json') { return 'json-data' }
  if ($ext -eq '.md') { return 'markdown-doc' }
  if ($ext -eq '.log' -or $ext -eq '.txt') { return 'log' }
  if ($ext -eq '.ps1') { return 'script' }
  return 'other'
}
# 任务 ID 规范化：wp 风格补零到两位（wp04-t16→WP-04-T16）；单元风格原样大写
function ConvertTo-CanonicalTaskId([string]$raw) {
  $u = $raw.ToUpperInvariant()
  if ($u -match '^WP-(\d+)-T(\d+)$') { return ('WP-{0:D2}-T{1:D2}' -f [int]$Matches[1], [int]$Matches[2]) }
  return $u
}
function Get-TaskIds([string]$rel) {
  $ids = @()
  foreach ($m in [regex]::Matches($rel, '(?i)(?:^|[/_.\-])(wp-\d{1,3}-t\d{1,3})(?:$|[/_.\-0-9])')) {
    $c = ConvertTo-CanonicalTaskId $m.Groups[1].Value
    if ($ids -notcontains $c) { $ids += $c }
  }
  foreach ($m in [regex]::Matches($rel, '(?:^|[/_.\-])([A-Z]{2,6}-T\d{1,3})(?:$|[/_.\-0-9])')) {
    $c = $m.Groups[1].Value
    if ($ids -notcontains $c) { $ids += $c }
  }
  return ,$ids
}

# ---------- 扫描（基准＝git 跟踪文件；排除索引自身）----------
# 用 git ls-files 枚举而非文件系统遍历：主工作区可能存在 gitignored 历史产物
# （截图/日志），计入会使干净检出（无这些文件）与索引计数失配、门禁误报
# （演练验收 F-908 教训）。代价：未提交的新证据不计入——与"新证据落库同批
# 重跑索引"纪律自洽（提交与索引同批完成）。
$repoTop = Split-Path -Parent $RepoRoot
$traceRel = $traceDir.Substring($repoTop.Length + 1).Replace('\','/')
# core.quotepath=off＋UTF-8 控制台：git 默认对非 ASCII 路径做八进制转义加引号，
# 中文文件名（如 ird-gates-增量登记.md）会解析失败（演练批实测 4 个文件丢失）
$prevEnc = [Console]::OutputEncoding
try {
  [Console]::OutputEncoding = [System.Text.Encoding]::UTF8
  $trackedList = @(git -C $repoTop -c core.quotepath=off ls-files -- $traceRel 2>$null)
} finally { [Console]::OutputEncoding = $prevEnc }
if (-not $trackedList.Count) { throw "git ls-files failed（证据枚举以 git 跟踪面为基准，不可用文件系统遍历替代）" }
# 未跟踪新证据提示（F-929，2026-10-18 Agent-native 治理优化批）：
# 上面的枚举基准是 git 跟踪面，所以 `git add` 之前新增的证据不会被收录——若此时
# 做DeepEvidence审计，索引与干净检出就会失配（UI-T85 attempt 1 B-1：索引 6599 vs 实际 6605，
# 实施树 PASS、验收 worktree 六条 missing entry）。这里给出可操作提示；硬门禁
# 在 validate-docs.ps1 -DeepEvidence（普通文档检查不强制刷新索引）。
$untrackedFiles = @()
try {
  $prevEncU = [Console]::OutputEncoding
  try {
    [Console]::OutputEncoding = [System.Text.Encoding]::UTF8
    $statusRaw = @(git -C $repoTop -c core.quotepath=off status --porcelain -z -uall -- $traceRel 2>$null)
  } finally { [Console]::OutputEncoding = $prevEncU }
  foreach ($seg in (([string]::Join('', $statusRaw)) -split "`0")) {
    if ($seg.Length -gt 3 -and $seg.StartsWith('?? ')) { $untrackedFiles += $seg.Substring(3) }
  }
} catch { }
if ($untrackedFiles.Count -gt 0) {
  Write-Warning ("检测到 {0} 个未跟踪证据文件——它们不会进入索引（git ls-files 基准）。索引审计前先 git add 再重跑；validate-docs -DeepEvidence 会拒绝未跟踪证据。" -f $untrackedFiles.Count)
  $untrackedFiles | Select-Object -First 10 | ForEach-Object { Write-Warning "  untracked: $_" }
}
$files = @($trackedList | Where-Object { $_ -ne "$traceRel/evidence-index.json" } | ForEach-Object { Get-Item -LiteralPath (Join-Path $repoTop $_) } | Sort-Object { $_.FullName })
$entries = @()
$byKind = @{}; $byLifecycle = @{ current = 0; superseded = 0; historical = 0; unverified = 0 }
$shaComputed = 0; $shaSkipped = 0; $totalBytes = [long]0; $associated = 0
$shaGroups = @{}
foreach ($f in $files) {
  $rel = $f.FullName.Substring($traceDir.Length + 1).Replace('\','/')
  $ext = $f.Extension.ToLowerInvariant()
  $kind = Get-EvidenceKind $rel $ext
  $ids = Get-TaskIds $rel
  if ($ids.Count) { $associated++ }
  # 生命周期（规则见文件头）
  $lc = 'unverified'
  if ($machineCurrent -contains $rel) { $lc = 'current' }
  elseif ($openRefPaths.ContainsKey($rel)) { $lc = 'current' }
  elseif ($ids.Count -and (@($ids | Where-Object { $taskStatus.ContainsKey($_) -and $taskStatus[$_] -eq 'done' }).Count -eq $ids.Count)) { $lc = 'historical' }
  $content = Get-IndexedContent $f.FullName $shaCapBytes
  $canonicalLength = $content.Bytes
  $sha = $null
  if ($canonicalLength -le $shaCapBytes) { $sha = $content.Sha256; $shaComputed++; if (-not $shaGroups.ContainsKey($sha)) { $shaGroups[$sha] = @() }; $shaGroups[$sha] += $rel }
  else { $shaSkipped++ }
  $byKind[$kind] = 1 + $(if ($byKind.ContainsKey($kind)) { $byKind[$kind] } else { 0 })
  $byLifecycle[$lc]++
  $totalBytes += $canonicalLength
  $entries += [PSCustomObject]@{ path = $rel; kind = $kind; taskIds = $ids; bytes = $canonicalLength; sha256 = $sha; lifecycle = $lc }
}
# 重复组（同 SHA 不同路径；只列组数与规模，路径明细入 duplicateGroups，封顶 100 组防索引膨胀）
$dupGroups = @()
$dupTotal = 0
foreach ($kv in $shaGroups.GetEnumerator()) { if ($kv.Value.Count -gt 1) { $dupTotal++ } }
if ($dupTotal) {
  $i = 0
  foreach ($kv in ($shaGroups.GetEnumerator() | Sort-Object { $_.Value[0] })) {
    if ($kv.Value.Count -gt 1) { $dupGroups += [PSCustomObject]@{ sha256 = $kv.Key; paths = @($kv.Value | Sort-Object) }; $i++; if ($i -ge 100) { break } }
  }
}

$index = [PSCustomObject]@{
  schemaVersion = 'ird-evidence-index/1'
  contentBasis = 'text-crlf-to-lf-v1'
  generatedAt = (Get-Date -Format 'yyyy-MM-ddTHH:mm:sszzz')
  generatedBy = 'index-evidence.ps1（治理 Task 7）'
  lifecycleRules = [PSCustomObject]@{
    current    = '被 open finding 引用或 pipeline 机器维护载体（state.json/b1 manifest）'
    superseded = '被取代——需人工标记，首轮不自动判定'
    historical = '关联任务契约状态全部 done（已完成工作的历史留痕）'
    unverified = '无法自动判定当前性（默认；含未关联任务与任务未完成的证据）'
  }
  summary = [PSCustomObject]@{
    totalFiles = $files.Count; totalBytes = $totalBytes
    byKind = $byKind; byLifecycle = $byLifecycle
    associated = $associated; unassociated = ($files.Count - $associated)
    shaComputed = $shaComputed; shaSkippedLarge = $shaSkipped
    duplicateGroupsTotal = $dupTotal; duplicateGroupsListed = $dupGroups.Count
  }
  duplicateGroups = $dupGroups
  entries = $entries
}
[System.IO.File]::WriteAllText($indexPath, ($index | ConvertTo-Json -Depth 6), (New-Object System.Text.UTF8Encoding($false)))
Write-Output "index-evidence: $($files.Count) files, $(('{0:N1}' -f ($totalBytes/1MB))) MB -> traceability/evidence-index.json"
Write-Output "index-evidence: lifecycle current=$($byLifecycle.current) historical=$($byLifecycle.historical) unverified=$($byLifecycle.unverified); associated=$associated unassociated=$($files.Count - $associated)"
Write-Output "index-evidence: sha computed=$shaComputed skippedLarge=$shaSkipped; duplicateGroups=$dupTotal (listed $($dupGroups.Count))"
