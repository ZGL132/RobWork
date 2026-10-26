# REUSE-T52 授权双面更新（工厂上收裁决执行面）：
# ①dynamics 授权收窄为 query/write-state（构造/变异已上收 runtime）；
# ②新增 runtime 工厂授权（runtime-owner——模型构造权本位，T30 形态）。
param([string]$RepoRoot = "D:/10_Source_Repos/21_robot/RobWork")
$ErrorActionPreference = 'Stop'
function Digest([string]$text) {
    $bytes = [Text.Encoding]::UTF8.GetBytes(($text.TrimStart([char]0xFEFF) -replace "`r`n", "`n").TrimEnd("`n") + "`n")
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant() } finally { $sha.Dispose() }
}
function FileDigest([string]$rel) { Digest ([IO.File]::ReadAllText((Join-Path $RepoRoot $rel))) }

$dCpp = FileDigest 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/dynamics/src/InverseDynamics.cpp'
$dCml = FileDigest 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/dynamics/CMakeLists.txt'
$rCpp = FileDigest 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/runtime/src/RneConsumptionDevice.cpp'
$rCml = FileDigest 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/runtime/CMakeLists.txt'

$p = Join-Path $RepoRoot 'RobWork/doc/industrial-robot-design/reuse/permissions.json'
$t = [IO.File]::ReadAllText($p)

# ①dynamics 授权：operations 收窄 + lifetime 文本更新（工厂裁决）
$oldD = @'
      "operations": [
        "query",
        "construct-model",
        "mutate-model",
        "write-state"
      ],
      "origin": "task-private",
      "lifetime": "ARCH 3.6 任务私有会话：evaluate 内一次性 RNE 面计算载体（StateStructure/SerialDevice/RigidDevice/State 全部本地构造、随调用栈消亡；addFrame/setQ 仅作用于该私有载体）——零共享快照触碰、零产品面名称/真值出口、无跨单元/跨进程所有权扩展（REUSE-T52 契约 acceptance 1 CM-0 登记语义）。",
'@
$newD = @'
      "operations": [
        "query",
        "write-state"
      ],
      "origin": "task-private",
      "lifetime": "ARCH 3.6 任务私有会话：evaluate 内一次性 RNE 载体消费（载体构造归 runtime 工厂 buildRneConsumptionDevice——2026-10-26 所有者构造面上收裁决；dynamics 侧仅 setQ 于该私有 State 与末连杆 BodyInfo 变体回写，随调用栈消亡）——零共享快照触碰、零产品面名称/真值出口、无跨单元/跨进程所有权扩展（REUSE-T52 契约登记语义）。",
'@
$oldD = $oldD.Replace("`r`n", "`n").Replace("`n", "`r`n")
$newD = $newD.Replace("`r`n", "`n").Replace("`n", "`r`n")
if ($t.Contains($oldD)) { $t = $t.Replace($oldD, $newD) }
elseif (-not $t.Contains('"query","write-state"')) { throw 'dynamics grant anchor not found' }
# dynamics 授权源摘要刷新（文件已改）
$t = $t.Replace('        "path": "RobWork/RobWorkStudio/src/rwslibs/industrialrobot/dynamics/src/InverseDynamics.cpp",      "sha256"', 'KEEP')  # 无操作防误
$i = $t.IndexOf('REUSE-T52-InverseDynamics.cpp')
if ($i -lt 0) { throw 'dynamics grant missing' }
$segStart = $t.IndexOf('"sha256": "', $i)
$segStart = $t.IndexOf('"', $segStart + 10) + 1
$segEnd = $t.IndexOf('"', $segStart)
$t = $t.Substring(0, $segStart) + $dCpp + $t.Substring($segEnd)
# dynamics build 绑定刷新（其后的第一个 CMakeLists sha256）
$j = $t.IndexOf('dynamics/CMakeLists.txt', $i)
$k = $t.IndexOf('"sha256": "', $j)
$k = $t.IndexOf('"', $k + 10) + 1
$kEnd = $t.IndexOf('"', $k)
$t = $t.Substring(0, $k) + $dCml + $t.Substring($kEnd)

# ②runtime 工厂授权（追加至 grants 尾部）
if (-not $t.Contains('REUSE-T52-RneConsumptionDevice.cpp')) {
    $grant = @'
    {
      "id": "REUSE-T52-RneConsumptionDevice.cpp",
      "source": {
        "path": "RobWork/RobWorkStudio/src/rwslibs/industrialrobot/runtime/src/RneConsumptionDevice.cpp",
        "sha256": "RCPP"
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
        "sha256": "RCML"
      }
    }
'@
    $grant = $grant.Replace("`r`n", "`n").Replace("`n", "`r`n").Replace('RCPP', $rCpp).Replace('RCML', $rCml)
    $idx = $t.LastIndexOf("    }`r`n  ]")
    if ($idx -lt 0) { $idx = $t.LastIndexOf("    }`n  ]") }
    if ($idx -lt 0) { throw 'grants tail not found' }
    $insertAt = $idx + "    }".Length
    $t = $t.Insert($insertAt, ",`r`n" + $grant.TrimEnd())
}

[IO.File]::WriteAllText($p, $t, (New-Object System.Text.UTF8Encoding($false)))
Write-Output "permissions updated (dCpp=$dCpp dCml=$dCml rCpp=$rCpp rCml=$rCml)"
