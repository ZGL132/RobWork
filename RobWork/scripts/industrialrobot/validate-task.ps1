# 任务契约校验（治理脚本）。
#
# 原有职责：单份契约 JSON 的结构与字段完整性（taskId 句法、status 枚举、ready 契约必备字段）。
# 2026-09-10 增强（流水线 v1.3 审核修复，登记于 DTB §8）——引用完整性三查：
#   ①dependsOn 指向的任务必须真实存在（防悬空前置：守卫 4 依赖"前置存在"这一事实）；
#   ②designRefs 指向**设计文档**（需求/架构/详设索引/DTB/单元卡）时必须携带章节锚点且锚点
#     在目标文件真实存在——PIPE v1.2 阅读阶梯靠 designRefs 锚点定向阅读，锚点失真＝实施者读错
#     章节＝静默实现偏差；traceability/** 等过程留痕文档整读即可，不强制锚点；
#     锚点强制仅对 ready/done 契约生效（锚点是编译产物要求，见契约编译协议 CCP §3）；
#   ③interUnit=true 的跨单元任务必须登记 knownPitfalls（CCP §3"陷阱随契约走"：
#     已登记语义陷阱 O-xx/CR-xx/P-xx-x 必须显式进入契约，不能指望实施者通读三处登记册）。
#
# 用法：pwsh -File validate-task.ps1 -TaskFile <契约路径> [-RepoRoot <仓库根>]
# 仓库根解析与 verify-task.ps1 同一套健壮逻辑（CORE-T01 验收 G-1 教训），调用方式无关。
param(
  [Parameter(Mandatory=$true)][string]$TaskFile,
  [string]$RepoRoot = ""
)
if (-not (Test-Path $TaskFile)) { throw "task file not found: $TaskFile" }

# 仓库根解析：优先显式参数；否则脚本位置上三级（scripts/industrialrobot → 仓库根），
# 并以"doc 目录存在"自检——解析失败必须显性报错，不允许归约到盘根后静默错判（G-1 根因对策）。
if (-not $RepoRoot) {
  $scriptDir = $PSScriptRoot
  if (-not $scriptDir -or -not (Test-Path $scriptDir)) { $scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path }
  $RepoRoot = (Resolve-Path (Join-Path $scriptDir "..\..\..")).Path
}
$docBase = Join-Path $RepoRoot "RobWork/doc/industrial-robot-design"
if (-not (Test-Path $docBase)) { throw "repo root resolution failed: doc base not found under '$RepoRoot'（请用 -RepoRoot 显式指定仓库根）" }

# 锚点存在性：标题行压缩空白后须以锚点开头，且锚点后不紧跟数字（防 #2 误匹配更深的编号段；
# §2.2 归 §2 的父子前缀视为合法——父节存在性由编号体系保证）。中文锚点（如"附录C"）同样支持。
function Test-AnchorExists([string]$text, [string]$anchor) {
  $a = ($anchor -replace "\s", "")
  if (-not $a) { return $false }
  $pattern = "^#{1,6}" + [regex]::Escape($a) + "($|[^0-9])"
  foreach ($line in ($text -split "`n")) {
    $cond = ($line -replace "\s", "")
    if ($cond -match $pattern) { return $true }
  }
  return $false
}

# 需要锚点的设计文档判定：单元卡与四份主文档（需求/架构/详设索引/DTB）按语义分节阅读；
# traceability、tasks 等过程文件整读即可，不强制锚点。
function Test-AnchorRequired([string]$relPath) {
  if ($relPath -like "units/*") { return $true }
  foreach ($d in @("REQUIREMENTS.md","ARCHITECTURE.md","DETAILED-DESIGN.md","development-task-breakdown.md")) {
    if ($relPath.EndsWith($d)) { return $true }
  }
  return $false
}

# 全量契约索引（dependsOn 引用检查的数据源；数组索引文件与数组形态文件跳过，与 validate-docs 口径一致）
$allTaskIds = @{}
Get-ChildItem (Join-Path $docBase "tasks") -Recurse -Filter "*.json" -ErrorAction SilentlyContinue | ForEach-Object {
  if ($_.Name -eq "foundation-tasks.json") { return }
  $j = Get-Content $_.FullName -Raw -Encoding UTF8 | ConvertFrom-Json
  if ($j -is [array]) { return }
  if ($j.taskId) { $allTaskIds[$j.taskId] = $true }
}

$tasks = @(Get-Content $TaskFile -Raw -Encoding UTF8 | ConvertFrom-Json)
$required = 'taskId','unit','masterWp','title','status','dependsOn','requirements','designRefs','verify'
$readyRequired = 'requirements','designRefs','verify','allowedFiles','forbiddenFiles','outputs','acceptance'
foreach ($t in $tasks) {
  foreach ($k in $required) { if ($null -eq $t.$k) { throw "missing task field: $k" } }
  if ($t.taskId -notmatch '^([A-Z]+-(T[0-9]+|CR-[0-9]+)|WP-[0-9]+-T[0-9]+)$') { throw "invalid taskId: $($t.taskId)" }
  if ($t.masterWp -notmatch '^WP-[A-I]$') { throw "invalid masterWp: $($t.masterWp)" }
  if ($t.status -notin 'planned','ready','blocked','done','design-written') { throw "invalid task status: $($t.taskId)" }
  if (@($t.verify).Count -eq 0) { throw "verify must contain at least one command: $($t.taskId)" }
  # ready 表示任务可直接领取，必须具备可评审的需求、范围、产物与验收；
  # planned 允许保留任务占位，但绝不能因字段名存在而被当作已准备就绪。
  if ($t.status -eq 'ready') {
    foreach ($k in $readyRequired) {
      if (@($t.$k).Count -eq 0) { throw "ready task has empty field ${k}: $($t.taskId)" }
    }
  }

  # ①dependsOn 引用完整性（全部状态生效——悬空前置在任何阶段都是数据缺陷）
  foreach ($d in @($t.dependsOn)) {
    if (-not $allTaskIds.ContainsKey("$d")) { throw "dependsOn target not found: $d (referenced by $($t.taskId))" }
    if ("$d" -eq "$($t.taskId)") { throw "self dependency: $($t.taskId)" }
  }

  # ②designRefs 引用完整性：文件必须存在；ready/done 契约引用设计文档必须带真实锚点
  foreach ($ref in @($t.designRefs)) {
    $parts = "$ref" -split "#", 2
    $relPath = $parts[0]
    $anchor = $null; if ($parts.Count -gt 1) { $anchor = $parts[1] }
    $fullPath = Join-Path $docBase $relPath
    if (-not (Test-Path $fullPath)) { throw "designRef file not found: $ref (referenced by $($t.taskId))" }
    if (($t.status -eq 'ready' -or $t.status -eq 'done') -and (Test-AnchorRequired $relPath)) {
      if (-not $anchor) { throw "design doc designRef missing anchor (CCP §3): $ref (in $($t.taskId))" }
      $text = Get-Content $fullPath -Raw -Encoding UTF8
      if (-not (Test-AnchorExists $text $anchor)) { throw "designRef anchor not found in target: $ref (in $($t.taskId))" }
    }
  }

  # ③陷阱随契约走（CCP §3）：interUnit 是编译产物必填字段——ready/done 契约缺失该字段即校验失败
  #   （此前仅在恰为 true 时检查，139 份契约集体缺字段等于把守卫绕空——审核结论 P1-4 对策）；
  #   planned 契约允许暂缺（未编译），但若存在必须为 bool。interUnit=true ⇒ knownPitfalls 必填。
  $iuRequired = ($t.status -eq 'ready' -or $t.status -eq 'done')
  if ($iuRequired -and $null -eq $t.interUnit) { throw "ready/done task missing required bool field interUnit (CCP §3): $($t.taskId)" }
  if ($null -ne $t.interUnit -and $t.interUnit -isnot [bool]) { throw "interUnit must be bool, got '$($t.interUnit)': $($t.taskId)" }
  # 任务分支字段（PIPE v1.4 §0.2 queue 结构化条目的唯一来源）：ready 必填（进队前提）；
  # done 契约若为分支纪律之前的历史直会话执行可无 branch（如 FOUNDATION-CR-01 系），
  # 但若存在必须句法合法。句法 <前缀>-t<序号>（小写）。
  if ($t.status -eq 'ready' -and -not $t.branch) { throw "ready task missing required field branch (CCP §3): $($t.taskId)" }
  if ($t.branch -and "$($t.branch)" -notmatch '^[a-z][a-z0-9]*-t[0-9]+$') { throw "invalid branch '$($t.branch)' (expect like wp03-t01 / doc-t03): $($t.taskId)" }
  if ($t.interUnit -eq $true) {
    # PS 细节：@($null).Count 为 1，须先滤空再判空，否则缺失字段会走错报错分支
    $pitfalls = @($t.knownPitfalls) | Where-Object { $null -ne $_ }
    if ($pitfalls.Count -eq 0) { throw "interUnit task must list knownPitfalls (CCP §3): $($t.taskId)" }
    foreach ($p in $pitfalls) {
      if ("$p" -notmatch '^(O-[0-9]+|CR-[0-9]+|P-[A-Z]+-[0-9]+)$') { throw "invalid knownPitfalls id '$p' (expect O-nn / CR-nn / P-XX-nn): $($t.taskId)" }
    }
  }
}
Write-Output "validate-task: PASS ($($tasks.Count) tasks)"
