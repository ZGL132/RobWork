# REUSE-T52 catalog DYN-RNE 升级（candidate→verified，production 实况）＋
# permissions.json 产品授权追加（task-private 一次性计算载体）。精准文本锚
# 编辑——不整文件重排；Digest 权威＝门禁本体。
param([string]$RepoRoot = "D:/10_Source_Repos/21_robot/RobWork")
$ErrorActionPreference = 'Stop'
$design = 'RobWork/doc/industrial-robot-design/'
function Edit-File([string]$rel, [scriptblock]$fn) {
    $p = Join-Path $RepoRoot $rel
    $t = [IO.File]::ReadAllText($p)
    $outputs = @(& $fn $t)
    $t = $outputs[-1]  # 末项＝返回的修改文本（Write-Output 诊断行不入文件）
    [IO.File]::WriteAllText($p, $t, (New-Object System.Text.UTF8Encoding($false)))
    Write-Output "edited: $rel"
}

# ---- catalog.json：DYN-RNE 块三处 ----
function ToCrlf([string]$s) { $s.Replace("`r`n", "`n").Replace("`n", "`r`n") }
Edit-File ($design + 'reuse/catalog.json') {
    param($t)
    # 1) search.conclusion（DYN-RNE 块唯一锚——两行组合在全文唯一）
    $old = ToCrlf @'
        "conclusion": "已读公共接口；适配等价性仍须REUSE-T01验证"
      },
      "boundary": "SerialDevice、Body坐标、DOF+1、移动关节力投影",
'@
    $new = ToCrlf @'
        "conclusion": "REUSE-T52 定向实核（migration §13.3 WP-07）：RecursiveNewtonEuler.hpp/.cpp 公共 API 逐行实读（solve DOF+1 Wrench、setGravity 显式传递、getJointForces payload 力矩臂符号与手算物理相反〔次要发现②〕、solveMotorTorques 移动关节同投影 torque 缺陷）；生产消费与四段证据见 REUSE-T52"
      },
      "boundary": "SerialDevice、Body坐标、DOF+1、移动关节力投影",
'@
    if ($t.Contains($old)) { $t = $t.Replace($old, $new) }
    elseif (-not $t.Contains('生产消费与四段证据见 REUSE-T52')) { throw 'catalog anchor-1 not found' }
    # 2) upstream symbols 增补 RecursiveNewtonEuler（类名——buildRneConsumptionChain 消费 ctor/validate）
    $old2 = ToCrlf @'
          "symbols": [
            "solve",
            "setGravity",
            "setPayload"
          ],
'@
    $new2 = @'
          "symbols": [
            "RecursiveNewtonEuler",
            "solve",
            "setGravity",
            "setPayload"
          ],
'@
    if ($t.Contains($old2)) { $t = $t.Replace($old2, $new2) }
    elseif (-not $t.Contains('"RecursiveNewtonEuler",')) { throw 'catalog anchor-2 not found' }
    # 3) DYN-RNE usages/operations/review 升级（块内唯一锚——其后紧跟 DYN-FD）
    $old3 = ToCrlf @'
      "usages": [
        {
          "consumerUnit": "dynamics",
          "usageRole": "adapter",
          "permissionRef": "ARCH:3.6",
          "operations": [
            "query"
          ]
        }
      ],
      "limits": {
        "semantics": "SerialDevice、Body坐标、DOF+1、移动关节力投影",
        "lifetime": "按ARCH 3.6区分owner/受控视图/任务私有会话",
        "determinism": "按需求与功能探测，未验证不得宣称确定性"
      },
      "ownerUnit": "dynamics",
      "examples": [],
      "docs": [],
      "operations": [
        "query"
      ],
      "review": {
        "status": "candidate",
        "version": "b12e8503cc1c2a1acc040f506842a02e126b9515"
      },
      "tests": [],
      "resourceSearch": {
        "command": "rg -n \"solve|setGravity|setPayload\" RobWork/RobWork/src RobWork/RobWorkSim/src RobWork/RobWork/gtest RobWork/RobWorkSim/gtest",
        "conclusion": "已核对所列资源；REUSE-T01 首批公共API探测见 traceability/reuse/REUSE-T01；candidate保持未结项，未运行原上游完整测试集"
      }
'@
    $new3 = @'
      "usages": [
        {
          "consumerUnit": "dynamics",
          "usageRole": "adapter",
          "permissionRef": "ARCH:3.6",
          "operations": [
            "construct",
            "query"
          ]
        }
      ],
      "limits": {
        "semantics": "SerialDevice、Body坐标、DOF+1、移动关节力投影；GAP-RNE-FRAME 消费边界薄适配（A2 裁决）——BodyInfo 按关节帧解释，产品侧换算 R_alignᵀ·com／R_alignᵀ·I·R_align；setPayload 工具条目力矩臂符号与手算物理相反，产品不消费（合成体并入法兰 BodyInfo）；getGravity ctor 默认零，须 setGravity 显式传递",
        "lifetime": "按ARCH 3.6区分owner/受控视图/任务私有会话；产品消费＝evaluate 内一次性 RNE 面设备（task-private，随调用栈消亡，零产品面名称/真值出口）",
        "determinism": "REUSE-T52 实测：同输入逐位确定（上游纯 double 递推＋ReproBenchmark 确定性套件全绿）；无随机源/时钟/locale 依赖"
      },
      "ownerUnit": "dynamics",
      "examples": [],
      "docs": [],
      "operations": [
        "query",
        "construct"
      ],
      "review": {
        "status": "verified",
        "version": "bf682893ff174d759a93e905889458d2f4e80e21",
        "evidence": {
          "path": "RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T52/suitability.md",
          "sha256": "SUITABILITY-DIGEST-PENDING"
        }
      },
      "tests": [],
      "resourceSearch": {
        "command": "rg -n \"solve|setGravity|setPayload\" RobWork/RobWork/src RobWork/RobWorkSim/src RobWork/RobWork/gtest RobWork/RobWorkSim/gtest",
        "conclusion": "上游无 RNE 单元测试（REUSE-T30 §2 在案）；产品 oracle 与上游对照证据＝traceability/reuse/REUSE-T52/（full-suite 119＋oracle＋upstream-comparison＋disconnected 四段）"
      }
'@
    if ($t.Contains($old3)) { $t = $t.Replace($old3, $new3) }
    elseif (-not $t.Contains('"status": "verified"')) { throw 'catalog anchor-3 not found' }
    $t
}

# suitability.md 摘要回填（两遍法：先写文档再算摘要）
Edit-File ($design + 'reuse/catalog.json') {
    param($t)
    $sp = Join-Path $RepoRoot ($design + 'traceability/reuse/REUSE-T52/suitability.md')
    if (-not (Test-Path $sp)) { return $t }  # suitability 尚未写——由写稿批再回填
    $bytes = [Text.Encoding]::UTF8.GetBytes((((([IO.File]::ReadAllText($sp)).TrimStart([char]0xFEFF)) -replace "`r`n", "`n").TrimEnd("`n")) + "`n")
    $sha = [Security.Cryptography.SHA256]::Create()
    $d = ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant(); $sha.Dispose()
    $t = $t.Replace('SUITABILITY-DIGEST-PENDING', $d)
    Write-Output "suitability digest=$d"
    $t
}

# ---- permissions.json：追加产品授权（插在 grants 数组末项之后）----
Edit-File ($design + 'reuse/permissions.json') {
    param($t)
    if ($t.Contains('REUSE-T52-InverseDynamics.cpp')) { throw 'grant already present' }
    $cmlDigest = 'PENDING-CML'
    $cmlPath = Join-Path $RepoRoot 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/dynamics/CMakeLists.txt'
    $bytes = [Text.Encoding]::UTF8.GetBytes((((([IO.File]::ReadAllText($cmlPath)).TrimStart([char]0xFEFF)) -replace "`r`n", "`n").TrimEnd("`n")) + "`n")
    $sha = [Security.Cryptography.SHA256]::Create()
    $cmlDigest = ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant(); $sha.Dispose()
    $grant = ToCrlf @'
    {
      "id": "REUSE-T52-InverseDynamics.cpp",
      "source": {
        "path": "RobWork/RobWorkStudio/src/rwslibs/industrialrobot/dynamics/src/InverseDynamics.cpp",
        "sha256": "CPP-DIGEST-PENDING"
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
        "construct-model",
        "mutate-model",
        "write-state"
      ],
      "origin": "task-private",
      "lifetime": "ARCH 3.6 任务私有会话：evaluate 内一次性 RNE 面计算载体（StateStructure/SerialDevice/RigidDevice/State 全部本地构造、随调用栈消亡；addFrame/setQ 仅作用于该私有载体）——零共享快照触碰、零产品面名称/真值出口、无跨单元/跨进程所有权扩展（REUSE-T52 契约 acceptance 1 CM-0 登记语义）。",
      "permissionRef": "ARCH:3.6",
      "usageRole": "adapter",
      "build": {
        "path": "RobWork/RobWorkStudio/src/rwslibs/industrialrobot/dynamics/CMakeLists.txt",
        "sha256": "CML-DIGEST-PENDING"
      }
    },
'@
    $grant = $grant.Replace('CML-DIGEST-PENDING', $cmlDigest)
    $cppPath = Join-Path $RepoRoot 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/dynamics/src/InverseDynamics.cpp'
    $bytes2 = [Text.Encoding]::UTF8.GetBytes((((([IO.File]::ReadAllText($cppPath)).TrimStart([char]0xFEFF)) -replace "`r`n", "`n").TrimEnd("`n")) + "`n")
    $sha2 = [Security.Cryptography.SHA256]::Create()
    $cppDigest = ([BitConverter]::ToString($sha2.ComputeHash($bytes2))).Replace('-', '').ToLowerInvariant(); $sha2.Dispose()
    $grant = $grant.Replace('CPP-DIGEST-PENDING', $cppDigest)
    # 插入点：grants 数组最后一个条目的闭合（"    }\n  ]" 形态——全文唯一收尾）
    $idx = $t.LastIndexOf("    }`r`n  ]")
    if ($idx -lt 0) { $idx = $t.LastIndexOf("    }`n  ]") }
    if ($idx -lt 0) { throw 'permissions grants tail not found' }
    $insertAt = $idx + "    }".Length
    $t = $t.Insert($insertAt, ",`r`n" + $grant.TrimEnd())
    Write-Output "grant appended (cpp=$cppDigest cml=$cmlDigest)"
    $t
}
