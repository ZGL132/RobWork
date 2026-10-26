# REUSE-T52 矩阵/凭证生成器（治理 Digest 口径＝validate-reuse.ps1 同一规范化：
# BOM 剥离、CRLF→LF、尾部换行；绑定值权威仍＝门禁本体实测）。
# 产出：reuse/functions/dynamics.json（保留未触碰行＋REUSE-T52 行替换）
#      ＋reuse/changes/REUSE-T52.json。铺面校验：改动文件每非空行有区段归属。
param([string]$RepoRoot = "D:/10_Source_Repos/21_robot/RobWork")
$ErrorActionPreference = 'Stop'
$design = 'RobWork/doc/industrial-robot-design/'
$dynDir = "$RepoRoot/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/dynamics"
$cpp  = 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/dynamics/src/InverseDynamics.cpp'
$hpp  = 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/dynamics/include/sdurws/ird/dynamics/InverseDynamics.hpp'
$cml  = 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/dynamics/CMakeLists.txt'
$tst  = 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/dynamics/test/RneaTest.cpp'
$rhpp = 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/runtime/include/sdurws/ird/runtime/RneConsumptionDevice.hpp'
$rcpp = 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/runtime/src/RneConsumptionDevice.cpp'
$rcml = 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/runtime/CMakeLists.txt'
$upHpp = 'RobWork/RobWorkSim/src/rwsim/util/RecursiveNewtonEuler.hpp'
$upTgt = 'RobWork/RobWorkSim/src/rwsim/CMakeLists.txt'

function ReadTextNorm([string]$p) {
    $full = Join-Path $RepoRoot $p
    ([IO.File]::ReadAllText($full)).TrimStart([char]0xFEFF)
}
function Digest([string]$text) {
    $bytes = [Text.Encoding]::UTF8.GetBytes(($text.TrimStart([char]0xFEFF) -replace "`r`n", "`n").TrimEnd("`n") + "`n")
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant() } finally { $sha.Dispose() }
}
function FileDigest([string]$p) { Digest (ReadTextNorm $p) }
function RegionDigest([string]$p, [int]$s, [int]$e) {
    $lines = ((ReadTextNorm $p) -replace "`r`n", "`n") -split "`n"
    Digest (($lines[($s-1)..($e-1)] -join "`n"))
}
function LineCount([string]$p) { (((ReadTextNorm $p) -replace "`r`n", "`n") -split "`n").Count }

$cppLines = ((ReadTextNorm $cpp) -replace "`r`n", "`n") -split "`n"
$hppLines = ((ReadTextNorm $hpp) -replace "`r`n", "`n") -split "`n"
$cmlLines = ((ReadTextNorm $cml) -replace "`r`n", "`n") -split "`n"
$tstLines = ((ReadTextNorm $tst) -replace "`r`n", "`n") -split "`n"
$rhppLines = ((ReadTextNorm $rhpp) -replace "`r`n", "`n") -split "`n"
$rcppLines = ((ReadTextNorm $rcpp) -replace "`r`n", "`n") -split "`n"
$rcmlLines = ((ReadTextNorm $rcml) -replace "`r`n", "`n") -split "`n"

$digestCpp = FileDigest $cpp;  $digestHpp = FileDigest $hpp
$digestCml = FileDigest $cml;  $digestTst = FileDigest $tst
$digestRHpp = FileDigest $rhpp; $digestRCpp = FileDigest $rcpp; $digestRCml = FileDigest $rcml
$digestUpHpp = FileDigest $upHpp; $digestUpTgt = FileDigest $upTgt

# ---- 区段表：file/symbol/functionId/decision/status/start/end/capability ----
$segments = @(
    @{file=$cpp; sym='InverseDynamics.cpp'; id='dynamics.module.header'; dec='business-specific'; cap='NONE'; s=1;    e=113;  req=@('DYN-01'); role='owner'},
    @{file=$cpp; sym='JointFriction';       id='dynamics.dto.types';       dec='business-specific'; cap='NONE'; s=114;  e=153;  req=@('DYN-01','DYN-02'); role='owner'},
    @{file=$cpp; sym='assertPropertiesFiniteAndSymmetric'; id='dynamics.property.recheck'; dec='business-specific'; cap='NONE'; s=154; e=218; req=@('DYN-02','NFR-COR-03'); role='owner'},
    @{file=$cpp; sym='extractEndComponent'; id='dynamics.endComponent.extract'; dec='business-specific'; cap='NONE'; s=219;  e=300;  req=@('DYN-02','DYN-06'); role='owner'},
    @{file=$cpp; sym='RneConsumptionChain'; id='dynamics.rne.chain.types'; dec='business-specific'; cap='NONE'; s=301;  e=337;  req=@('DYN-01'); role='owner'},
    @{file=$cpp; sym='buildRneConsumptionChain'; id='dynamics.rne.device.build'; dec='adapter'; cap='DYN-RNE'; s=338; e=500; req=@('DYN-01'); role='adapter'; prod=@{entry='buildRneConsumptionChain'; upstream='RecursiveNewtonEuler'; consumer='buildRneConsumptionChain'; target='sdurws_ird_dynamics'}; ev='production-dyn-rne-device-build.json'},
    @{file=$cpp; sym='composeFlangeVariant'; id='dynamics.flange.compose'; dec='business-specific'; cap='NONE'; s=501;  e=583;  req=@('DYN-02'); role='owner'},
    @{file=$cpp; sym='frictionTerm';        id='dynamics.friction.term';   dec='business-specific'; cap='NONE'; s=584;  e=620;  req=@('DYN-02'); role='owner'},
    @{file=$cpp; sym='solveChannel';        id='dynamics.solve.channel';   dec='adapter'; cap='DYN-RNE'; s=621; e=647; req=@('DYN-01'); role='adapter'; prod=@{entry='solveChannel'; upstream='solve'; consumer='solveChannel'; target='sdurws_ird_dynamics'}; ev='production-dyn-rne-solve-channel.json'},
    @{file=$cpp; sym='InverseDynamicsEvaluator::evaluate'; id='dynamics.evaluate.entry'; dec='business-specific'; cap='NONE'; s=648; e=1064; req=@('DYN-01','DYN-02'); role='owner'},
    @{file=$hpp; sym='kRneaAlgorithmVersion'; id='dynamics.idyn.header.contract'; dec='business-specific'; cap='NONE'; s=1;   e=91;   req=@('DYN-01'); role='owner'},
    @{file=$hpp; sym='InverseDynRequest';   id='dynamics.idyn.request.dto'; dec='business-specific'; cap='NONE'; s=92;  e=182;  req=@('DYN-01','DYN-02'); role='owner'},
    @{file=$hpp; sym='InverseDynOutcome';   id='dynamics.idyn.outcome.dto'; dec='business-specific'; cap='NONE'; s=183; e=207;  req=@('DYN-01'); role='owner'},
    @{file=$hpp; sym='InverseDynamicsEvaluator'; id='dynamics.idyn.evaluator.class'; dec='business-specific'; cap='NONE'; s=208; e=273; req=@('DYN-01'); role='owner'},
    @{file=$cml; sym='sdurwsim';            id='dynamics.cmake.targets';   dec='business-specific'; cap='TEST-HARNESS'; s=1; e=789; req=@('DYN-01'); role='owner'},
    @{file=$tst; sym='DynRnea';             id='dynamics.rnea.test.suite'; dec='business-specific'; cap='TEST-HARNESS'; s=1; e=1289; req=@('DYN-01','DYN-02'); role='owner'},
    @{file=$rhpp; sym='RneConsumptionDevice'; id='runtime.rne.device.contract'; dec='business-specific'; cap='NONE'; s=1; e=101; req=@('DYN-01'); role='owner'},
    @{file=$rcpp; sym='buildRneConsumptionDevice'; id='runtime.rne.device.factory'; dec='adapter'; cap='DYN-RNE'; s=1; e=219; req=@('DYN-01'); role='owner'; prod=@{entry='buildRneConsumptionDevice'; upstream='RecursiveNewtonEuler'; consumer='buildRneConsumptionDevice'; target='sdurws_ird_runtime'}; ev='production-runtime-rne-device-factory.json'},
    @{file=$rcml; sym='sdurwsim';           id='runtime.cmake.rne-supply'; dec='business-specific'; cap='TEST-HARNESS'; s=1; e=442; req=@('DYN-01'); role='owner'}
)

# ---- 铺面校验（每非空行恰被覆盖）＋区段符号/界校验 ----
$maps = @{ $cpp = @{}; $hpp = @{}; $cml = @{}; $tst = @{}; $rhpp = @{}; $rcpp = @{}; $rcml = @{} }
$allLines = @{ $cpp = $cppLines; $hpp = $hppLines; $cml = $cmlLines; $tst = $tstLines; $rhpp = $rhppLines; $rcpp = $rcppLines; $rcml = $rcmlLines }
foreach ($seg in $segments) {
    $f = $seg.file; $lines = $allLines[$f]
    if ($seg.e -gt $lines.Count) { throw "REGION-BOUNDS $($seg.id): $($seg.e) > $($lines.Count)" }
    $region = ($lines[($seg.s-1)..($seg.e-1)] -join "`n")
    if (-not $region.Contains($seg.sym)) { throw "SYMBOL-MISSING $($seg.id): $($seg.sym) in [$($seg.s)..$($seg.e)]" }
    for ($n = $seg.s; $n -le $seg.e; $n++) { $maps[$f][$n] = $true }
}
foreach ($f in @($cpp; $hpp; $cml; $tst; $rhpp; $rcpp; $rcml)) {
    $n = 0; $miss = @()
    foreach ($line in $allLines[$f]) { $n++; if ($line.Trim() -and -not $maps[$f].ContainsKey($n)) { $miss += $n } }
    if ($miss.Count) { throw "UNCOVERED LINES ${f}: $($miss -join ',')" }
}
Write-Output "tiling OK (cpp=$($cppLines.Count) hpp=$($hppLines.Count) cml=$($cmlLines.Count) tst=$($tstLines.Count) rhpp=$($rhppLines.Count) rcpp=$($rcppLines.Count) rcml=$($rcmlLines.Count))"

$evBase = 'RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T52/'
$searchBase = 'rg -n "RecursiveNewtonEuler|solve|setGravity|setPayload|SerialDevice|RigidDevice" RobWork/RobWorkSim/src/rwsim/util RobWork/RobWorkSim/src/rwsim/dynamics ' + $cpp
$rows = @()
foreach ($seg in $segments) {
    $f = $seg.file
    $row = [ordered]@{
        functionId = $seg.id
        ownerUnit = $(if ($f -in @($rhpp; $rcpp; $rcml)) { 'runtime' } else { 'dynamics' })
        requirements = $seg.req
        sources = @(@{
            path = $f; symbol = $seg.sym
            qualifiedSymbol = 'sdurws::ird::' + $(if ($f -in @($rhpp; $rcpp; $rcml)) { 'runtime::' } else { 'dynamics::' + $(if ($f -eq $cpp) { '(anonymous namespace)::' } else { '' }) }) + $seg.sym
            startLine = $seg.s; endLine = $seg.e
            sha256 = RegionDigest $f $seg.s $seg.e
        })
        targets = @('sdurws_ird_dynamics')
        usageRole = $seg.role
        permissionRef = $(if ($f -in @($rhpp; $rcpp)) { 'ARCH:3.6.1' } else { 'ARCH:3.6' })
        capabilityId = $seg.cap
        decision = $seg.dec
        status = 'migrated'
        search = @{
            command = $searchBase
            conclusion = 'REUSE-T52 定向实核（migration §13.3 WP-07）：RecursiveNewtonEuler.hpp/.cpp 公共 API 逐行实读（solve DOF+1 Wrench/setGravity 显式传递/getJointForces payload 差异登记）在案；本区段语义与职责见 differences'
        }
        differences = ''
        inputs = @(@{ path = $cml; sha256 = $digestCml })
        auditBoundary = '仅本功能源区段；不声称其余文件/单元已审计'
    }
    if ($f -eq $cml) { $row.inputs = @(@{ path = $cml; sha256 = $digestCml }); $row.targets = @('sdurws_ird_dynamics','sdurws_ird_dynamics_test') }
    if ($f -eq $tst) { $row.targets = @('sdurws_ird_dynamics_test') }
    if ($f -in @($rhpp; $rcpp; $rcml)) {
        $row.inputs = @(@{ path = $rcml; sha256 = $digestRCml })
        $row.targets = @('sdurws_ird_runtime')
    }
    if ($f -eq $rcml) { $row.targets = @('sdurws_ird_runtime') }
    if ($seg.dec -eq 'business-specific') { $row.nonOverlapEvidence = @{ path = $f; sha256 = FileDigest $f } }
    if ($seg.prod) {
        $row.production = $seg.prod
        # 证据文件由 gen-production.ps1 先行生成（管线序：production → matrix）；
        # 此处实算摘要，杜绝占位符与二次回填。
        $row.evidence = @{ path = $evBase + $seg.ev; sha256 = FileDigest ($evBase + $seg.ev) }
    }
    $rows += $row
}

# ---- differences 逐行填写（结项语义——审计可读的职责/差异文本）----
$diffText = @{
    'dynamics.module.header' = '文件头设计依据/上游复用边界/算法总览注释（WP-07 换核登记）：上游 RecursiveNewtonEuler 消费＋GAP-RNE-FRAME 薄适配边界＋T30 次要发现处置（setGravity 显式/setPayload 不消费/solveMotorTorques 不盲用）；零可执行语义'
    'dynamics.dto.types' = '摩擦三元组四态/关节行元数据/末端组成件值类型——产品业务数据模型，上游无对应物（BodyInfo 为关节帧物性承载，不承载摩擦四态与 Provenance 降级语义）；nonOverlap＝上游 rwsim::dynamics 无摩擦四态/估算标记等价类型（REUSE-T30 §4 摩擦承载边界在案）'
    'dynamics.property.recheck' = '物性有限性＋对称性相对 1e-12 防御复检（F-645 真相对口径）：产品防御面，建模侧 MDL-06 SPD 断言前置——本域不重复 SPD 判定；上游无对应物'
    'dynamics.endComponent.extract' = '工具/负载物性四态解析＋保守估算（D-DYN-6：com 缺失→安装点、惯量缺失→点质量、GeometricEstimate 计数）：§5.4/§5.5 产品语义，上游 setPayload 单通道无四态/估算对应物；mass 必填 fail-fast（MDL-06 分域）'
    'dynamics.rne.chain.types' = 'RNE 消费面集合（rne 载体句柄/solver/摩擦与变体保留面）：evaluate 内一次性计算载体的生命周期描述，零产品面名称/真值出口（CM-0 登记语义，REUSE-T52 契约 acceptance 1）'
    'dynamics.rne.device.build' = '验证提取＋工厂调用（2026-10-26 所有者构造面上收裁决——模型构造权归 runtime）：本区段纯值验证/提取（链词表/轴单位/零位偏置防御复检、物性有限性＋对称性 F-645、摩擦四态 MDL-16/F-632、估算计数、工具面——DynamicsError 语义保持）；设备构造与 GAP-RNE-FRAME 关节帧换算归 runtime::buildRneConsumptionDevice（dynamics->runtime 登记边；R_align 知识回归模型 owner 本位）'
    'dynamics.flange.compose' = '工具＋挂载负载平行轴合成进法兰体（§5.4 变体语义）＋薄适配回写（R_alignᵀ·com、R_alignᵀ·I·R_align 相似变换写回末连杆 BodyInfo）：上游 setPayload 通道因 getJointForces 工具条目 payload 力矩臂符号与手算物理相反（T30 次要发现②，本批代数复核在案）不消费——合成体并入法兰 BodyInfo 为产品语义等价承载'
    'dynamics.friction.term' = 'τ_fric=fv·q̇+fc·sgn₀(q̇)+bias（§5.5 sgn₀(0)=0）：产品摩擦模型——上游 RNE 无逐关节摩擦一职（REUSE-T30 §4 边界），叠加在产品层、不入上游'
    'dynamics.solve.channel' = '单通道求解：setGravity 显式传递（T30 次要发现①）→上游 solve（DOF+1 Wrench，base 系）→按关节类型投影（转动 dot(torque,z)／移动 dot(force,z)——solveMotorTorques 对移动关节也投影 torque 且末元素复用陈旧 z（上游缺陷），不盲用自行投影，migration §13.3 WP-07 行）；z_i=frameTframe(base,joint_i).R() 第 2 列；第 DOF+1 项按 R1 恒零不读'
    'dynamics.evaluate.entry' = '评估主入口编排：前置校验/维度时间轴/负载池/诊断素材/事件时间线/变体重合成/逐样本四通道（共享 State 位形）→摩擦叠加/功率能量/样本行组装/有效性摘要——§5.2 分项图产品语义，上游无编排对应物；接口面（请求/结局 DTO）零变化（kInverseDynContractVersion=1）'
    'dynamics.idyn.header.contract' = '契约头设计依据/上游复用边界注释＋kInverseDynContractVersion=1（接口面零变化）＋kRneaAlgorithmVersion 1→2（CON-04：换核＝数值实现源变更，旧结果不与新结果混比）'
    'dynamics.idyn.request.dto' = '请求 DTO（样本激励/负载池/事件时间线/模型只读引用/基座系重力）：§4.3/§5.3 契约面，零字段变化；上游无对应物'
    'dynamics.idyn.outcome.dto' = '结局 DTO（样本行/有效性/诊断/取消位）：§4.4 契约面，零字段变化；上游无对应物'
    'dynamics.idyn.evaluator.class' = '评估器类注释（职责执行序/分项口径）：WP-07 换核后步骤 2/3 注释同步（RNE 消费面构建/四通道经上游 solve）；接口零变化'
    'dynamics.cmake.targets' = 'dynamics 构建脚本：集成分支零改动（sdurw_math/kinematics/models/sdurwsim＋Eigen3 PRIVATE 既有登记）；冒烟分支 WP-07 起补框架供给（REUSE-T51 同款 find_package(RobWork)＋同名 ALIAS；sdurwsim 直含 targets/sdurwsim.cmake 导入目标——RobWorkSimConfig 对非安装树无条件 REQUIRED RobWorkStudio 且其 sdurws 验证链冒烟解析不了，探针在案；sdurwsim 非集成判定符全库零命中，不触 sdurw_kinematics 禁令）；门禁词表零改动'
    'dynamics.rnea.test.suite' = '测试面：既有 116 项 oracle 断言零放松全绿（y 轴静态/F580 移动关节滑移臂/倒挂反号/偏置权威角/工具负载合成/四通道恒等/黄金拉格朗日数据集）；REUSE-T52 增测——任意构型平衡叉积＋准静态能量平衡（独立物理 oracle 禁自比）＋上游直接对照（同模型同 State 内联同构设备直连 solve 逐位一致）＋断开实验负例承载（工厂薄适配换算去除→y 轴静态与弯曲平衡真实失败）'
    'runtime.rne.device.contract' = 'RNE 载体工厂公共契约头（runtime 唯一模型 owner 的构造面——2026-10-26 所有者构造面上收裁决；消费者＝WP-07 dynamics 已落位、WP-09 FD 预留）：载体值聚合/所有权生命周期/GAP-RNE-FRAME 薄适配契约注释——上游无对应物（runtime 侧产品职责），nonOverlap＝RNE 消费载体为产品编排语义非上游 API'
    'runtime.rne.device.factory' = '工厂实现：CanonicalModel→StateStructure+SerialDevice+RigidDevice 一次性载体——关节帧 tAligned=origin·R_align＋zeroOffset 烘焙＋连杆帧 R_alignᵀ（S6 同源语义）＋GAP-RNE-FRAME BodyInfo 关节帧换算（R_alignᵀ·com、R_alignᵀ·I·R_align——A2 裁决薄适配落点，2026-10-26 构造面上收后归 runtime 单点；§8.2/S7 产品映射/modeling 导出零触碰）；公共边界防御（空链/Fixed/轴单位/偏置有限——InputInvalid）；轴对齐函数与 WorkCellCompiler.cpp 同源（本单元内最小授权面镜像，合并归卫生批）；断开实验＝本工厂换算行去除→dynamics oracle 真实失败'
    'runtime.cmake.rne-supply' = 'runtime 构建脚本 REUSE-T52 增列：集成 target_sources 工厂 TU＋冒烟分支框架供给（REUSE-T51 同款 find_package(RobWork)＋同名 ALIAS＋sdurwsim 直含 targets/sdurwsim.cmake——非安装树 RobWorkSimConfig REQUIRED RobWorkStudio 探针在案；sdurwsim 非集成判定符全库零命中）——工厂 TU 双模式编译＝单一实现；门禁代码/词表零改动'
    }
foreach ($row in $rows) { $row.differences = $diffText[$row.functionId] }

# ---- 保留旧文件中未触碰的行（ForwardDynamics/replay/envelope/contract mains）----
$old = Get-Content (Join-Path $RepoRoot $design'reuse/functions/dynamics.json') -Raw | ConvertFrom-Json
$keepIds = @('dynamics.solveLinearSystem','dynamics.evalAcceleration','dynamics.ForwardDynamicsValidator.validate.rk4Stages','dynamics.DynamicsReplayProjector.sampleAt','dynamics.DynamicsEnvelopeCalculator.computeRms','dynamics.contract_test.ContractTestMain.main','dynamics.test.TestMainReport.main')
$newFunctions = @()
foreach ($id in $keepIds) { $newFunctions += @($old.functions | Where-Object { $_.functionId -eq $id }) }
foreach ($row in $rows) { if ($row.ownerUnit -eq 'dynamics') { $newFunctions += $row } }

$out = [ordered]@{ schemaVersion = 'ird-reuse-functions/1'; unit = 'dynamics'; functions = $newFunctions }
$json = ($out | ConvertTo-Json -Depth 8)
[IO.File]::WriteAllText((Join-Path $RepoRoot $design'reuse/functions/dynamics.json'), $json, (New-Object System.Text.UTF8Encoding($false)))
Write-Output "functions/dynamics.json written: $($newFunctions.Count) rows"

# ---- functions/runtime.json：文本式追加三行（避免整文件重排；已存在即跳过——
#      行内摘要由本脚本同一 Digest 口径生成，重跑时无需刷新）----
$rjPath = Join-Path $RepoRoot $design'reuse/functions/runtime.json'
$rj = [IO.File]::ReadAllText($rjPath)
if ($rj.Contains('runtime.rne.device.factory')) {
    Write-Output 'functions/runtime.json: rows already present — skip'
} else {
$rtRows = @($rows | Where-Object { $_.functionId -like 'runtime.*' })
$rtJson = ($rtRows | ConvertTo-Json -Depth 8)
if ($rtRows.Count -eq 1) { $rtJson = '[' + $rtJson + ']' }
# functions 数组闭合＝最后一个 ']'（其后为对象收尾 '}'）——实勘锚
$idx = $rj.LastIndexOf(']')
if ($idx -lt 0) { throw 'runtime.json functions array close not found' }
$prefix = $rj.Substring(0, $idx).TrimEnd()
if (-not $prefix.EndsWith('}')) { throw 'runtime.json functions tail not found' }
$rj = $prefix + ",`r`n" + ($rtJson -replace '^\[', '').Replace(']$', '').TrimEnd() + "`r`n  ]`r`n}"
[IO.File]::WriteAllText($rjPath, $rj, (New-Object System.Text.UTF8Encoding($false)))
Write-Output "functions/runtime.json appended: $($rtRows.Count) rows"
}

# ---- changes/REUSE-T52.json ----
$manifest = [ordered]@{
    schemaVersion = 'ird-reuse-change/1'
    taskId = 'REUSE-T52'
    taskFile = ($design + 'tasks/REUSE-T52.json')
    files = @(
        @{ path = $cpp; sha256 = $digestCpp; mode = 'adapter'; capabilities = @('DYN-RNE');
           boundary = 'WP-07（migration §13.3）逆动力学换核：上游 rwsim::util::RecursiveNewtonEuler（H1 直接 API）消费边界薄适配——StateStructure+SerialDevice+RigidDevice 一次性物化（S6 tAligned/R_alignᵀ/offset 烘焙同源语义镜像，独立 oracle 钉住）＋GAP-RNE-FRAME 质心/惯量关节帧换算（A2 裁决落点，S7/§8.2/modeling 导出零触碰）＋setGravity 显式传递＋按关节类型投影（不盲用 solveMotorTorques）；删除自写旧核 Vec3/Mat3/RneaChain/buildRneaChain/SampleGeometry/computeGeometry/rneaTorques 全清单；kRneaAlgorithmVersion 1→2（CON-04）；断开实验＝薄适配去除→y 轴静态/弯曲平衡真实失败'
           verify = $evBase + 'README.md' },
        @{ path = $hpp; sha256 = $digestHpp; mode = 'business'; capabilities = @();
           boundary = '契约头注释同步（WP-07 换核设计依据/职责执行序/上游边界）＋kRneaAlgorithmVersion 1→2（CON-04）；请求/结局/接口契约面零变化（kInverseDynContractVersion=1）'
           verify = $evBase + 'README.md' },
        @{ path = $cml; sha256 = $digestCml; mode = 'business'; capabilities = @();
           boundary = '冒烟分支框架供给（REUSE-T51 同款）：find_package(RobWork)＋sdurw_math/sdurw_models 同名 ALIAS＋sdurwsim 直含 targets/sdurwsim.cmake 导入目标（RobWorkSimConfig 非安装树 REQUIRED RobWorkStudio 探针在案）；sdurwsim 非集成判定符（全库零命中）；门禁代码/词表零改动；集成分支零改动'
           verify = $evBase + 'README.md' },
        @{ path = $tst; sha256 = $digestTst; mode = 'business'; capabilities = @();
           boundary = '既有 116 项 oracle 断言零放松全绿；REUSE-T52 增测：任意构型平衡叉积＋准静态能量平衡（独立物理 oracle）＋上游直接对照（内联同构设备直连 solve 逐位一致——test-fixture 授权面）；断开实验负例承载'
           verify = $evBase + 'README.md' },
        @{ path = $rhpp; sha256 = $digestRHpp; mode = 'business'; capabilities = @();
           boundary = 'runtime RNE 载体工厂公共契约头（2026-10-26 所有者构造面上收裁决——模型构造权归 runtime；消费者 WP-07 dynamics 已落位/WP-09 预留）：载体值聚合＋所有权生命周期＋GAP-RNE-FRAME 契约注释'
           verify = $evBase + 'README.md' },
        @{ path = $rcpp; sha256 = $digestRCpp; mode = 'adapter'; capabilities = @('DYN-RNE');
           boundary = 'RNE 载体工厂实现：CanonicalModel→StateStructure+SerialDevice+RigidDevice 一次性载体＋GAP-RNE-FRAME 关节帧换算（A2 裁决薄适配落点，构造面上收后归 runtime 单点；§8.2/S7 产品映射/modeling 导出零触碰）；轴对齐与 WorkCellCompiler.cpp 同源（最小授权面镜像）；断开实验＝换算行去除→dynamics oracle 真实失败'
           verify = $evBase + 'README.md' },
        @{ path = $rcml; sha256 = $digestRCml; mode = 'business'; capabilities = @();
           boundary = 'runtime REUSE-T52 增列：集成 target_sources 工厂 TU＋冒烟分支框架供给（REUSE-T51 同款同名 ALIAS＋sdurwsim 直含 targets/sdurwsim.cmake）；工厂 TU 双模式编译＝单一实现；门禁代码/词表零改动'
           verify = $evBase + 'README.md' }
    )
}
$mjson = $manifest | ConvertTo-Json -Depth 6
[IO.File]::WriteAllText((Join-Path $RepoRoot $design'reuse/changes/REUSE-T52.json'), $mjson, (New-Object System.Text.UTF8Encoding($false)))
Write-Output 'changes/REUSE-T52.json written'
Write-Output ("cpp=" + $digestCpp)
Write-Output ("hpp=" + $digestHpp)
Write-Output ("cml=" + $digestCml)
Write-Output ("tst=" + $digestTst)
Write-Output ("rhpp=" + $digestRHpp)
Write-Output ("rcpp=" + $digestRCpp)
Write-Output ("rcml=" + $digestRCml)
Write-Output ("upHpp=" + $digestUpHpp)
Write-Output ("upTgt=" + $digestUpTgt)
