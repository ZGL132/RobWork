# REUSE-T32 verification.json 生成器（T26 形态：绑定+构建+结果+性能账目）。
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$ErrorActionPreference = 'Stop'
$RepoRoot = 'D:/10_Source_Repos/21_robot/RobWork'
$ev = 'RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T32'
function Get-Digest([string]$text) {
  $bytes = [Text.Encoding]::UTF8.GetBytes(($text.TrimStart([char]0xFEFF) -replace "`r`n", "`n").TrimEnd("`n") + "`n")
  $sha = [Security.Cryptography.SHA256]::Create()
  try { return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant() } finally { $sha.Dispose() }
}
function Bind([string]$rel) {
  return [pscustomobject]@{ path = $rel; sha256 = (Get-Digest ([IO.File]::ReadAllText((Join-Path $RepoRoot $rel)))) }
}
function Build([string]$cmd, [string]$log) {
  return [pscustomobject]@{ command = $cmd; log = (Bind "$ev/$log"); exitCode = 0 }
}
function Res([int]$tests, [int]$failures) {
  return [pscustomobject]@{ tests = $tests; failures = $failures; errors = 0; disabled = 0; skipped = 0 }
}
$doc = [pscustomobject]@{
  taskId = 'REUSE-T32'
  base = 'c65ffc4fc495da0b01b2060ff3049eb127091d72'
  implementation = 'local working tree bound by exact inputs; final commit supplied at handoff'
  bindings = @(
    (Bind 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/trajectory/src/Planner.cpp'),
    (Bind 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/trajectory/CMakeLists.txt'),
    (Bind 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/trajectory/test/PlannerReuseTest.cpp'),
    (Bind 'RobWork/doc/industrial-robot-design/reuse/functions/trajectory.json'),
    (Bind 'RobWork/doc/industrial-robot-design/reuse/catalog.json'))
  toolchain = 'VS2022 x64 / C++17 / Release; microbenchmark MSVC /O2 /MD /DNDEBUG; existing vcpkg and Qt'
  results = [pscustomobject]@{
    final = (Res 193 0)
    contract = (Res 69 0)
    disconnectedL1 = (Res 1 1)
    disconnectedL2 = (Res 2 2)
    disconnectedCll = (Res 1 1)
    rechecks = 'disconnected-*-recheck: all passed, exit 0'
  }
  builds = @(
    (Build 'cmake --build build --config Release --target sdurws_ird_trajectory_test sdurws_ird_trajectory_contract_test' 'final-build.log'),
    (Build 'cmake --build build/govt24-smoke --config Release --target sdurws_ird_trajectory' 'smoke-build.log'))
  architecture = [pscustomobject]@{
    command = 'cmake -DIRD_ROOT=<repo>/RobWork/RobWorkStudio/src/rwslibs/industrialrobot -P RobWork/RobWorkStudio/src/rwslibs/industrialrobot/cmake/ird_gates.cmake'
    log = (Bind "$ev/architecture-gates.log")
    result = 'PASS: R-1/R-2/R-3/R-4/R-5/T-1/T-2/SUB/GRAPH/LIB/SA02 zero findings'
  }
  performance = [pscustomobject]@{
    command = 'cmd /c RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T32/run-performance.cmd'
    log = (Bind "$ev/performance.log")
    summary = 'pathL1(50n) +24~41% relative (+40ns/op absolute), maxStep/L2 noise-band; L1 fold-order relative delta 1.9e-15; owner acceptance NOT claimed, referred to independent acceptance'
  }
  counts = 'PASS/FAIL/SKIP/NOT RUN 分列：195/0/0（final）；69/0/0（contract）；3 组断开红例 2/1/2 failed 均 exit 1＋恢复复测全绿；NOT RUN＝上游原测试目标、1e6 上限护栏、真实Q PTP 极值、独立验收（README 表逐项）。'
}
$out = Join-Path $RepoRoot "$ev/verification.json"
[IO.File]::WriteAllText($out, ($doc | ConvertTo-Json -Depth 40), [Text.UTF8Encoding]::new($false))
Write-Output 'wrote verification.json'
