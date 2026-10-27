# REUSE-T36 现场核查：逐字复刻 reuse-contract.ps1 CheckSourcePermission 的
# 类别/操作提取逻辑（不含授权判定），对 policy 改动面三个文件实测提取结果。
# 仅读文件、输出提取值；不改任何门禁行为。
$ErrorActionPreference = 'Stop'
$RepoRoot = 'D:/10_Source_Repos/21_robot/RobWork'
function Extract([string]$rel, [switch]$IsCMake) {
  $text = [IO.File]::ReadAllText((Join-Path $RepoRoot $rel))
  if ($IsCMake) { $text = [regex]::Replace($text, '(?m)#[^\r\n]*', ' ') }
  $code = [regex]::Replace($text, '(?s)/\*.*?\*/|(?m)//[^\r\n]*', ' ')
  $categories = @()
  foreach ($pair in @(@('model','rw/models/'),@('state','rw/kinematics/'),@('solver','rw/(invkin|trajectory|pathplanning)/'),@('collision','rw/proximity/'),@('simulation','rwsim/'),@('loader','rw/loaders/'),@('graphics','(rw/graphics|rwlibs/opengl)/'),@('host','rws/'))) {
    if ($code -cmatch ('(?m)^\s*#\s*include\s*[<"]' + $pair[1])) { $categories += $pair[0] }
  }
  $code = [regex]::Replace($code, '"(?:\\.|[^"\\])*"|''(?:\\.|[^''\\])*''', ' ')
  if ($code -cmatch '\brw::models::|\b(WorkCell|DynamicWorkCell|RigidDevice)\b') { $categories += 'model' }
  if ($code -cmatch '\brw::kinematics::|\bState\b') { $categories += 'state' }
  if ($code -cmatch '\brw::invkin::|\bIKSolver\b') { $categories += 'solver' }
  if ($code -cmatch '\b(rwsim::|PhysicsEngine)') { $categories += 'simulation' }
  $ops = @()
  if ($code -cmatch '\b(new\s+(?:rw::models::|rwsim::dynamics::)?(?:WorkCell|DynamicWorkCell|SerialDevice|RigidDevice)|(?:make_shared|ownedPtr)\s*<\s*(?:rw::models::|rwsim::dynamics::)?(?:WorkCell|DynamicWorkCell|SerialDevice|RigidDevice))\b|\b(?:create|make|build)(?:WorkCell|DynamicWorkCell|SerialDevice|RigidDevice)\s*\(') { $ops += 'construct-model' }
  if ($code -cmatch '\b(?:WorkCellLoader|DynamicWorkCellLoader)\s*::\s*load') { $ops += 'load-model' }
  if ($code -cmatch '(?:->|\.)\s*(?:addFrame|addDevice|addBody|removeFrame|removeDevice|setMass|setInertia)\s*\(') { $ops += 'mutate-model' }
  if ($code -cmatch '\b(?:cloneWorkCell|cloneDynamicWorkCell)\s*\(') { $ops += 'clone-model' }
  if ($code -cmatch '(?:->|\.)\s*setQ\s*\(') { $ops += 'write-state' }
  if (@($categories | Where-Object { $_ -in @('model','state') }).Count -and $code -cmatch '(?:->|\.)\s*setTransform\s*\(') { $ops += 'write-state' }
  if ($code -cmatch '\b(?:rw::kinematics::)?StateData::setData\s*\(') { $ops += 'write-state' }
  foreach ($declaration in [regex]::Matches($code, '\b(?:rw::kinematics::)?StateData\s*[*&]?\s*(\w+)')) {
    if ($code -cmatch ('\b' + [regex]::Escape($declaration.Groups[1].Value) + '\s*(?:->|\.)\s*setData\s*\(')) { $ops += 'write-state' }
  }
  if ($code -cmatch '\b(?:new\s+(?:rw::kinematics::)?State|(?:make_shared|ownedPtr)\s*<\s*(?:rw::kinematics::)?State)\b') { $ops += 'construct-session' }
  $categories = @($categories | Sort-Object -Unique)
  $ops = @($ops | Sort-Object -Unique)
  [pscustomobject]@{ file = $rel.Split('/')[-1]; cats = ($categories -join ','); ops = ($ops -join ','); restricted = ($categories.Count -gt 0 -or $ops.Count -gt 0) }
}
Extract 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/policy/src/CollisionEvaluator.cpp'
Extract 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/policy/src/RobWorkCollisionEvaluator.cpp'
Extract 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/policy/test/CollisionSessionTest.cpp'
