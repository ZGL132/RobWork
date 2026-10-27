# REUSE-T52 授权补充（扫描面补齐）：runtime 工厂公共头（state/model/simulation
# 类别——零所有权操作）＋RneaTest.cpp（/test/ 路径 test-fixture 例外——上游
# 直接对照用例的内联同构设备构造）。
param([string]$RepoRoot = "D:/10_Source_Repos/21_robot/RobWork")
$ErrorActionPreference = 'Stop'
function Digest([string]$text) {
    $bytes = [Text.Encoding]::UTF8.GetBytes(($text.TrimStart([char]0xFEFF) -replace "`r`n", "`n").TrimEnd("`n") + "`n")
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant() } finally { $sha.Dispose() }
}
function FileDigest([string]$rel) { Digest ([IO.File]::ReadAllText((Join-Path $RepoRoot $rel))) }
function ToCrlf([string]$s) { $s.Replace("`r`n", "`n").Replace("`n", "`r`n") }

$hHpp = FileDigest 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/runtime/include/sdurws/ird/runtime/RneConsumptionDevice.hpp'
$rCml = FileDigest 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/runtime/CMakeLists.txt'
$tCpp = FileDigest 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/dynamics/test/RneaTest.cpp'
$dCml = FileDigest 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/dynamics/CMakeLists.txt'

$grantHpp = ToCrlf @"
    {
      "id": "REUSE-T52-RneConsumptionDevice.hpp",
      "source": {
        "path": "RobWork/RobWorkStudio/src/rwslibs/industrialrobot/runtime/include/sdurws/ird/runtime/RneConsumptionDevice.hpp",
        "sha256": "$hHpp"
      },
      "targets": [
        "sdurws_ird_runtime"
      ],
      "categories": [
        "model",
        "state",
        "simulation"
      ],
      "operations": [
        "query"
      ],
      "origin": "runtime-owner",
      "lifetime": "ARCH 3.6.1 runtime 模型 owner：RNE 载体工厂公共契约头（载体值聚合的 rw 类型成员面——零构造/零写入操作，所有权语义见头注释；实现构造归同名 src 授权）。",
      "permissionRef": "ARCH:3.6.1",
      "usageRole": "owner",
      "build": {
        "path": "RobWork/RobWorkStudio/src/rwslibs/industrialrobot/runtime/CMakeLists.txt",
        "sha256": "$rCml"
      }
    }
"@

$grantTest = ToCrlf @"
    {
      "id": "REUSE-T52-RneaTest.cpp",
      "source": {
        "path": "RobWork/RobWorkStudio/src/rwslibs/industrialrobot/dynamics/test/RneaTest.cpp",
        "sha256": "$tCpp"
      },
      "targets": [
        "sdurws_ird_dynamics_test"
      ],
      "categories": [
        "model",
        "state",
        "simulation"
      ],
      "operations": [
        "query",
        "construct-model",
        "mutate-model"
      ],
      "origin": "test-fixture",
      "lifetime": "REUSE-T36 测试夹具例外（/test/ 路径）：上游直接对照用例（DynRnea.UpstreamDirectComparison_REUSE_T52）测试侧内联同构 RNE 载体构造（StateStructure/SerialDevice/RigidDevice/addFrame——对照位形取默认 State，零 State 写；构型相关面由产品路径用例与工厂断开实验承担）——任务输入装配而非模型所有权主张，产品侧构造仍归 runtime 工厂授权；仅集成模式测试目标，无生产权限扩散。",
      "permissionRef": "ARCH:3.6.1",
      "usageRole": "owner",
      "build": {
        "path": "RobWork/RobWorkStudio/src/rwslibs/industrialrobot/dynamics/CMakeLists.txt",
        "sha256": "$dCml"
      }
    }
"@

$p = Join-Path $RepoRoot 'RobWork/doc/industrial-robot-design/reuse/permissions.json'
$t = [IO.File]::ReadAllText($p)
if ($t.Contains('REUSE-T52-RneConsumptionDevice.hpp') -or $t.Contains('REUSE-T52-RneaTest.cpp')) {
    throw 'supplement grants already present'
}
$tail = $t.LastIndexOf("    }`r`n  ]`r`n}")
if ($tail -lt 0) { throw 'grants tail not found' }
$insertAt = $tail + "    }".Length
$t = $t.Insert($insertAt, ",`r`n" + $grantHpp + ",`r`n" + $grantTest)
[IO.File]::WriteAllText($p, $t, (New-Object System.Text.UTF8Encoding($false)))
Write-Output "supplement grants appended"
