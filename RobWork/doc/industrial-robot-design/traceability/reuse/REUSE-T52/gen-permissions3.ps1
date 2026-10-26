# REUSE-T52 授权更新 v3（从 HEAD 基线一次性成型——两笔授权按 grants 数组
# 结构化追加，避免文本锚歧义；Digest 权威＝门禁本体）。
param([string]$RepoRoot = "D:/10_Source_Repos/21_robot/RobWork")
$ErrorActionPreference = 'Stop'
function Digest([string]$text) {
    $bytes = [Text.Encoding]::UTF8.GetBytes(($text.TrimStart([char]0xFEFF) -replace "`r`n", "`n").TrimEnd("`n") + "`n")
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant() } finally { $sha.Dispose() }
}
function FileDigest([string]$rel) { Digest ([IO.File]::ReadAllText((Join-Path $RepoRoot $rel))) }
function ToCrlf([string]$s) { $s.Replace("`r`n", "`n").Replace("`n", "`r`n") }

$dCpp = FileDigest 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/dynamics/src/InverseDynamics.cpp'
$dCml = FileDigest 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/dynamics/CMakeLists.txt'
$rCpp = FileDigest 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/runtime/src/RneConsumptionDevice.cpp'
$rCml = FileDigest 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/runtime/CMakeLists.txt'

$grantDynamics = ToCrlf @"
    {
      "id": "REUSE-T52-InverseDynamics.cpp",
      "source": {
        "path": "RobWork/RobWorkStudio/src/rwslibs/industrialrobot/dynamics/src/InverseDynamics.cpp",
        "sha256": "$dCpp"
      },
      "targets": [
        "sdurws_ird_dynamics"
      ],
      "categories": [
        "model",
        "state",
        "simulation"
      ],
      "operations": [
        "query",
        "write-state"
      ],
      "origin": "task-private",
      "lifetime": "ARCH 3.6 任务私有会话：evaluate 内一次性 RNE 载体消费（载体构造归 runtime 工厂 buildRneConsumptionDevice——2026-10-26 所有者构造面上收裁决；dynamics 侧仅 setQ 于该私有 State 与末连杆 BodyInfo 变体回写，随调用栈消亡）——零共享快照触碰、零产品面名称/真值出口、无跨单元/跨进程所有权扩展（REUSE-T52 契约登记语义）。",
      "permissionRef": "ARCH:3.6",
      "usageRole": "adapter",
      "build": {
        "path": "RobWork/RobWorkStudio/src/rwslibs/industrialrobot/dynamics/CMakeLists.txt",
        "sha256": "$dCml"
      }
    }
"@

$grantRuntime = ToCrlf @"
    {
      "id": "REUSE-T52-RneConsumptionDevice.cpp",
      "source": {
        "path": "RobWork/RobWorkStudio/src/rwslibs/industrialrobot/runtime/src/RneConsumptionDevice.cpp",
        "sha256": "$rCpp"
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
        "query",
        "construct-model",
        "mutate-model"
      ],
      "origin": "runtime-owner",
      "lifetime": "ARCH 3.6.1 既有 runtime 模型 owner：RNE 可消费一次性载体工厂（2026-10-26 所有者构造面上收裁决）——工厂调用期独占构造 StateStructure/SerialDevice/RigidDevice 与 addFrame 关节帧树，载体所有权随返回值移交调用方（task-private 消费）；GAP-RNE-FRAME 关节帧换算在本工厂单点（§8.2/S7 产品映射/modeling 导出零触碰）；无跨单元/跨进程所有权扩展。",
      "permissionRef": "ARCH:3.6.1",
      "usageRole": "owner",
      "build": {
        "path": "RobWork/RobWorkStudio/src/rwslibs/industrialrobot/runtime/CMakeLists.txt",
        "sha256": "$rCml"
      }
    }
"@

$p = Join-Path $RepoRoot 'RobWork/doc/industrial-robot-design/reuse/permissions.json'
$t = [IO.File]::ReadAllText($p)
if ($t.Contains('REUSE-T52-InverseDynamics.cpp') -or $t.Contains('REUSE-T52-RneConsumptionDevice.cpp')) {
    throw 'grants already present — start from HEAD baseline'
}
# grants 数组闭合锚＝文件尾形态："    }\r\n  ]\r\n}"（grants 是末成员）
$tail = $t.LastIndexOf("    }`r`n  ]`r`n}")
if ($tail -lt 0) { throw ('grants tail not found — tail bytes: ' + $t.Substring($t.Length - 40).Replace("`r", '\r').Replace("`n", '\n')) }
$insertAt = $tail + "    }".Length
$t = $t.Insert($insertAt, ",`r`n" + $grantDynamics + ",`r`n" + $grantRuntime)
[IO.File]::WriteAllText($p, $t, (New-Object System.Text.UTF8Encoding($false)))
Write-Output "grants appended (dynamics + runtime factory)"
