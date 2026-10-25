# REUSE-T36 权限登记＋COLLISION 能力升级（T32 add-grant/catalog-edit 同款机制）。
# ①permissions.json：CE（task-private/collision,model/query）＋RCE（同）＋
#   CollisionSessionTest（test-fixture/collision,model/state＋query,construct-model,
#   mutate-model——2026-10-26 所有者落地授权脉络批准的 §3.6.2.1 测试夹具例外）
# ②catalog COLLISION：review.status candidate→verified＋review.evidence 绑定
#   ＋ProximityStrategyRW.hpp 补类型构造符号（RCE ctor 行生产证据的 upstream）
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$ErrorActionPreference = 'Stop'
$RepoRoot = 'D:/10_Source_Repos/21_robot/RobWork'
$prod = 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/policy'
$ev = 'RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T36'
function Get-Digest([string]$text) {
  $bytes = [Text.Encoding]::UTF8.GetBytes(($text.TrimStart([char]0xFEFF) -replace "`r`n", "`n").TrimEnd("`n") + "`n")
  $sha = [Security.Cryptography.SHA256]::Create()
  try { return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant() } finally { $sha.Dispose() }
}
function FileBind([string]$rel) {
  return [pscustomobject]@{ path = $rel; sha256 = (Get-Digest ([IO.File]::ReadAllText((Join-Path $RepoRoot $rel)))) }
}
$cmakeBind = FileBind "$prod/CMakeLists.txt"

# ---- ①permissions.json ----
$permPath = Join-Path $RepoRoot 'RobWork/doc/industrial-robot-design/reuse/permissions.json'
$perm = Get-Content $permPath -Raw -Encoding UTF8 | ConvertFrom-Json
$hasTest = $false
foreach ($g in $perm.grants) { if ($g.id -eq 'policy-collision-test-fixture') { $hasTest = $true } }
if ($hasTest) { throw 'grants already present (idempotency guard)' }
$perm.grants += @(
  [pscustomobject]@{
    id = 'policy-collision-evaluator'; usageRole = 'adapter'
    source = (FileBind "$prod/src/CollisionEvaluator.cpp")
    targets = @('sdurws_ird_policy'); categories = @('collision','model'); operations = @('query')
    origin = 'task-private'; lifetime = 'policy 会话任务私有（构建期只读；会话构造后只读可重入）'
    permissionRef = 'ARCH:3.6'
    build = $cmakeBind
  },
  [pscustomobject]@{
    id = 'policy-collision-backend'; usageRole = 'adapter'
    source = (FileBind "$prod/src/RobWorkCollisionEvaluator.cpp")
    targets = @('sdurws_ird_policy'); categories = @('collision'); operations = @('query')
    origin = 'task-private'; lifetime = 'policy 会话任务私有（评估器持后端实例与查询互斥，会话共享）'
    permissionRef = 'ARCH:3.6'
    build = $cmakeBind
  },
  [pscustomobject]@{
    id = 'policy-collision-test-fixture'; usageRole = 'owner'
    source = (FileBind "$prod/test/CollisionSessionTest.cpp")
    targets = @('sdurws_ird_policy_test'); categories = @('collision','model','state')
    operations = @('query','construct-model','mutate-model')
    origin = 'test-fixture'
    lifetime = '测试夹具装配任务输入（gtest 会话内构造 WorkCell/FakeDevice 驱动解析链；ARCH §3.6.2.1——2026-10-26 所有者落地授权脉络批准，待追认）'
    permissionRef = 'ARCH:3.6'
    build = $cmakeBind
  }
)
[IO.File]::WriteAllText($permPath, ($perm | ConvertTo-Json -Depth 40), [Text.UTF8Encoding]::new($false))
Write-Output ("permissions saved: grants=" + $perm.grants.Count)

# ---- ②catalog COLLISION ----
$catPath = Join-Path $RepoRoot 'RobWork/doc/industrial-robot-design/reuse/catalog.json'
$cat = Get-Content $catPath -Raw -Encoding UTF8 | ConvertFrom-Json
$cap = $cat.capabilities | Where-Object { $_.id -eq 'COLLISION' }
if ("$($cap.review.status)" -ne 'candidate') { throw 'COLLISION not candidate (idempotency guard)' }
foreach ($u in $cap.upstream) {
  if ("$($u.path)" -like '*ProximityStrategyRW.hpp') {
    if ('ProximityStrategyRW' -notin @($u.symbols)) { $u.symbols = @('ProximityStrategyRW') + @($u.symbols) }
  }
}
$cap.review | Add-Member -NotePropertyName status -NotePropertyValue 'verified' -Force
$cap.review | Add-Member -NotePropertyName evidence -NotePropertyValue (FileBind "$ev/collision-capability-review.json") -Force
[IO.File]::WriteAllText($catPath, ($cat | ConvertTo-Json -Depth 40), [Text.UTF8Encoding]::new($false))
Write-Output 'catalog COLLISION: candidate -> verified (evidence bound)'
