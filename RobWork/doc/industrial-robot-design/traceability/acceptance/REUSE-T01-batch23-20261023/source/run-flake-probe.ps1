# 偶发失败探针（run-flake-probe.ps1，2026-10-18 F-925 治理批）。
#
# 职责：把"负载下偶发失败"从不可复现的口头现象变成可留痕事件——对指定测试
# 可执行文件重复运行 N 次，逐次落盘 stdout/stderr 与 gtest XML；可选在运行
# 期间制造后台 CPU 负载（模拟并行编译/验收环境）。任一次失败即：
#   ① 在汇总里点名失败用例（XML 的 testcase[failure/error] 节点，不靠翻日志）；
#   ② 保留该次完整输出与 XML 路径；
#   ③ 退出码 1（便于 CI/批处理判红）。
#
# 背景（F-925）：UI-T85 验收 attempt 1 中验收者 6 跑 1 败（3 用例失败），失败
# 输出被并行构建日志淹没，失败用例名未捕获；实施者主树复跑未复现。根因未定
# （环境负载型 vs 真实竞态），但"失败无法被记录"本身是可修的观测缺陷——本
# 脚本保证任何一次失败都带用例名与原始输出落盘，后续复现即可定性。
#
# 用法：
#   pwsh -File run-flake-probe.ps1 -TestExe build/RobWorkStudio/bin/Release/sdurws_ird_ui_test.exe `
#        [-Repeat 10] [-LoadJobs 4] [-OutDir traceability/builds/flake-probe/<名字>] [-GtestFilter "*"]
#   pwsh -File run-flake-probe.ps1 -SelfTest      # 自检：失败点名与零执行拒绝
#
# 约定：
#   - 测试 exe 的运行期 DLL 前置（框架 bin / vcpkg bin / Qt bin / python）由调用方
#     PATH 提供，脚本不猜测环境（F-007/F-428 教训：环境口径不写死在脚本里）；
#   - OutDir 缺省＝<仓库根>/traceability/builds/flake-probe/<UTC 时间戳>，证据只增不改；
#   - 后台负载用独立 PowerShell 进程（Start-Job）制造，结束后强制清理，不残留。
#
# 追溯：findings F-925；AGENTS §4.2（验证留痕）；DTB §5.1（脚本注册）；
# rule-enforcement.md §2（观测工具，非门禁——失败才红，不因"跑得慢"红）。

param(
  [string]$TestExe = "",
  [int]$Repeat = 5,
  [int]$LoadJobs = 0,
  [string]$OutDir = "",
  [string]$GtestFilter = "*",
  [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'
$scriptDir = $PSScriptRoot
if (-not $scriptDir -or -not (Test-Path $scriptDir)) { $scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path }
$repoRoot = (Resolve-Path (Join-Path $scriptDir "..\..\..")).Path
if (-not (Test-Path (Join-Path $repoRoot "RobWork/doc/industrial-robot-design"))) {
  throw "repo root resolution failed under '$repoRoot'"
}

# ---------- 失败解析（自检与正式运行共用同一函数——防"工具本身没测过"） ----------
function Get-FailedTestNames([string]$XmlPath) {
  if (-not (Test-Path $XmlPath)) { return @('<xml-missing>') }
  try { [xml]$x = Get-Content -LiteralPath $XmlPath -Raw } catch { return @('<xml-unparsable>') }
  $names = @()
  # 零用例常由 filter 拼错造成；进程 exit0 不能证明测试已执行（AGENTS §4.2）。
  $cases = @($x.SelectNodes('//testcase'))
  if ($cases.Count -eq 0) { return @('<xml-no-tests>') }
  $executed = 0
  foreach ($tc in $cases) {
    # error 与 failure 同属失败证据；跳过项不计入实际执行数。
    if ($tc.SelectSingleNode('failure') -or $tc.SelectSingleNode('error')) {
      $suite = "$($tc.parentNode.GetAttribute('name'))"
      $names += ("{0}.{1}" -f $suite, $tc.name)
      $executed++
    } elseif (-not $tc.SelectSingleNode('skipped') -and $tc.GetAttribute('status') -ne 'notrun') {
      $executed++
    }
  }
  # 全跳过/禁用同样不能支持“负载下稳定”的结论，必须显式拒绝假绿。
  if ($executed -eq 0) { return @('<xml-no-executed-tests>') }
  return $names
}

# ---------- 自检：共用正式解析器，验证失败与零执行均可判红 ----------
if ($SelfTest) {
  $tmp = Join-Path ([IO.Path]::GetTempPath()) ("flake-selftest-" + [guid]::NewGuid().ToString('N') + ".xml")
  $cases = @(
    @{ Name='missing'; Xml=$null; Expected=@('<xml-missing>') },
    @{ Name='malformed'; Xml='<testsuites'; Expected=@('<xml-unparsable>') },
    @{ Name='zero'; Xml='<testsuites tests="0"/>'; Expected=@('<xml-no-tests>') },
    @{ Name='healthy'; Xml='<testsuites><testsuite name="S"><testcase name="Healthy"/></testsuite></testsuites>'; Expected=@() },
    @{ Name='failure'; Xml='<testsuites><testsuite name="S"><testcase name="Failed"><failure/></testcase></testsuite></testsuites>'; Expected=@('S.Failed') },
    @{ Name='error'; Xml='<testsuites><testsuite name="S"><testcase name="Errored"><error/></testcase></testsuite></testsuites>'; Expected=@('S.Errored') },
    @{ Name='all-skipped'; Xml='<testsuites><testsuite name="S"><testcase name="Skipped"><skipped/></testcase></testsuite></testsuites>'; Expected=@('<xml-no-executed-tests>') }
  )
  $passed = 0
  try {
    foreach ($case in $cases) {
      # 每例独立文件状态，missing 例不得误读前例遗留 XML。
      if (Test-Path -LiteralPath $tmp) { Remove-Item -LiteralPath $tmp -Force }
      if ($null -ne $case.Xml) { [IO.File]::WriteAllText($tmp, $case.Xml) }
      $names = @(Get-FailedTestNames $tmp)
      $ok = ($names.Count -eq $case.Expected.Count -and ($names -join '|') -ceq ($case.Expected -join '|'))
      if ($ok) { $passed++ }
      Write-Output ("  selftest {0}: {1} actual=[{2}]" -f $case.Name, $ok, ($names -join ', '))
    }
  } finally {
    if (Test-Path -LiteralPath $tmp) { Remove-Item -LiteralPath $tmp -Force }
  }
  $failed = $cases.Count - $passed
  if ($failed -eq 0) {
    Write-Output "run-flake-probe: SELFTEST PASS（executed=$($cases.Count) passed=$passed failed=0 skipped=0）"
    exit 0
  }
  Write-Output "run-flake-probe: SELFTEST FAIL（executed=$($cases.Count) passed=$passed failed=$failed skipped=0）"
  exit 1
}

# ---------- 参数校验 ----------
if (-not $TestExe) { throw "缺少 -TestExe（测试可执行文件路径）" }
if (-not (Test-Path $TestExe)) { throw "test executable not found: $TestExe" }
if ($Repeat -lt 1) { throw "-Repeat 必须 >= 1" }
if (-not $OutDir) { $OutDir = Join-Path $repoRoot ("traceability/builds/flake-probe/" + (Get-Date -Format 'yyyyMMdd-HHmmss')) }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$exeFull = (Resolve-Path $TestExe).Path
$exeName = [IO.Path]::GetFileNameWithoutExtension($exeFull)

Write-Output ("run-flake-probe: {0} repeat={1} loadJobs={2} out={3}" -f $exeName, $Repeat, $LoadJobs, $OutDir)

# ---------- 可选：后台 CPU 负载（模拟并行编译/验收负载） ----------
$loadProcesses = @()
try {
  for ($i = 0; $i -lt $LoadJobs; $i++) {
    # 独立进程自旋：Job 被 Stop 时其子进程一并结束（Start-Job 进程模型）；
    # 用 sqrt 累加而非空循环，避免编译器/JIT 把循环优化成空转。
    $loadProcesses += Start-Job -ScriptBlock {
      $x = 0.0
      while ($true) { $x += [math]::Sqrt([math]::Abs($x + 1.234567)) }
    }
  }
  if ($LoadJobs -gt 0) { Write-Output "run-flake-probe: 已启动 $LoadJobs 个后台负载进程" }

  # ---------- 重复运行 ----------
  $results = @()
  $anyFailure = $false
  for ($run = 1; $run -le $Repeat; $run++) {
    $xml = Join-Path $OutDir ("run-{0:D3}.xml" -f $run)
    $stdout = Join-Path $OutDir ("run-{0:D3}.stdout.log" -f $run)
    $stderr = Join-Path $OutDir ("run-{0:D3}.stderr.log" -f $run)
    $started = Get-Date
    # --gtest_output 用绝对路径（F-508 教训：相对路径随 cwd 漂移）
    $gtestArgs = @("--gtest_output=xml:$xml", "--gtest_filter=$GtestFilter")
    $p = Start-Process -FilePath $exeFull -ArgumentList $gtestArgs -NoNewWindow -PassThru `
      -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    $p.WaitForExit()
    $elapsed = [math]::Round(((Get-Date) - $started).TotalSeconds, 1)
    $failed = @(Get-FailedTestNames $xml)
    $exitCode = $p.ExitCode
    $ok = ($exitCode -eq 0 -and $failed.Count -eq 0)
    if (-not $ok) { $anyFailure = $true }
    $results += [pscustomobject]@{
      Run = $run; ExitCode = $exitCode; Seconds = $elapsed
      Failed = ($failed -join '; '); Xml = $xml
      Stdout = $stdout; Stderr = $stderr
    }
    $tag = if ($ok) { 'PASS' } else { 'FAIL' }
    Write-Output ("  run {0}/{1}: {2} exit={3} {4}s failed=[{5}]" -f $run, $Repeat, $tag, $exitCode, $elapsed, ($failed -join ', '))
  }
} finally {
  foreach ($j in $loadProcesses) {
    try { Stop-Job $j -ErrorAction SilentlyContinue; Remove-Job $j -Force -ErrorAction SilentlyContinue } catch { }
  }
}

# ---------- 汇总（机器可读 + 人读） ----------
$summaryPath = Join-Path $OutDir 'summary.txt'
$results | Format-Table -AutoSize | Out-String | Set-Content -LiteralPath $summaryPath -Encoding utf8
$failCount = @($results | Where-Object { $_.Failed -or $_.ExitCode -ne 0 }).Count
Add-Content -LiteralPath $summaryPath -Value ("TOTAL={0} FAILED_RUNS={1}" -f $Repeat, $failCount)
Write-Output "run-flake-probe: 汇总 $summaryPath（failed runs=$failCount/$Repeat）"
if ($anyFailure) {
  Write-Output "run-flake-probe: FAIL —— 失败用例已在 summary.txt 点名（原始 stdout/stderr/XML 均在 $OutDir）"
  exit 1
}
Write-Output "run-flake-probe: PASS —— $Repeat/$Repeat 全绿（未复现；本结果只证明本次环境稳定，不证明无竞态）"
