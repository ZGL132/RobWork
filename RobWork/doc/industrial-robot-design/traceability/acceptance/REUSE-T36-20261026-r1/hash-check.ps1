# REUSE-T36 独立验收辅助（10-hash-check）：按 validate-reuse.ps1 Digest 同一口径
# 复算治理登记 sha256（BOM 剥除/CRLF→LF/尾单换行规范化），不另起可绕过入口。
[Console]::OutputEncoding = [Text.Encoding]::UTF8
$RepoRoot = 'D:\10_Source_Repos\21_robot\RobWork'
function Digest([string]$text) {
    $bytes = [Text.Encoding]::UTF8.GetBytes(($text.TrimStart([char]0xFEFF) -replace "`r`n", "`n").TrimEnd("`n") + "`n")
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant() } finally { $sha.Dispose() }
}
function Get-AccDigest([string]$rel) { Digest ([IO.File]::ReadAllText((Join-Path $RepoRoot $rel))) }

$claims = [ordered]@{
    'policy/src/CollisionEvaluator.cpp'        = '8ddfbb5d2a3cc9c15820feee2e98c8298eaa2a7439f325ce588988a2912e5dd8'
    'policy/test/CollisionSessionTest.cpp'     = '589599905ea17ccbe5d3e2865de523ffc8152c6925792a0fc5b91f6d6b422600'
    'production-buildPolicyProximitySetup.json' = '77cd10b0c47b7d757193fe63e7227039c92b26e29bf86919ac961dccfac9108e'
    'production-RobWorkCollisionEvaluator.json' = 'f28383d51c0d3c5cc4f271ec665700a582711d2f659acd591149dc5c605d06f7'
}
$files = [ordered]@{
    'policy/src/CollisionEvaluator.cpp'         = 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/policy/src/CollisionEvaluator.cpp'
    'policy/test/CollisionSessionTest.cpp'      = 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/policy/test/CollisionSessionTest.cpp'
    'production-buildPolicyProximitySetup.json' = 'RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T36/production-buildPolicyProximitySetup.json'
    'production-RobWorkCollisionEvaluator.json' = 'RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T36/production-RobWorkCollisionEvaluator.json'
    'runtime/src/NameMap.cpp (inputs)'          = 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/runtime/src/NameMap.cpp'
    'policy/CMakeLists.txt (inputs)'            = 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/policy/CMakeLists.txt'
    'include CollisionEvaluator.hpp (inputs)'   = 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/policy/include/sdurws/ird/policy/CollisionEvaluator.hpp'
}
foreach ($k in $files.Keys) {
    $actual = Get-AccDigest $files[$k]
    $ok = ''
    if ($claims.Contains($k)) { $ok = if ($actual -eq $claims[$k]) { '  MATCH-claimed' } else { '  *** MISMATCH claimed=' + $claims[$k] } }
    Write-Output ("{0} => {1}{2}" -f $k, $actual, $ok)
}

# 级联项：REUSE-T33/production-rpy.json 绑定 final.log sha256=f38b31ff…（acceptance 第4项）
Write-Output '--- REUSE-T33 cross-batch hygiene ---'
Write-Output ("REUSE-T33/final.log Digest = " + (Get-AccDigest 'RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T33/final.log'))

# catalog review evidence
Write-Output '--- catalog COLLISION review evidence ---'
Write-Output ("collision-capability-review.json Digest = " + (Get-AccDigest 'RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T36/collision-capability-review.json'))
