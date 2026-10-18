param(
  [string]$RepoRoot = (Resolve-Path "$PSScriptRoot\..\..").Path,
  # -DeepEvidence：证据索引深校验（路径集合＋字节数＋SHA-256 复算）——本地快速
  # 门禁默认只做路径/字节数比较；gate-all 与 CI 通道传此开关做全量内容复算
  # （2026-10-17 审查 P1-3 对策：仅比条目数无法发现"同数换内容"失配）
  [switch]$DeepEvidence
)
$base = Join-Path $RepoRoot 'doc/industrial-robot-design'
$errors = @()
# 与 index-evidence.ps1 相同的内容口径：文本 CRLF/CR 规范化为 LF，二进制按原始字节。
# 共享文本扩展名白名单单源加载（F-912——生成器与校验器同读 config/
# text-extensions.txt 一份口径，杜绝双份漂移；脚本相对解析，调用方式无关；
# 函数体内只引用，避免逐文件重读配置——6596 文件量级）。
$script:TextExtensions = @(Get-Content (Join-Path $PSScriptRoot 'config\text-extensions.txt') |
  ForEach-Object { $_.Trim() } | Where-Object { $_ -ne '' -and -not $_.StartsWith('#') })
function Get-IndexedContent([string]$LiteralPath, [switch]$WithHash) {
  $f = Get-Item -LiteralPath $LiteralPath
  [byte[]]$b = [IO.File]::ReadAllBytes($f.FullName)
  $isText = ($script:TextExtensions -contains $f.Extension.ToLowerInvariant()) -or $f.Name -eq '.gitattributes'
  if ($isText -and -not ($b -contains [byte]0)) { $enc = [Text.Encoding]::GetEncoding(28591); $b = $enc.GetBytes($enc.GetString($b).Replace("`r`n", "`n").Replace("`r", "`n")) }
  $hash = $null
  if ($WithHash) { $sha = [Security.Cryptography.SHA256]::Create(); $hash = ([BitConverter]::ToString($sha.ComputeHash($b)).Replace('-','')).ToLowerInvariant(); $sha.Dispose() }
  return @{ Bytes = $b.Length; Sha256 = $hash }
}
# PIPE/ACC 是定时执行器的可执行输入，不能只检查文件存在：以下锚点把 P0 并发临界区、
# 验收失败证据回传与 evidence 分支命名的关键语义纳入文档门禁，防止后续修订悄然删回旧规则。
$pipelineText = Get-Content (Join-Path $base 'automation-pipeline.md') -Raw -Encoding UTF8
$acceptanceText = Get-Content (Join-Path $base 'acceptance-protocol.md') -Raw -Encoding UTF8
if ($pipelineText -notmatch 'Windows 命名 mutex') { $errors += 'PIPE missing Windows named mutex critical-section rule (P0)' }
if ($pipelineText -notmatch 'lastFailureRecord') { $errors += 'PIPE missing retry failure-evidence record rule (P1)' }
if ($pipelineText -notmatch '阻塞等待返回后再次 renew') { $errors += 'PIPE missing post-wait renew rule (P0)' }
if ($pipelineText -notmatch 'b1-host-integration-batch.json') { $errors += 'PIPE missing B.1 batch manifest rule' }
if ($pipelineText -notmatch '禁止使用子智能体') { $errors += 'PIPE missing current single-session execution rule' }
$b1BatchValidator = Join-Path $RepoRoot 'RobWork/scripts/industrialrobot/validate-b1-batch.ps1'
if (-not (Test-Path $b1BatchValidator)) { $b1BatchValidator = Join-Path $RepoRoot 'scripts/industrialrobot/validate-b1-batch.ps1' }
if (-not (Test-Path $b1BatchValidator)) {
  $errors += 'missing B.1 batch validator'
} else {
  $b1Result = $null
  try { $b1Result = & $b1BatchValidator -RepoRoot $RepoRoot 2>&1 } catch { $b1Result = "ERROR: $_" }
  if ("$b1Result" -notmatch 'validate-b1-batch: PASS') { $errors += 'B.1 batch manifest validation failed' }
}
if ($acceptanceText -match '落在 `acc/<taskId>` 分支') { $errors += 'ACC still contains stale acc/<taskId> evidence-branch wording' }
if ($acceptanceText -notmatch 'acc/<taskId>/<attempt>') { $errors += 'ACC missing attempt-scoped evidence branch wording' }
$status = Get-Content (Join-Path $base 'traceability/unit-status.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$ids = @($status.units | ForEach-Object { $_.id })
if ($ids.Count -ne 20 -or ($ids | Sort-Object -Unique).Count -ne 20) { $errors += 'unit-status must contain 20 unique units' }
foreach ($u in $status.units) {
  if ($u.designCompletion -notin 'written','not-written') { $errors += "invalid designCompletion: $($u.id)" }
  if ($u.designCompletion -eq 'written' -and -not (Test-Path (Join-Path $base $u.design))) { $errors += "missing written design: $($u.design)" }
}
$taskFiles = @(Get-ChildItem (Join-Path $base "tasks") -Recurse -Filter "*.json" -ErrorAction SilentlyContinue | Where-Object { $_.Name -ne "foundation-tasks.json" })
$taskIds = @()
foreach ($f in $taskFiles) {
  $x = Get-Content $f.FullName -Raw -Encoding UTF8 | ConvertFrom-Json
  if ($x -is [array]) { $errors += "array task file must be named foundation-tasks.json: $($f.Name)"; continue }
  if ($x.taskId -notmatch "^([A-Z]+-(T[0-9]+|CR-[0-9]+)|WP-[0-9]+-T[0-9]+)$") { $errors += "invalid task file: $($f.Name)" }
  $taskIds += $x.taskId
}
if (($taskIds | Sort-Object -Unique).Count -ne $taskIds.Count) { $errors += "duplicate task ids" }
$trace = Get-Content (Join-Path $base 'traceability/requirements-to-units.json') -Raw -Encoding UTF8 | ConvertFrom-Json
foreach ($e in $trace.entries) { if ($ids -notcontains $e.unit) { $errors += "unknown unit $($e.unit)" }; if ($e.masterWp -notmatch '^WP-[A-I]$') { $errors += "invalid master WP" } }
$phaseIndexPath = Join-Path $base 'traceability/phase-one-task-index.json'
if (-not (Test-Path $phaseIndexPath)) {
  $errors += 'missing phase-one task index'
} else {
  $phaseIndex = Get-Content $phaseIndexPath -Raw -Encoding UTF8 | ConvertFrom-Json
  $phaseTaskIds = @($phaseIndex.entries | ForEach-Object { $_.taskId })
  if ($phaseTaskIds.Count -ne ($phaseTaskIds | Sort-Object -Unique).Count) { $errors += 'duplicate phase-one task ids' }
  foreach ($entry in $phaseIndex.entries) {
    if ($ids -notcontains $entry.unit) { $errors += "phase index has unknown unit: $($entry.unit)"; continue }
    $designPath = Join-Path $base $entry.design
    if (-not (Test-Path $designPath)) { $errors += "phase index design missing: $($entry.design)"; continue }
    $designText = Get-Content $designPath -Raw -Encoding UTF8
    if (-not $designText.Contains($entry.sourceRow)) { $errors += "phase index task row mismatch: $($entry.taskId)" }
  }
}
# ---------- 资产清单门禁（Agent-native 治理 Task 1，2026-10-17） ----------
# context/README.md 定义七类资产与默认上下文规则，asset-inventory.json 是机读登记簿。
# 本段把"清单完整性"变成机器检查：路径存在、分类/字段/布尔类型合法、canonicalLinks
# 可解析、设计树根级资产全覆盖——新增根级文件/目录未登记即报错，防止 Agent 上下文
# 地图缺位失真（清单字段口径见 context/README.md §6）。
$invKinds = @('authority','workflow','task-source','generated','evidence','decision','archive')
$invOwners = @('owner','pipeline','sessions','script')
$assetCount = 0
$contextReadme = Join-Path $base 'context/README.md'
$inventoryPath = Join-Path $base 'context/asset-inventory.json'
if (-not (Test-Path $contextReadme)) { $errors += 'missing context/README.md (asset classification rules)' }
if (-not (Test-Path $inventoryPath)) {
  $errors += 'missing context/asset-inventory.json'
} else {
  $inv = $null
  try { $inv = Get-Content $inventoryPath -Raw -Encoding UTF8 | ConvertFrom-Json } catch { $errors += "asset-inventory.json is not valid JSON: $_" }
  if ($inv) {
    if ($inv.schemaVersion -ne 'ird-asset-inventory/1') { $errors += "asset-inventory schemaVersion must be 'ird-asset-inventory/1', got '$($inv.schemaVersion)'" }
    $registered = @()
    foreach ($a in @($inv.assets)) {
      foreach ($f in 'path','kind','owner','defaultContext','generated','canonicalLinks') {
        if ($a.PSObject.Properties.Name -notcontains $f) { $errors += "asset-inventory entry missing field '$f': $($a.path)" }
      }
      if ($a.kind -notin $invKinds) { $errors += "asset-inventory invalid kind '$($a.kind)': $($a.path)" }
      if ($a.owner -notin $invOwners) { $errors += "asset-inventory invalid owner '$($a.owner)': $($a.path)" }
      if ($a.defaultContext -isnot [bool]) { $errors += "asset-inventory defaultContext must be boolean: $($a.path)" }
      if ($a.generated -isnot [bool]) { $errors += "asset-inventory generated must be boolean: $($a.path)" }
      if ($registered -contains $a.path) { $errors += "asset-inventory duplicate path: $($a.path)" }
      $registered += $a.path
      if (-not (Test-Path (Join-Path $base $a.path))) { $errors += "asset-inventory path not found: $($a.path)"; continue }
      # canonicalLinks：相对设计树根的路径，可带 #锚点（锚段无文件系统语义，校验时剥离只验路径存在）
      foreach ($link in @($a.canonicalLinks)) {
        if (-not (Test-Path (Join-Path $base ($link -replace '#.*$','')))) { $errors += "asset-inventory broken canonicalLink '$link' at: $($a.path)" }
      }
    }
    # 根级全覆盖：设计树根下每个文件/目录都必须已登记（-contains 大小写不敏感，匹配 Windows 文件系统语义）。
    # 点前缀条目跳过——那是工具本地目录（如 .zcode 工作流运行产物，自含 .gitignore 自忽略），
    # 不是设计资产，不进清单（context/README.md §6 规则 5）。
    foreach ($r in @(Get-ChildItem $base -Force | Where-Object { $_.Name -notlike '.*' } | ForEach-Object { $_.Name })) {
      if ($registered -notcontains $r) { $errors += "unregistered root-level asset: $r" }
    }
    $assetCount = @($inv.assets).Count
  }
}
# ---------- 分层规则入口门禁（Agent-native 治理 Task 2，2026-10-17） ----------
# 根 AGENTS.md 是唯一总入口，设计区/源码区各设一层本地 AGENTS.md（阅读阶梯见根 §6.1）。
# 本段检查两份分区规则文件存在、设计区规则写全六类权威资产与 context/ 入口链接、
# 源码区规则回指根 AGENTS.md——防止分层入口被删或断链而无人察觉（引用不复制原则：
# 分区文件只做速查与指向，全量规则仍在根 AGENTS.md，防止两份文本漂移）。
$designAgentsPath = Join-Path $base 'AGENTS.md'
if (-not (Test-Path $designAgentsPath)) {
  $errors += 'missing design-tree AGENTS.md (layered rule entry)'
} else {
  $designAgentsText = Get-Content $designAgentsPath -Raw -Encoding UTF8
  foreach ($anchor in 'REQUIREMENTS.md','ARCHITECTURE.md','development-task-breakdown.md','tasks','traceability','units','context/README.md') {
    if ($designAgentsText -notmatch [regex]::Escape($anchor)) { $errors += "design AGENTS.md missing authority link: $anchor" }
  }
}
$srcAgentsPath = Join-Path $RepoRoot 'RobWorkStudio/src/rwslibs/industrialrobot/AGENTS.md'
if (-not (Test-Path $srcAgentsPath)) {
  $errors += 'missing source-area AGENTS.md (layered rule entry)'
} else {
  $srcAgentsText = Get-Content $srcAgentsPath -Raw -Encoding UTF8
  # 回指锚兼容"根 AGENTS.md"与 Markdown 反引号写法"根 `AGENTS.md`"（\s* 吃空格、`? 吃反引号）
  if ($srcAgentsText -notmatch '根\s*`?AGENTS\.md') { $errors += 'source AGENTS.md missing root AGENTS.md back-reference' }
}
# 根 AGENTS.md 阅读阶梯回链：必须同时指向两个分区规则入口（根 §6.1 分层阅读顺序）。
# 根文件在仓库根（$RepoRoot 默认解析为 RobWork/ 子目录，故先取上一级；兼容显式传仓库根）。
$rootAgentsPath = Join-Path $RepoRoot '..\AGENTS.md'
if (-not (Test-Path $rootAgentsPath)) { $rootAgentsPath = Join-Path $RepoRoot 'AGENTS.md' }
if (-not (Test-Path $rootAgentsPath)) {
  $errors += 'root AGENTS.md not found (expected at repo root)'
} else {
  $rootAgentsText = Get-Content $rootAgentsPath -Raw -Encoding UTF8
  if ($rootAgentsText -notmatch [regex]::Escape('doc/industrial-robot-design/AGENTS.md')) { $errors += 'root AGENTS.md missing design-tree AGENTS.md reading-order link' }
  if ($rootAgentsText -notmatch [regex]::Escape('rwslibs/industrialrobot/AGENTS.md')) { $errors += 'root AGENTS.md missing source-area AGENTS.md reading-order link' }
}
# ---------- Agent Notes 决策记录门禁（Agent-native 治理 Task 4，2026-10-17） ----------
# .agents/notes/ 是长期决策叙事（治理原则 5：决策即资产）。本段校验目录结构、六段式
# 齐全与 implemented 禁计划语气（启发式黑名单——机器拦明显违例，真把关在验收 review）。
# 权威分工：ARCHITECTURE 仍是规则 normative 权威，Notes 只承载叙事不复制规则正文。
$notesBase = Join-Path $RepoRoot '.agents/notes'
$noteSections = @('## Problem','## Decision','## Alternatives considered','## Consequences','## Verification','## Related sources')
if (-not (Test-Path (Join-Path $notesBase 'README.md'))) { $errors += 'missing .agents/notes/README.md (decision record format)' }
$noteCount = 0
foreach ($sd in 'implemented','proposed','rejected','archived') {
  # archived 同受六段式/编号校验（审查 P2 对策，2026-10-17——目录规范四态齐查），
  # 另要求注明取代关系（notes/README §2：顶部注明被哪条取代）；计划语气黑名单
  # 仅约束 implemented（archived 陈述的是已生效后被取代的事实）
  $dir = Join-Path $notesBase $sd
  if (-not (Test-Path $dir)) { $errors += "missing notes status dir: $sd"; continue }
  foreach ($n in @(Get-ChildItem $dir -Filter '*.md' -ErrorAction SilentlyContinue)) {
    $noteCount++
    $nt = Get-Content $n.FullName -Raw -Encoding UTF8
    foreach ($sec in $noteSections) {
      if ($nt -notmatch [regex]::Escape($sec)) { $errors += "note missing section '$sec': $($sd)/$($n.Name)" }
    }
    if ($nt -notmatch 'IRD-NOTE-') { $errors += "note missing IRD-NOTE id: $($sd)/$($n.Name)" }
    if ($sd -eq 'archived' -and $nt -notmatch '取代') { $errors += "archived note missing supersession pointer: $($n.Name)（notes/README §2）" }
    if ($sd -eq 'implemented') {
      foreach ($w in '待实现','将落位','尚未实现','计划中','拟实现') {
        if ($nt -match $w) { $errors += "implemented note uses plan-tone wording '$w': $($n.Name)" }
      }
    }
  }
}
# ---------- 执行协议 Skills 门禁（Agent-native 治理 Task 5，2026-10-17） ----------
# 六个阶段 Skill 是执行协议的操作化载体（验收口径＝新会话按 AGENTS.md 入口能找到并
# 照执行，不要求运行端自动触发）。本段校验六件 SKILL.md 存在且 frontmatter 含
# name/description（最低可用格式）；协议权威仍在 PIPE/ACC/CCP 与根 AGENTS，Skill 只是速查。
# ird-python313-dll（2026-10-18 治理批增）：python313.dll 运行期缺失处置规程——
# DTB §5.1「fresh 树运行时前置」③行的操作面唯一 home（F-428/F-440/F-715/F-828 族），
# AGENTS §4.1 fresh 树行挂引用、asset-inventory canonicalLinks 登记。
$skillNames = @('ird-discover-change','ird-design-change','ird-implement-task','ird-verify-change','ird-qt-gui-windows','ird-handoff','ird-python313-dll')
$skillsBase = Join-Path $RepoRoot '.agents/skills'
$skillCount = 0
foreach ($sn in $skillNames) {
  $sp = Join-Path $skillsBase "$sn/SKILL.md"
  if (-not (Test-Path $sp)) { $errors += "missing skill file: $sn/SKILL.md"; continue }
  $skillCount++
  $head = (Get-Content $sp -TotalCount 15) -join "`n"
  if ($head -notmatch 'name:\s*\S') { $errors += "skill frontmatter missing name: $sn" }
  if ($head -notmatch 'description:\s*\S') { $errors += "skill frontmatter missing description: $sn" }
}
# ---------- 上下文包门禁（Agent-native 治理 Task 6，2026-10-17） ----------
# context/packs/ 是脚本生成的按任务上下文包（生成物禁手改）。本段轻量校验生成器
# 标记与指纹段头——防手改伪装；新鲜度（git blob SHA-1 重算报 STALE）与 PIPE 权威红线
# （包内禁 state.json 状态字段副本）由专用脚本 validate-context-pack.ps1 承担
# （过期是预期状态，不在此失败——包按需重生成）。
$packsDir = Join-Path $base 'context/packs'
$packCount = 0
if (Test-Path $packsDir) {
  foreach ($pk in @(Get-ChildItem $packsDir -Filter '*.md' -ErrorAction SilentlyContinue)) {
    $packCount++
    $pt = Get-Content $pk.FullName -Raw -Encoding UTF8
    if ($pt -notmatch 'build-context-pack\.ps1') { $errors += "pack missing generator marker: $($pk.Name)" }
    if ($pt -notmatch '源文件指纹') { $errors += "pack missing blob-SHA fingerprint section: $($pk.Name)" }
    if ($pt -notmatch '(?m)^\|\s*源文件\s*\|\s*git blob SHA-1\s*\|') { $errors += "pack fingerprint table header must be 'git blob SHA-1': $($pk.Name)" }
  }
}
# ---------- 证据索引门禁（Agent-native 治理 Task 7，2026-10-17） ----------
# traceability/ 是只增证据库，evidence-index.json 是其摘要索引（Agent 检索默认入口，
# 生成物禁手改）。本段校验：索引存在、schema 正确、条目数与实际文件数一致——
# 新证据落库未重跑 index-evidence.ps1 即失败（报错附重跑指引）。生命周期与删除
# 前置规则见 traceability/README.md。
$evidenceReadme = Join-Path $base 'traceability/README.md'
if (-not (Test-Path $evidenceReadme)) { $errors += 'missing traceability/README.md (evidence lifecycle rules)' }
$evidenceIndexPath = Join-Path $base 'traceability/evidence-index.json'
$evidenceCount = 0
if (-not (Test-Path $evidenceIndexPath)) {
  $errors += 'missing traceability/evidence-index.json (run: pwsh -File RobWork/scripts/industrialrobot/index-evidence.ps1)'
} else {
  $ei = $null
  try { $ei = Get-Content $evidenceIndexPath -Raw -Encoding UTF8 | ConvertFrom-Json } catch { $errors += "evidence-index.json is not valid JSON: $_" }
  if ($ei) {
    if ($ei.schemaVersion -ne 'ird-evidence-index/1') { $errors += "evidence-index schemaVersion must be 'ird-evidence-index/1', got '$($ei.schemaVersion)'" }
    if ($ei.contentBasis -ne "text-crlf-to-lf-v1") { $errors += "evidence index contentBasis must be text-crlf-to-lf-v1——重跑 index-evidence.ps1" }
    # 计数基准＝git 跟踪面（与 index-evidence.ps1 一致；文件系统遍历会计入
    # gitignored 产物导致干净检出失配——F-908 教训）
    $repoTopEv = Split-Path -Parent $RepoRoot
    $prevEncEv = [Console]::OutputEncoding
    try { [Console]::OutputEncoding = [System.Text.Encoding]::UTF8; $actualEvidence = @(git -C $repoTopEv -c core.quotepath=off ls-files -- "RobWork/doc/industrial-robot-design/traceability" 2>$null | Where-Object { $_ -ne 'RobWork/doc/industrial-robot-design/traceability/evidence-index.json' }) } finally { [Console]::OutputEncoding = $prevEncEv }
    # 未跟踪证据检测（F-929，2026-10-18 Agent-native 治理优化批）：
    # 索引与校验都以 git ls-files（跟踪面）为基准，新证据在 `git add` 之前对两者
    # 都"隐形"——实施树 validate-docs 照样 PASS，换到干净检出/验收 worktree 才暴露
    # 6 条 missing entry（UI-T85 attempt 1 B-1 的实际形态：索引 6599 vs 实际 6605）。
    # 这里对 traceability/ 下任何未跟踪文件硬失败，把"先 git add、再索引、再校验"
    # 的顺序变成门禁而不是口头纪律；gitignored 产物不在 git status 默认输出中，
    # 不会误伤（F-908 的口径不变）。
    $untrackedEvidence = @()
    try {
      $prevEncU = [Console]::OutputEncoding
      try {
        [Console]::OutputEncoding = [System.Text.Encoding]::UTF8
        $statusRaw = @(git -C $repoTopEv -c core.quotepath=off status --porcelain -z -uall -- "RobWork/doc/industrial-robot-design/traceability" 2>$null)
      } finally { [Console]::OutputEncoding = $prevEncU }
      foreach ($seg in (([string]::Join('', $statusRaw)) -split "`0")) {
        # porcelain -z 每条为 "XY <path>"；只关心未跟踪 "?? "（新增 staged 的 "A " 已被 ls-files 收录）
        if ($seg.Length -gt 3 -and $seg.StartsWith('?? ')) { $untrackedEvidence += $seg.Substring(3) }
      }
    } catch { }
    if ($untrackedEvidence.Count -gt 0) {
      foreach ($u in ($untrackedEvidence | Select-Object -First 10)) { $errors += "untracked evidence file (git add 后重跑 index-evidence.ps1): $u" }
      if ($untrackedEvidence.Count -gt 10) { $errors += "untracked evidence total: $($untrackedEvidence.Count)（仅列前 10）——先 git add 再重跑 index-evidence.ps1" }
    }
    $evidenceCount = @($ei.entries).Count
    if ($evidenceCount -ne $actualEvidence.Count) { $errors += "evidence index out of date: index=$evidenceCount actual=$($actualEvidence.Count)——重跑 index-evidence.ps1" }
    # 深比较（审查 P1-3 对策，2026-10-17）：数量一致不足以证明内容一致——路径被
    # 交换/文件被同数替换时纯计数门禁失效。基准层：规范化路径集合双向比对＋逐
    # 条目字节数；-DeepEvidence（gate-all/CI）追加索引中 sha256 非 null 者的
    # SHA-256 复算。差异报错封顶 10 条＋总数，防错误刷屏。
    $tracePrefixEv = 'RobWork/doc/industrial-robot-design/traceability/'
    $indexByPath = @{}
    foreach ($e in @($ei.entries)) { $indexByPath["$($e.path)"] = $e }
    $mismatchCap = 10; $mismatchSeen = 0
    foreach ($ap in $actualEvidence) {
      $relEv = $ap.Substring($tracePrefixEv.Length)
      if (-not $indexByPath.ContainsKey($relEv)) {
        $mismatchSeen++; if ($mismatchSeen -le $mismatchCap) { $errors += "evidence index missing entry: $relEv——重跑 index-evidence.ps1" }
      } else {
        $entryEv = $indexByPath[$relEv]
        $absEv = Join-Path $repoTopEv $ap
        $contentEv = Get-IndexedContent $absEv -WithHash:$DeepEvidence
        $actualLen = $contentEv.Bytes
        if ([long]$entryEv.bytes -ne $actualLen) {
          $mismatchSeen++; if ($mismatchSeen -le $mismatchCap) { $errors += "evidence index size mismatch: $relEv (index=$($entryEv.bytes) actual=$actualLen)——重跑 index-evidence.ps1" }
        } elseif ($DeepEvidence -and "$($entryEv.sha256)") {
          $shaNow = $contentEv.Sha256
          if ($shaNow -ne "$($entryEv.sha256)") {
            $mismatchSeen++; if ($mismatchSeen -le $mismatchCap) { $errors += "evidence index sha mismatch: $relEv——内容已变，重跑 index-evidence.ps1" }
          }
        }
      }
    }
    foreach ($ip in @($indexByPath.Keys)) {
      $fullEv = $tracePrefixEv + "$ip"
      if ($actualEvidence -notcontains $fullEv) {
        $mismatchSeen++; if ($mismatchSeen -le $mismatchCap) { $errors += "evidence index stale entry: $ip（实际文件已不存在）——重跑 index-evidence.ps1" }
      }
    }
    if ($mismatchSeen -gt $mismatchCap) { $errors += "evidence index mismatch total: $mismatchSeen（仅列前 $mismatchCap 条）——重跑 index-evidence.ps1" }
  }
}
# ---------- findings 台账一致性门禁（F-930，2026-10-18 Agent-native 治理优化批） ----------
# findings.json 是验收/治理发现的唯一登记簿（ACC §5 强制转登），但此前只被装配
# 清单的 waiver 联动读取，字段一致性无人校验。UI-T85 返工批出现过"resolution＋
# fixedAt 已写、status 仍 open"的漂移（commit 声称已翻 fixed，台账实际未闭合），
# 而验收 4.8 恰以 open/fixed 状态做消账核对——状态字段失真会让核对结论整体失真。
# 本段校验四类不变量：
#   ① id 唯一且句法 F-nnn（编号是跨文档引用键，撞号/畸变会让引用指向错条目）；
#   ② status ∈ open/fixed/closed、severity ∈ 四档（枚举外值无法被下游消费）；
#   ③ registeredAt/fixedAt 为 yyyy-MM-dd（排序与"本轮是否已消账"判定的基础）；
#   ④ fixedAt 与 status 互斥一致：写了 fixedAt 就必须 status=fixed。
# ④ 是本批的直接动因；历史存量里"fixed 但无 fixedAt"（112 条）不追罚——那属于
# 早期台账格式，强制补齐会制造 112 处无意义改写（PA-2 精神：只增不改）。
$findingsPath = Join-Path $base 'traceability/findings.json'
if (-not (Test-Path $findingsPath)) {
  $errors += 'missing traceability/findings.json'
} else {
  $fjCheck = $null
  try { $fjCheck = Get-Content $findingsPath -Raw -Encoding UTF8 | ConvertFrom-Json } catch { $errors += "findings.json is not valid JSON: $_" }
  if ($fjCheck) {
    if ($fjCheck.schemaVersion -ne 'ird-findings/1') { $errors += "findings schemaVersion must be 'ird-findings/1', got '$($fjCheck.schemaVersion)'" }
    $seenFindingIds = @{}
    $findingCount = 0
    foreach ($fd in @($fjCheck.findings)) {
      $findingCount++
      $fid = "$($fd.id)"
      if ($fid -notmatch '^F-\d{3}$') { $errors += "findings invalid id: '$fid'" }
      if ($seenFindingIds.ContainsKey($fid)) { $errors += "findings duplicate id: $fid" } else { $seenFindingIds[$fid] = $true }
      if ("$($fd.status)" -notin @('open','fixed','closed')) { $errors += "findings invalid status: $fid ($($fd.status))" }
      if ("$($fd.severity)" -notin @('blocking','major','minor','suggestion')) { $errors += "findings invalid severity: $fid ($($fd.severity))" }
      if ("$($fd.registeredAt)" -and "$($fd.registeredAt)" -notmatch '^\d{4}-\d{2}-\d{2}$') { $errors += "findings invalid registeredAt: $fid ($($fd.registeredAt))" }
      if ("$($fd.fixedAt)") {
        if ("$($fd.fixedAt)" -notmatch '^\d{4}-\d{2}-\d{2}$') { $errors += "findings invalid fixedAt: $fid ($($fd.fixedAt))" }
        if ("$($fd.status)" -ne 'fixed') { $errors += "findings status/fixedAt inconsistent: $fid status=$($fd.status) fixedAt=$($fd.fixedAt)——消账必须同批把 status 翻 fixed" }
      }
    }
    if ($findingCount -eq 0) { $errors += 'findings.json has no entries' }
  }
}
# ---------- 生产装配清单门禁（装配完备性 Tier A 结构面，T1/IRD-NOTE-006 裁决通过） ----------
# production-assembly-manifest.json 是"当前产品配置必需装配集合"的唯一权威。本段做
# 结构面：schema／moduleId 唯一／wired 锚点格式与文件存在／wired 不得携带豁免／
# required-unwired 必须挂 waiver 且 waiver 引用的 finding 必须 open——finding 关闭而
# 豁免未删＝失败（豁免自动失效，防永久后门）。调用点扫描面归 T2 契约测试，
# 真实装配断言归 Tier B（F-910/F-911 修复批 DoD）。
$manifestPath = Join-Path $base 'traceability/production-assembly-manifest.json'
if (-not (Test-Path $manifestPath)) {
  $errors += 'missing traceability/production-assembly-manifest.json (ird-assembly-manifest/1)'
} else {
  $am = $null
  try { $am = Get-Content $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json } catch { $errors += "production-assembly-manifest.json is not valid JSON: $_" }
  if ($am) {
    if ($am.schemaVersion -ne 'ird-assembly-manifest/1') { $errors += "assembly manifest schemaVersion must be 'ird-assembly-manifest/1', got '$($am.schemaVersion)'" }
    # waiver 联动需要 findings 状态表
    $findingsStatus = @{}
    try { $fsj = Get-Content (Join-Path $base 'traceability/findings.json') -Raw -Encoding UTF8 | ConvertFrom-Json; foreach ($fd in @($fsj.findings)) { $findingsStatus["$($fd.id)"] = "$($fd.status)" } } catch { $errors += 'findings.json unparsable (assembly waiver 联动需要)' }
    $irdRoot = Join-Path $RepoRoot 'RobWorkStudio/src/rwslibs/industrialrobot'
    $seenModuleIds = @()
    foreach ($m in @($am.draftModules)) {
      if ($seenModuleIds -contains "$($m.moduleId)") { $errors += "assembly manifest duplicate moduleId: $($m.moduleId)" }
      $seenModuleIds += "$($m.moduleId)"
      if ($m.wired) {
        if (-not "$($m.wiredAnchor)" -or "$($m.wiredAnchor)" -notmatch '^[\w./\\-]+:\d+$') { $errors += "assembly manifest wired module missing/invalid wiredAnchor (<路径>:<行号>): $($m.moduleId)" }
        elseif (-not (Test-Path (Join-Path $irdRoot ($m.wiredAnchor -replace ':\d+$','')))) { $errors += "assembly manifest wiredAnchor file not found: $($m.wiredAnchor)" }
        if ($m.waiver) { $errors += "assembly manifest wired module must not carry waiver: $($m.moduleId)" }
      } elseif ($m.required) {
        if (-not $m.waiver -or -not "$($m.waiver.finding)") { $errors += "assembly manifest required-but-unwired module must carry waiver: $($m.moduleId)" }
        else {
          $fst = $findingsStatus["$($m.waiver.finding)"]
          if ($null -eq $fst) { $errors += "assembly manifest waiver references unknown finding: $($m.waiver.finding) ($($m.moduleId))" }
          elseif ($fst -ne 'open') { $errors += "assembly manifest waiver expired (finding $($m.waiver.finding)=$fst)——豁免随 finding 关闭自动失效: $($m.moduleId)" }
        }
      }
    }
  }
}
if ($errors.Count) { $errors | ForEach-Object { Write-Error $_ }; exit 1 }
Write-Output "validate-docs: PASS ($($ids.Count) units, $($trace.entries.Count) trace entries, $($taskFiles.Count) task files, $assetCount registered assets, $noteCount notes, $skillCount skills, $packCount packs, $evidenceCount evidence)"
