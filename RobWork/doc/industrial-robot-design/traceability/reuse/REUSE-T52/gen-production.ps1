# REUSE-T52 生产证据 JSON 生成器 v2（ird-reuse-production/1——三 adapter 行
# 各自身份；共享 normal/oracle/upstream 运行原件，disconnected 各绑自身文件
# 内变异；Digest 口径同 validate-reuse）。
param([string]$RepoRoot = "D:/10_Source_Repos/21_robot/RobWork")
$ErrorActionPreference = 'Stop'
$design = 'RobWork/doc/industrial-robot-design/'
$ev = $design + 'traceability/reuse/REUSE-T52/'
$cpp  = 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/dynamics/src/InverseDynamics.cpp'
$cml  = 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/dynamics/CMakeLists.txt'
$tst  = 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/dynamics/test/RneaTest.cpp'
$rhpp = 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/runtime/include/sdurws/ird/runtime/RneConsumptionDevice.hpp'
$rcpp = 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/runtime/src/RneConsumptionDevice.cpp'
$rcml = 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/runtime/CMakeLists.txt'
$upHpp = 'RobWork/RobWorkSim/src/rwsim/util/RecursiveNewtonEuler.hpp'
$upTgt = 'RobWork/RobWorkSim/src/rwsim/CMakeLists.txt'

function Digest([string]$text) {
    $bytes = [Text.Encoding]::UTF8.GetBytes(($text.TrimStart([char]0xFEFF) -replace "`r`n", "`n").TrimEnd("`n") + "`n")
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant() } finally { $sha.Dispose() }
}
function Bind([string]$p) { @{ path = $p; sha256 = (Digest ([IO.File]::ReadAllText((Join-Path $RepoRoot $p)))) } }

$dCpp = Digest ([IO.File]::ReadAllText((Join-Path $RepoRoot $cpp)))
$rCppD = Digest ([IO.File]::ReadAllText((Join-Path $RepoRoot $rcpp)))
$mutZero = Digest ([IO.File]::ReadAllText((Join-Path $RepoRoot ($ev + 'disconnected-mutated-InverseDynamics-zeroffset.cpp.txt'))))
$mutGrav = Digest ([IO.File]::ReadAllText((Join-Path $RepoRoot ($ev + 'disconnected-mutated-InverseDynamics-setgravity.cpp.txt'))))
$mutRne  = Digest ([IO.File]::ReadAllText((Join-Path $RepoRoot ($ev + 'disconnected-mutated-RneConsumptionDevice.cpp.txt'))))
if ($mutZero -eq $dCpp -or $mutGrav -eq $dCpp -or $mutRne -eq $rCppD) { throw 'mutated copy identical to source' }

$integrationCmd = 'VS x64 集成树: pwsh -NoProfile -File RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T52/run-dynamics.ps1 -RepoRoot D:/10_Source_Repos/21_robot/RobWork'

# 共享绑定（三行依赖面并集的超集——FRESHNESS 检查按行取必需子集）
function SharedBindings {
    @((Bind $cpp), (Bind $cml), (Bind $tst), (Bind $rhpp), (Bind $rcpp), (Bind $rcml), (Bind $upHpp), (Bind $upTgt),
      @{ path = $ev + 'run-dynamics.ps1'; sha256 = (Digest ([IO.File]::ReadAllText((Join-Path $RepoRoot ($ev + 'run-dynamics.ps1'))))) })
}
function SharedRuns {
    @{ normal = @{ command = $integrationCmd + ' -Label full-suite（119 项＝118 既有断言零放松＋REUSE-T52 增测）'
                   xml = (Bind ($ev + 'full-suite.xml')); log = (Bind ($ev + 'full-suite.log')); exitCode = 0 }
      oracle = @{ command = $integrationCmd + ' -Label oracle -Filter ''DynRnea.BentConfigurationStaticEquilibriumCrossProduct_REUSE_T52:DynRnea.QuasiStaticEnergyBalancePotentialDelta_REUSE_T52:DynGolden.*''（独立物理 oracle：弯曲构型手算叉积平衡＋准静态功率积分＝手算势能差＋黄金数据集拉格朗日独立参考——禁自比）'
                  xml = (Bind ($ev + 'oracle.xml')); log = (Bind ($ev + 'oracle.log')); exitCode = 0 }
      upstreamComparison = @{ command = $integrationCmd + ' -Label upstream-comparison -Filter ''DynRnea.UpstreamDirectComparison_REUSE_T52''（同模型同 State：测试侧内联同构 RNE 载体直连 RecursiveNewtonEuler::solve，与产品评估器全量通道 τ EXPECT_EQ 逐位一致——生产路径真实路由经上游）'
                             xml = (Bind ($ev + 'upstream-comparison.xml')); log = (Bind ($ev + 'upstream-comparison.log')); exitCode = 0 } }
}

function MakeReport([string]$functionId, [string]$target, [string]$discLabel, [string]$discCmd, [hashtable]$mutation, [string]$mutatedPath) {
    $r = [ordered]@{
        schemaVersion = 'ird-reuse-production/1'
        functionId = $functionId
        target = $target
        bindings = SharedBindings
    }
    $r.normal = (SharedRuns).normal
    $r.oracle = (SharedRuns).oracle
    $r.upstreamComparison = (SharedRuns).upstreamComparison
    $r.disconnected = @{
        command = $discCmd
        xml = (Bind ($ev + $discLabel + '.xml')); log = (Bind ($ev + $discLabel + '.log')); exitCode = 1
        mutation = $mutation
        mutatedSource = (Bind $mutatedPath)
    }
    return $r
}

# ①dynamics 验证提取行（变异＝零位偏置提取断开——本行区段内）
$r1 = MakeReport 'dynamics.rne.device.build' 'sdurws_ird_dynamics' 'disconnected-devbuild' `
    ('变异＝buildRneConsumptionChain 内 jm.zeroOffset 提取行置 0（变异副本 ' + $ev + 'disconnected-mutated-InverseDynamics-zeroffset.cpp.txt）→重建→' + $integrationCmd + ' -Label disconnected-devbuild -Filter ''DynRnea.NonZeroZeroOffsetSnapshotUsesAuthoritativeAngle_R3_A8''（exit 非零＋真实失败＝权威角换算链断裂复现）；已恢复并全量复验 119 passed') `
    @{ source = $cpp; originalSha256 = $dCpp; description = '零位偏置提取断开（q_rw＝q_auth−offset 换算分量丢失——T13 语义链断裂）' } `
    ($ev + 'disconnected-mutated-InverseDynamics-zeroffset.cpp.txt')

# ②dynamics 通道求解行（变异＝setGravity 显式传递断开——本行区段内）
$r2 = MakeReport 'dynamics.solve.channel' 'sdurws_ird_dynamics' 'disconnected-solve' `
    ('变异＝solveChannel 内 setGravity 显式传递行注释（变异副本 ' + $ev + 'disconnected-mutated-InverseDynamics-setgravity.cpp.txt）→重建→' + $integrationCmd + ' -Label disconnected-solve -Filter ''DynRnea.StaticGravityTwoLinkAnalytic''（exit 非零＋真实失败＝T30 次要发现①复现：ctor 默认零重力静默无重力）；已恢复并全量复验 119 passed') `
    @{ source = $cpp; originalSha256 = $dCpp; description = '重力显式传递断开（上游 ctor 默认零重力——静默无重力）' } `
    ($ev + 'disconnected-mutated-InverseDynamics-setgravity.cpp.txt')

# ③runtime 载体工厂行（变异＝GAP-RNE-FRAME 关节帧换算断开——工厂源内）
$r3 = MakeReport 'runtime.rne.device.factory' 'sdurws_ird_runtime' 'disconnected' `
    ('变异＝buildRneConsumptionDevice 内 info.masscenter 换算行改连杆系直喂（变异副本 ' + $ev + 'disconnected-mutated-RneConsumptionDevice.cpp.txt）→重建→' + $integrationCmd + ' -Label disconnected -Filter ''DynRnea.BentConfigurationStaticEquilibriumCrossProduct_REUSE_T52:DynRnea.StaticGravityTwoLinkAnalytic''（exit 非零＋2 断言真实失败＝GAP-RNE-FRAME 复现：权威轴≠z 质心世界位置错位）；已恢复并全量复验 119 passed') `
    @{ source = $rcpp; originalSha256 = $rCppD; description = 'GAP-RNE-FRAME 关节帧质心换算断开（权威连杆系值直喂——权威轴≠z 时质心世界位置错位）' } `
    ($ev + 'disconnected-mutated-RneConsumptionDevice.cpp.txt')

foreach ($pair in @(@($r1, 'production-dyn-rne-device-build.json'),
                    @($r2, 'production-dyn-rne-solve-channel.json'),
                    @($r3, 'production-runtime-rne-device-factory.json'))) {
    $json = $pair[0] | ConvertTo-Json -Depth 6
    [IO.File]::WriteAllText((Join-Path $RepoRoot ($ev + $pair[1])), $json, (New-Object System.Text.UTF8Encoding($false)))
    Write-Output ("written: " + $pair[1])
}
Write-Output ("dCpp=" + $dCpp + " rCpp=" + $rCppD)
