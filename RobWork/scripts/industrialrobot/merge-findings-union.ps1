# findings.json 合并冲突确定性并集解算（治理脚本）。
#
# 职责：automation-pipeline.md（PIPE）§4.7③ v1.13 唯一例外的执行面——收尾"按 acceptedHead
# 合入代码"（git merge --no-ff <acceptedHead>）时，冲突文件仅为 traceability/findings.json
# 的场景按确定性规则自动解算；其余一切冲突照旧"即停转 blocked"，本脚本不被调用。
#
# 背景（F-204，PIPE v1.13）：findings.json 是实施端（任务分支自登记 ird_gates 命中等）与
# 验收端（evidence 分支基于 origin/redesign-main 建出、看不见任务分支已用编号、按主线尾部
# 顺延取号）的**双端并行追加**登记簿；收尾次序先②合 evidence 再③合任务分支，第二笔合并的
# 尾部 hunk 必然重叠——IO-T04/T06/T07 三连 blocked 且三次所有者裁决结论完全一致（条目并集、
# 编号序），属可机械化的确定性冲突。本脚本把该裁决固化为机器规则；真分歧（撞号/双方异改
# 同一条目/删除）仍退出非 0，合并现场原样保留，由 tick 报 blocked 等所有者。
#
# 算法（纯文本级、零重序列化——防 pwsh ConvertFrom-Json 的 ISO 日期归一化与全文件格式漂移，
# 解算 diff 只含真实变化条目）：
#   以 merge-base(:1) 的条目集合为参照，对双方(:2 我方 / :3 对方)条目块（"    {"～"    },"
#   行区间，块内文本原样保留）做集合运算：
#   ①双方各自新增条目 → 全部并存，按 id 数字序并入条目区（稳定排序：我方既有条目相对序不变）；
#   ②同号新增且块文本一致 → 取我方（跳过对方副本）；
#   ③对方独改既有条目（我方未动）→ 按对方块替换（与 git hunk 级合并语义同型）；
#   ④以下任一情形退出码 1（超出确定性范围，照旧即停）：
#     同号异义（撞号）/ 任一方删除既有条目 / 双方异改同一条目 /
#     顶层字段（schemaVersion、note）不一致 / 文件形状异常 / 解算后校验失败。
#   解算成功 = 重写工作区文件 + git add，退出 0。
#
# 用法（tick 在仓库根执行；PIPE §4.7③ 逐字登记）：
#   pwsh -File RobWork/scripts/industrialrobot/merge-findings-union.ps1
# 退出码：0＝已解算并暂存；1＝超出确定性范围，须所有者裁决（blocked）；2＝环境/调用错误。
param(
  [string]$RepoRoot = "",
  [string]$Path = "RobWork/doc/industrial-robot-design/traceability/findings.json"
)

$ErrorActionPreference = "Stop"

function Fail([int]$code, [string]$reason) {
  Write-Output ("NOT-RESOLVED: " + $reason)
  exit $code
}

# 仓库根解析：与 validate-state.ps1 同一套健壮逻辑（CORE-T01 验收 G-1 教训），与调用方式无关
if (-not $RepoRoot) {
  $scriptDir = $PSScriptRoot
  if (-not $scriptDir -or -not (Test-Path $scriptDir)) { $scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path }
  $RepoRoot = (Resolve-Path (Join-Path $scriptDir "..\..\..")).Path
}
$absPath = Join-Path $RepoRoot ($Path -replace '/', '\')
# git show 原生输出含中文，须以 UTF-8 解码，否则块比对/写回会失真
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

# ---------- ① 读取三方暂存条目（仅合并冲突现场存在 :1/:2/:3，否则视为调用错误） ----------
function Get-StageLines([string]$stage) {
  $lines = @(& git -C $RepoRoot show ("{0}:{1}" -f $stage, $Path) 2>$null)
  if ($LASTEXITCODE -ne 0) { return $null }
  return ,@($lines)
}
$baseLines = Get-StageLines ":1"
$oursLines = Get-StageLines ":2"
$theirsLines = Get-StageLines ":3"
if ($null -eq $baseLines -or $null -eq $oursLines -or $null -eq $theirsLines) {
  Fail 2 "路径无三方暂存条目（:1/:2/:3 不全）——本脚本仅用于 §4.7③ 合并冲突现场：$Path"
}

# ---------- ② 条目块扫描：id → 块行区间（登记簿条目全为字符串字段、无嵌套，行形状固定） ----------
function Get-EntryMap([string[]]$lines, [string]$side) {
  $map = [ordered]@{}
  $order = @()
  $i = 0
  while ($i -lt $lines.Count) {
    if ($lines[$i] -ceq '    {') {
      $j = $i
      while ($j -lt $lines.Count -and ($lines[$j] -cnotmatch '^    \},?$')) { $j++ }
      if ($j -ge $lines.Count) { Fail 1 "[$side] 第 $($i + 1) 行起的条目块未闭合——文件形状超出自动解算范围" }
      $idVal = $null
      for ($k = $i; $k -le $j; $k++) {
        if ($lines[$k] -cmatch '^      "id": "([^"]+)",?$') { $idVal = $Matches[1]; break }
      }
      if ($null -eq $idVal) { Fail 1 "[$side] 第 $($i + 1) 行起的条目块缺 id 字段——文件形状超出自动解算范围" }
      if ($map.Contains($idVal)) { Fail 1 "[$side] 重复条目 id=$idVal" }
      $map[$idVal] = @{ Start = $i; End = $j; Lines = $lines[$i..$j] }
      $order += $idVal
      $i = $j + 1
    } else { $i++ }
  }
  return @{ Map = $map; Order = @($order) }
}

# 块归一化比较：剥掉末行行尾逗号后按原文比对（大小写敏感——中文与标识符不容 Case 折叠）
function Get-NormBlock($block) {
  $ls = [System.Collections.Generic.List[string]]::new()
  $ls.AddRange([string[]]$block.Lines)
  $ls[$ls.Count - 1] = ($ls[$ls.Count - 1] -creplace ',$', '')
  return ($ls -join "`n")
}

# id 排序键：取尾部数字（F-203 → 203）；无数字 id 排最末（实际登记簿全为 F-编号，纯兜底）
function Get-SortKey([string]$id) {
  if ($id -cmatch '(\d+)$') { return [long]$Matches[1] }
  return [long]9223372036854775807
}

$base = Get-EntryMap $baseLines "base"
$ours = Get-EntryMap $oursLines "ours"
$theirs = Get-EntryMap $theirsLines "theirs"
if ($ours.Order.Count -eq 0) { Fail 1 "我方侧未扫得任何条目块——文件形状超出自动解算范围" }

# ---------- ③ 顶层字段一致性（findings 之外仅 schemaVersion/note 两键，任一不一致即真分歧） ----------
# git show 的行流会把文件头 UTF-8 BOM 以 U+FEFF 文本字符附着在首行——ConvertFrom-Json
# 不接受前导 BOM（F-262 实录：UI-T03 收尾首次触发）。解析前剥离属纯机械适配：
# 集合运算输入与比对语义零变化，两侧防护守卫原样生效
$oursJson = (($oursLines -join "`n") -replace "^\uFEFF", "") | ConvertFrom-Json
$theirsJson = (($theirsLines -join "`n") -replace "^\uFEFF", "") | ConvertFrom-Json
if (-not $oursJson.findings -or -not $theirsJson.findings) { Fail 1 "任一侧缺 findings 数组" }
if ("$($oursJson.schemaVersion)" -ne "$($theirsJson.schemaVersion)") {
  Fail 1 "顶层 schemaVersion 双方不一致——超出自动解算范围"
}
if ("$($oursJson.note)" -ne "$($theirsJson.note)") {
  Fail 1 "顶层 note 双方不一致——超出自动解算范围"
}

# ---------- ④ 集合运算 ----------
# 删除守卫：登记簿语义为"只增不改"式追加，任一方删除既有条目都属结构异常（IO-T06 先例的
# 状态翻转是"改"而非"删"，不触发本守卫）
foreach ($id in $base.Order) {
  if (-not $ours.Map.Contains($id)) { Fail 1 "我方删除了既有条目 $id——超出追加型范围" }
  if (-not $theirs.Map.Contains($id)) { Fail 1 "对方删除了既有条目 $id——超出追加型范围" }
}
# 共有条目守卫：双方一致/仅我方改 → 我方文本已含正确版本；仅对方改 → 记入替换集；
# 双方异改 → 真分歧，即停
$toReplace = @()
foreach ($id in $base.Order) {
  $o = Get-NormBlock $ours.Map[$id]
  $t = Get-NormBlock $theirs.Map[$id]
  $b = Get-NormBlock $base.Map[$id]
  if ($o -ceq $t) { continue }                     # 双方一致（含同翻转到同一内容）
  if ($t -ceq $b) { continue }                     # 仅我方改——保留我方
  if ($o -ceq $b) { $toReplace += $id; continue }  # 仅对方改——按对方版本替换
  Fail 1 "条目 $id 双方异改（真分歧）——停转 blocked 等所有者裁决"
}
# 新增条目：对方新增且我方没有 → 并入；同号新增 → 内容一致取一，异义即撞号（编号空间双端
# 并行分配的事故面，属语义冲突必须人工裁决——F-198"跳号顺延避让"即人工避让实录）
$append = @()
foreach ($id in $theirs.Order) {
  if ($base.Map.Contains($id)) { continue }
  if ($ours.Map.Contains($id)) {
    if ((Get-NormBlock $theirs.Map[$id]) -ceq (Get-NormBlock $ours.Map[$id])) { continue }
    Fail 1 "新增条目撞号且内容异义：$id——停转 blocked 等所有者裁决"
  }
  $append += $id
}
$append = @($append | Sort-Object { '{0:D12}|{1}' -f (Get-SortKey $_), $_ })
if ($append.Count -eq 0 -and $toReplace.Count -eq 0) {
  Fail 1 "无并集增量且无对方独改条目，但文件存在冲突——超出自动解算范围"
}

# ---------- ⑤ 重组条目区：以我方条目区整段替换为"我方原序（稳定）＋新增条目按编号序插入" ----------
$merged = [System.Collections.Generic.List[object]]::new()
$seq = 0
foreach ($id in $ours.Order) {
  $merged.Add([pscustomobject]@{ Id = $id; UseTheirs = ($toReplace -contains $id); Seq = $seq; Key = (Get-SortKey $id) })
  $seq++
}
foreach ($id in $append) {
  $merged.Add([pscustomobject]@{ Id = $id; UseTheirs = $true; Seq = $seq; Key = (Get-SortKey $id) })
  $seq++
}
$mergedSorted = @($merged | Sort-Object { '{0:D12}|{1:D8}' -f $_.Key, $_.Seq })

$body = [System.Collections.Generic.List[string]]::new()
$n = $mergedSorted.Count
for ($m = 0; $m -lt $n; $m++) {
  $e = $mergedSorted[$m]
  $src = if ($e.UseTheirs) { $theirs.Map[$e.Id] } else { $ours.Map[$e.Id] }
  $blk = [System.Collections.Generic.List[string]]::new()
  $blk.AddRange([string[]]$src.Lines)
  # 逗号统一重排：块尾逗号先剥掉，再按"末条目不带逗号、其余带"重建——与登记簿既有排版逐字节同型
  $suffix = ','
  if ($m -eq ($n - 1)) { $suffix = '' }
  $blk[$blk.Count - 1] = (($blk[$blk.Count - 1] -creplace ',$', '') + $suffix)
  foreach ($l in $blk) { $body.Add($l) }
}
$outLines = [System.Collections.Generic.List[string]]::new()
$outLines.AddRange([string[]]$oursLines)
$regionStart = $ours.Map[$ours.Order[0]].Start
$regionEnd = $ours.Map[$ours.Order[$ours.Order.Count - 1]].End
$outLines.RemoveRange($regionStart, $regionEnd - $regionStart + 1)
$outLines.InsertRange($regionStart, $body.ToArray())

# ---------- ⑥ 写前三重校验（JSON 解析＋无重复 id＋条目计数），全过才写盘并暂存 ----------
$resultText = ($outLines -join "`n") + "`n"
$parsed = $null
# 校验同样剥前导 BOM（输入带 BOM 时 resultText 首字符为 U+FEFF）；写盘用无 BOM 编码器——
# 首行附着的 U+FEFF 字符本身会被编码为 BOM 字节，输入侧 BOM 状态据此零漂移保留
try { $parsed = ($resultText -replace "^\uFEFF", "") | ConvertFrom-Json } catch { Fail 1 "解算结果 JSON 解析失败：$($_.Exception.Message)" }
$finalIds = @($parsed.findings | ForEach-Object { $_.id })
$expected = $ours.Order.Count + $append.Count
if ($finalIds.Count -ne $expected) { Fail 1 "条目计数不符：期望 $expected 实得 $($finalIds.Count)" }
if (@($finalIds | Select-Object -Unique).Count -ne $finalIds.Count) { Fail 1 "解算后出现重复 id" }

[System.IO.File]::WriteAllText($absPath, $resultText, (New-Object System.Text.UTF8Encoding($false)))
& git -C $RepoRoot add -- $Path | Out-Null
if ($LASTEXITCODE -ne 0) { Fail 1 "git add 失败：$Path" }
$repTxt = ''
if ($toReplace.Count -gt 0) { $repTxt = '、对方独改替换 ' + $toReplace.Count + ' 条（' + ($toReplace -join ',') + '）' }
Write-Output ("RESOLVED: $Path —— 并集新增 " + $append.Count + " 条（" + ($append -join ',') + "）" + $repTxt + "，总计 " + $finalIds.Count + " 条；已写入并 git add")
exit 0
