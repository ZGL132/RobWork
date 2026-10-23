# 上下文包生成器（治理 Task 6，2026-10-17）。
#
# 职责：按任务 ID 解析契约 JSON，生成"最小充分上下文包"（context/packs/<TaskId>.md）——
# 新会话接手任务时以此包替代通读 DTB/REQUIREMENTS 全文（上下文经济学）。
# 包内容：任务身份、阅读阶梯、权威章节、代码/测试根、允许与禁止路径、验证命令、
# 已知阻塞（dependsOn 非 done ＋ open findings 引用）、源文件 git blob SHA-1 指纹（40 位——本仓对象库为 sha1 格式，非 SHA-256）。
#
# 纪律（PIPE 权威红线，评审修正 3）：本脚本不读取也不复制 traceability/pipeline/
# state.json 的任何状态字段——包内只写"流水线相位读 state.json 本体"的指引。
#
# 用法：pwsh -File build-context-pack.ps1 -TaskId CORE-T01 [-RepoRoot <RobWork 目录>]
# 产物为生成物（禁止手改）；源文件变化后重跑本脚本刷新，validate-context-pack.ps1 负责报过期。
param(
  [Parameter(Mandatory = $true)][string]$TaskId,
  [string]$RepoRoot = ""
)
$scriptDir = $PSScriptRoot
if (-not $scriptDir -or -not (Test-Path $scriptDir)) { $scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path }
if (-not $RepoRoot) { $RepoRoot = (Resolve-Path (Join-Path $scriptDir "..\..")).Path }
# RepoRoot＝RobWork/ 子目录（与 validate-docs.ps1 同口径）；仓库真正根＝其上一级（指纹表路径基准）
$base = Join-Path $RepoRoot 'doc/industrial-robot-design'
if (-not (Test-Path $base)) { throw "repo root resolution failed: doc base not found under '$RepoRoot'（请用 -RepoRoot 显式指定 RobWork 目录）" }
$repoTop = Split-Path -Parent $RepoRoot
$tasksDir = Join-Path $base 'tasks'

# 任务契约定位＋一次性构建 id→文件映射（dependsOn 状态查询复用，避免反复扫描 356 份）
$taskFiles = @(Get-ChildItem $tasksDir -Recurse -Filter '*.json' -ErrorAction SilentlyContinue | Where-Object { $_.Name -ne 'foundation-tasks.json' })
$taskMap = @{}
foreach ($f in $taskFiles) {
  $x = $null
  try { $x = Get-Content $f.FullName -Raw -Encoding UTF8 | ConvertFrom-Json } catch { $x = $null }
  if ($x -and $x.taskId) { $taskMap[$x.taskId] = $f }
}
if (-not $taskMap.ContainsKey($TaskId)) { throw "task not found: $TaskId（在 tasks/ 树中未找到对应契约 JSON）" }
$contractFile = $taskMap[$TaskId]
$contract = Get-Content $contractFile.FullName -Raw -Encoding UTF8 | ConvertFrom-Json

# ---------- 按根规则使用可选导航，不恢复已经撤销的五条强制阅读路径 ----------
$unit = "$($contract.unit)"
$readingPath = 'context/reading-paths.md 按任务性质取用（可选导航）'

# ---------- 已知阻塞：① dependsOn 非 done；② open findings 引用本任务 ----------
$blockedDeps = @()
foreach ($dep in @($contract.dependsOn)) {
  if (-not $dep) { continue }
  if ($taskMap.ContainsKey($dep)) {
    $dx = Get-Content $taskMap[$dep].FullName -Raw -Encoding UTF8 | ConvertFrom-Json
    if ("$($dx.status)" -ne 'done') { $blockedDeps += "$dep（status=$($dx.status)）" }
  } else {
    $blockedDeps += "$dep（契约未找到——登记核查）"
  }
}
$openFindings = @()
$findingsPath = Join-Path $base 'traceability/findings.json'
if (Test-Path $findingsPath) {
  $fj = Get-Content $findingsPath -Raw -Encoding UTF8 | ConvertFrom-Json
  $openFindings = @($fj.findings | Where-Object { "$($_.status)" -eq 'open' -and (("$($_.source)" -match [regex]::Escape($TaskId)) -or ("$($_.title)" -match [regex]::Escape($TaskId))) })
}

# ---------- 源文件指纹（git blob SHA-1，40 位）——validate-context-pack.ps1 的校验基准 ----------
# 指纹面＝构成本包上下文基础的规则/契约/需求文件；路径一律相对仓库真正根（$repoTop）。
# 指纹取 git hash-object（经 clean 过滤的 blob SHA-1，行尾无关）——字节级哈希在
# core.autocrlf 下随检出行尾漂移，干净树上全量误报 STALE（演练验收 F-909 教训）。
function Get-BlobSha([string]$path) { $h = git -C $script:repoTop hash-object $path 2>$null | Select-Object -First 1; if (-not $h) { throw "git hash-object failed: $path" }; return "$h".ToLowerInvariant() }
. (Join-Path $scriptDir 'context-pack-files.ps1')
$fingerprintFiles = @(Get-ContextFingerprintFiles $contract $contractFile.FullName $repoTop)
$relContract = [IO.Path]::GetFullPath($contractFile.FullName).Substring([IO.Path]::GetFullPath($repoTop).Length + 1).Replace('\','/')
$shaRows = @()
foreach ($rel in $fingerprintFiles) {
  $abs = Join-Path $repoTop $rel
  if (-not (Test-Path -LiteralPath $abs)) { throw "fingerprint source not found: $rel（designRefs 锚点或规则文件缺失，先修文档再生成包）" }
  $shaRows += "| $rel | $(Get-BlobSha $abs) |"
}

# ---------- 生成基线（git HEAD） ----------
$headSha = 'unknown'
try { $headSha = (git -C $repoTop rev-parse HEAD 2>$null | Select-Object -First 1) } catch { $headSha = 'unknown' }
if (-not $headSha) { $headSha = 'unknown' }
$now = Get-Date -Format 'yyyy-MM-ddTHH:mm:sszzz'

# ---------- 组装包内容 ----------
$unitDir = "RobWork/RobWorkStudio/src/rwslibs/industrialrobot/$unit"
$L = New-Object System.Collections.Generic.List[string]
$L.Add('<!-- 生成物：由 build-context-pack.ps1 生成，禁止手改；源文件变化后重跑生成（validate-context-pack.ps1 报过期） -->')
$L.Add('')
$L.Add("# 上下文包：$TaskId —— $($contract.title)")
$L.Add('')
$L.Add('| 字段 | 值 |')
$L.Add('| --- | --- |')
$L.Add("| 生成时间 | $now |")
$L.Add("| 生成基线 | git HEAD $headSha |")
$L.Add("| 任务状态（契约） | $($contract.status) |")
$L.Add("| 单元 / 主 WP | $unit / $($contract.masterWp) |")
$L.Add("| 分支 | $($contract.branch) |")
$L.Add("| 跨单元 | $($contract.interUnit) |")
$L.Add('')
$L.Add('## 1. 阅读阶梯（规则入口）')
$L.Add('')
$L.Add('- 仓库根 `AGENTS.md` §1：当前契约＋相关条款/源码；分区规则按实际修改区域取用；')
$L.Add("- 本任务阅读路径：**$readingPath**；")
$L.Add('- 规则执法分级：`context/rule-enforcement.md`；')
$L.Add('- **仅自动编排读取 `traceability/pipeline/state.json` 本体**（人工任务不读；本包不复制状态——PIPE 权威红线）。')
$L.Add('')
$L.Add('## 2. 权威章节（必须读，章节级取用）')
$L.Add('')
$L.Add("- 需求条目：$((@($contract.requirements) | Where-Object { $_ }) -join '、')（REQUIREMENTS.md 按 ID 检索，勿通读）；")
$L.Add("- 设计锚点：$((@($contract.designRefs) | Where-Object { $_ }) -join '；')；")
$L.Add('- DTB 任务行：development-task-breakdown.md §2 对应 WP 节内的本任务行（**只取该行**）。')
$L.Add('')
$L.Add('## 3. 代码根与改动面')
$L.Add('')
$L.Add('- 代码根：`' + $unitDir + '/`（include/sdurws/ird/' + $unit + '/、src/、test|contract_test|gui_test|perf_test 按单元实有）；')
$afLines = @($contract.allowedFiles | Where-Object { $_ }); foreach ($a in $afLines) { $L.Add('- 允许：`' + $a + '`') }
$ffLines = @($contract.forbiddenFiles | Where-Object { $_ }); foreach ($fitem in $ffLines) { $L.Add('- **禁止**：`' + $fitem + '`') }
$L.Add('')
$L.Add('## 4. 验证命令')
$L.Add('')
$vLines = @($contract.verify | Where-Object { $_ }); foreach ($v in $vLines) { $L.Add('- 契约 verify：`' + $v + '`') }
$L.Add('- 任务级：`pwsh -File RobWork/scripts/industrialrobot/verify-task.ps1 -TaskFile ' + $relContract + '`；')
$L.Add('- 门禁矩阵：ird-verify-change Skill（`RobWork/.agents/skills/ird-verify-change/SKILL.md`）。')
$L.Add('')
$L.Add('## 5. 已知阻塞')
$L.Add('')
if ($blockedDeps.Count) { foreach ($b in $blockedDeps) { $L.Add("- 前置未完成：$b") } } else { $L.Add('- 前置依赖：全部 done（无阻塞）。') }
if ($openFindings.Count) { foreach ($of in $openFindings) { $L.Add("- open finding：$($of.id)（$($of.severity)）$($of.title)") } } else { $L.Add('- open findings 引用本任务：无。') }
$L.Add('')
$L.Add('## 6. 验收要点（契约 acceptance）')
$L.Add('')
foreach ($ac in @($contract.acceptance | Where-Object { $_ })) { $L.Add("- $ac") }
$L.Add('')
$L.Add('## 7. 源文件指纹（git blob SHA-1，40 位，行尾无关；相对仓库根；validate-context-pack.ps1 校验基准——非 SHA-256，本仓对象库为 sha1 格式）')
$L.Add('')
$L.Add('| 源文件 | git blob SHA-1 |')
$L.Add('| --- | --- |')
foreach ($r in $shaRows) { $L.Add($r) }
$L.Add('')

# ---------- 落盘（覆盖式再生成——包是生成物不是历史档案） ----------
$packsDir = Join-Path $base 'context/packs'
if (-not (Test-Path $packsDir)) { New-Item -ItemType Directory -Path $packsDir | Out-Null }
$packPath = Join-Path $packsDir "$TaskId.md"
[System.IO.File]::WriteAllText($packPath, ($L -join "`r`n"), (New-Object System.Text.UTF8Encoding($false)))
Write-Output "build-context-pack: $TaskId -> context/packs/$TaskId.md (fingerprints=$($shaRows.Count), blockedDeps=$($blockedDeps.Count), openFindings=$($openFindings.Count))"
