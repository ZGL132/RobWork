# 契约 verify 命令执行器（治理脚本）。
#
# 用法：pwsh -File verify-task.ps1 -TaskFile <契约路径> [-RepoRoot <仓库根>] [-DryRun]
# verify 命令相对 git 仓库根书写（DTB §5.1：pwsh -File RobWork/scripts/... 与 cmake --build build）。
#
# 修复史（CORE-T01 验收 G-1 / findings F-001）：
#   - 原 ..\..（<repo>/RobWork）导致 verify 命令路径解析失败 → 改上三级；
#   - -File 调用路径下 $PSScriptRoot 参与参数默认值求值时可能为空 → RepoRoot 归约到盘根
#     （D:\）→ cmake 报错。现改为"显式参数 > $PSScriptRoot > MyInvocation 回退"三段解析，
#     并以"doc 目录存在性"自检兜底——解析失败显性报错，杜绝静默错判；
#     顺带修复行尾字面 `\n` 输出瑕疵。两种调用方式（-File / -Command &）行为一致。
# -DryRun：仅打印解析后的仓库根与将要执行的 verify 命令，不实际执行——用于调用方式/环境自检。
param(
  [Parameter(Mandatory=$true)][string]$TaskFile,
  [string]$RepoRoot = "",
  [switch]$DryRun
)
if (-not (Test-Path $TaskFile)) { throw "task file not found: $TaskFile" }

# 仓库根解析（与 validate-task.ps1 同一套逻辑，保持两个脚本行为一致）
$scriptDir = $PSScriptRoot
if (-not $scriptDir -or -not (Test-Path $scriptDir)) { $scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path }
if (-not $RepoRoot) { $RepoRoot = (Resolve-Path (Join-Path $scriptDir "..\..\..")).Path }
if (-not (Test-Path (Join-Path $RepoRoot "RobWork/doc/industrial-robot-design"))) {
  throw "repo root resolution failed: doc base not found under '$RepoRoot'（请用 -RepoRoot 显式指定仓库根）"
}
Write-Output "verify-task: RepoRoot=$RepoRoot"

& (Join-Path $scriptDir "validate-task.ps1") -TaskFile $TaskFile -RepoRoot $RepoRoot

$t = Get-Content $TaskFile -Raw -Encoding UTF8 | ConvertFrom-Json
if ($DryRun) {
  foreach ($cmd in @($t.verify)) { Write-Output "[dryrun] $cmd" }
  Write-Output "verify-task: DRYRUN PASS $($t.taskId)"
  return
}
# 散文条目显式化（F-513②——UI-T62 治理清扫批次）：verify 数组允许散文式
# 验收条目（非命令文本）。Invoke-Expression 对散文必然报错——此前无
# catch、非终结错误不打断循环、LASTEXITCODE 沿用上一命令 → 散文条目被
# 静默"PASS"。本版逐条捕获：机器可执行＝真实执行并核对退出码；执行失败
# （非命令/术语不识别）＝显式 [manual] 人工核对口径并计数——汇总行分列
# 两种条目数，"PASS" 仅代表机器可执行部分全部通过（散文部分不产生虚假
# 通过语义；人工核对结论归验收记录）。
$manualEntries = 0
$machineEntries = 0
foreach ($cmd in @($t.verify)) {
  Write-Output "> $cmd"
  Push-Location $RepoRoot
  try {
    Invoke-Expression $cmd -ErrorAction Stop
    if ($LASTEXITCODE -and $LASTEXITCODE -ne 0) { throw "verification failed: $cmd" }
    $machineEntries++
  } catch {
    # 散文条目判定＝Parse/CommandNotFound 类错误（非命令文本必然抛出）；
    # 其余异常＝真实验证失败，原样重抛（不吞错——验证失败轨保持致命）。
    if ($_.Exception -is [System.Management.Automation.CommandNotFoundException] -or
        $_.Exception -is [System.Management.Automation.ParseException]) {
      Write-Output "[manual] 条目非机器可执行——人工核对口径（结论归验收记录）: $cmd"
      $manualEntries++
    } else {
      throw
    }
  }
  finally { Pop-Location }
}
Write-Output ("verify-task: PASS {0}（机器可执行 {1} 条全部通过；散文人工核对 {2} 条）" -f $t.taskId, $machineEntries, $manualEntries)
