# REUSE-T32 逐文件产品复用凭证生成（ird-reuse-change/1）。
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$ErrorActionPreference = 'Stop'
$RepoRoot = 'D:/10_Source_Repos/21_robot/RobWork'
function Get-Digest([string]$text) {
  $bytes = [Text.Encoding]::UTF8.GetBytes(($text.TrimStart([char]0xFEFF) -replace "`r`n", "`n").TrimEnd("`n") + "`n")
  $sha = [Security.Cryptography.SHA256]::Create()
  try { return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant() } finally { $sha.Dispose() }
}
function FB([string]$rel) {
  return [pscustomobject]@{ path = $rel; sha256 = (Get-Digest ([IO.File]::ReadAllText((Join-Path $RepoRoot $rel)))) }
}
$doc = [pscustomobject]@{
  schemaVersion = 'ird-reuse-change/1'
  taskId = 'REUSE-T32'
  taskFile = 'RobWork/doc/industrial-robot-design/tasks/REUSE-T32.json'
  files = @(
    [pscustomobject]@{
      path = 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/trajectory/src/Planner.cpp'
      sha256 = (FB 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/trajectory/src/Planner.cpp').sha256
      mode = 'adapter'
      capabilities = @('MATH-METRIC', 'PATH-PLANNING')
      boundary = '三处度量手写核删除并直接消费上游MetricFactory（Infinity/Manhattan/Euclidean）与PathAnalyzer::pathLength；稳定选择序四键全序/词表/身份/取消预算种子纪律不变；policy三态与runtime视图借持保持；整文件逐功能实审（5 adapter行production证据＋11 business-specific audit行），独验NOT RUN。'
      verify = 'RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T32/README.md'
    },
    [pscustomobject]@{
      path = 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/trajectory/CMakeLists.txt'
      sha256 = (FB 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/trajectory/CMakeLists.txt').sha256
      mode = 'governance'
      capabilities = @()
      boundary = '仅新增test/PlannerReuseTest.cpp注册（if(TARGET sdurw_kinematics)集成条件段，同既有形态）；ird_add_gtest宏与目标注册体零改动；冒烟模式不受影响（smoke-build.log）。'
      verify = 'RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T32/README.md'
    },
    [pscustomobject]@{
      path = 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/trajectory/test/PlannerReuseTest.cpp'
      sha256 = (FB 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/trajectory/test/PlannerReuseTest.cpp').sha256
      mode = 'business'
      capabilities = @()
      boundary = '新增测试：独立度量oracle/采样网格/选择序确定性/近pi反例（Quaternion半角精确构造）；端口替身只在六类边界，数值期望独立重写不调用产品匿名内部。'
      verify = 'RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T32/README.md'
    })
}
$out = Join-Path $RepoRoot 'RobWork/doc/industrial-robot-design/reuse/changes/REUSE-T32.json'
[IO.File]::WriteAllText($out, ($doc | ConvertTo-Json -Depth 40), [Text.UTF8Encoding]::new($false))
Write-Output 'wrote changes/REUSE-T32.json'
