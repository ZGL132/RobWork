# REUSE-T09 registration script: rewrite functions/trajectory.json capability rows,
# emit changes/REUSE-T09.json and production evidence reports with validator-identical digests.
$ErrorActionPreference = 'Stop'
$root = 'C:/Users/zgl18/.codex/worktrees/reuse-trajectory-poly/RobWork'
$design = 'RobWork/doc/industrial-robot-design'
$traj = 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/trajectory'
$smoothCpp = "$traj/src/Smooth.cpp"
$timeCpp = "$traj/src/TimeParam.cpp"
$smoothTest = "$traj/test/SmoothTest.cpp"
$timeTest = "$traj/test/TimeParamTest.cpp"
$trajCmake = "$traj/CMakeLists.txt"
$ev = "$design/traceability/reuse/REUSE-T09"
$exe = 'C:/Users/zgl18/.codex/worktrees/reuse-trajectory-poly/RobWork/build/RobWorkStudio/bin/Release/sdurws_ird_trajectory_test.exe'

function ReadT([string]$rel) {
  $full = Join-Path $root $rel
  return ([IO.File]::ReadAllText($full)).TrimStart([char]0xFEFF)
}
function Dig([string]$text) {
  $bytes = [Text.Encoding]::UTF8.GetBytes(($text.TrimStart([char]0xFEFF) -replace "`r`n", "`n").TrimEnd("`n") + "`n")
  $sha = [Security.Cryptography.SHA256]::Create()
  try { return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant() } finally { $sha.Dispose() }
}
function Bind([string]$rel) {
  [pscustomobject]@{ path = $rel; sha256 = (Dig (ReadT $rel)) }
}
function WholeSrc([string]$path, [string]$symbol, [string]$qualified) {
  $txt = ReadT $path
  $lines = @(($txt -replace "`r`n", "`n") -split "`n")
  [pscustomobject]@{ path = $path; symbol = $symbol; qualifiedSymbol = $qualified; startLine = 1; endLine = $lines.Count; sha256 = (Dig $txt) }
}
function SaveJson([string]$rel, $obj) {
  $full = Join-Path $root $rel
  $json = ConvertTo-Json -InputObject $obj -Depth 16
  [IO.File]::WriteAllText($full, $json + "`n", [Text.UTF8Encoding]::new($false))
  Write-Output "wrote $rel"
}

# ---------- C. production reports first (rows bind their digests) ----------
$capUpstream = @('RobWork/RobWork/src/rw/math/Polynomial.hpp', 'RobWork/RobWork/src/rw/math/PolynomialND.hpp', 'RobWork/RobWork/src/rw/math/CMakeLists.txt')
$normalCmd = "& `"$exe`" --gtest_output=xml:$ev/normal/trajectory-test.xml --ird_report=$ev/normal/trajectory-test-report.json"

$prodSmooth = [pscustomobject]@{
  schemaVersion = 'ird-reuse-production/1'
  functionId = 'trajectory.smoothQuinticHermiteReuse'
  target = 'sdurws_ird_trajectory'
  bindings = @((Bind $smoothCpp), (Bind $smoothTest), (Bind $trajCmake)) + @($capUpstream | ForEach-Object { Bind $_ })
  normal = [pscustomobject]@{
    command = $normalCmd
    xml = (Bind "$ev/normal/trajectory-test.xml")
    log = (Bind "$ev/normal/trajectory-test-run.log")
    exitCode = 0
  }
  disconnected = [pscustomobject]@{
    command = "& `"$exe`" --gtest_filter=SmoothRecheckTest.SmoothAnalyticGoldenCase_WP16T07_NFRCOR01 --gtest_output=xml:$ev/disconnected/smooth/gtest.xml"
    xml = (Bind "$ev/disconnected/smooth/gtest.xml")
    log = (Bind "$ev/disconnected/smooth/run.log")
    exitCode = 1
    mutation = [pscustomobject]@{
      source = $smoothCpp
      originalSha256 = (Dig (ReadT $smoothCpp))
      description = 'sampleAt将h101基误取h100（上游基表返回值扰动——丢弃正确消费）。真实重建产品库并重跑全套用例：解析黄金/容差门控/上游对照/下游局部重规划断言失败；随后恢复源码并核验SHA一致、复跑全绿'
      }
    mutatedSource = (Bind "$ev/disconnected/Smooth.mutated.cpp")
  }
  oracle = [pscustomobject]@{
    command = $normalCmd
    xml = (Bind "$ev/normal/trajectory-test.xml")
    log = (Bind "$ev/normal/trajectory-test-run.log")
    exitCode = 0
    selectedCases = @('SmoothRecheckTest.SmoothAnalyticGoldenCase_WP16T07_NFRCOR01', 'SmoothRecheckTest.SmoothCollinearIsNoChange_WP16T07_ACC1')
  }
  upstreamComparison = [pscustomobject]@{
    command = $normalCmd
    xml = (Bind "$ev/normal/trajectory-test.xml")
    log = (Bind "$ev/normal/trajectory-test-run.log")
    exitCode = 0
    selectedCases = @('SmoothRecheckTest.SmoothUpstreamPolynomialCrossCheck_REUSE_T09')
  }
  note = '正常/oracle/上游对照来自同一最终完整执行，原件只保存一次；真实断开另编另跑。独验未执行。'
}
SaveJson "$ev/production-smooth.json" $prodSmooth

$prodTime = [pscustomobject]@{
  schemaVersion = 'ird-reuse-production/1'
  functionId = 'trajectory.timeParamHermiteBasesReuse'
  target = 'sdurws_ird_trajectory'
  bindings = @((Bind $timeCpp), (Bind $timeTest), (Bind $trajCmake)) + @($capUpstream | ForEach-Object { Bind $_ })
  normal = [pscustomobject]@{
    command = $normalCmd
    xml = (Bind "$ev/normal/trajectory-test.xml")
    log = (Bind "$ev/normal/trajectory-test-run.log")
    exitCode = 0
  }
  disconnected = [pscustomobject]@{
    command = "& `"$exe`" --gtest_filter=TimeParamSepticTest.C3KnotContinuityWithDwell --gtest_output=xml:$ev/disconnected/timeparam/gtest.xml"
    xml = (Bind "$ev/disconnected/timeparam/gtest.xml")
    log = (Bind "$ev/disconnected/timeparam/run.log")
    exitCode = 1
    mutation = [pscustomobject]@{
      source = $timeCpp
      originalSha256 = (Dig (ReadT $timeCpp))
      description = 'evalBasis将二阶导改为返回值基（丢弃上游derivative(2)结果）。真实重建产品库并重跑全套用例：黄金/C²结点匹配/缩放收敛/驻留/确定性/上游对照等43个断言失败；随后恢复源码并核验SHA一致、复跑全绿'
      }
    mutatedSource = (Bind "$ev/disconnected/TimeParam.mutated.cpp")
  }
  oracle = [pscustomobject]@{
    command = $normalCmd
    xml = (Bind "$ev/normal/trajectory-test.xml")
    log = (Bind "$ev/normal/trajectory-test-run.log")
    exitCode = 0
    selectedCases = @('TimeParamGoldenTest.TwoPointRestToRestProfile', 'TimeParamGoldenTest.C2KnotMatchCentralDifference', 'TimeParamSepticTest.RestToRestGoldenProfileJerkLimited', 'TimeParamSepticTest.C3KnotContinuityWithDwell', 'TimeParamDegenerate.ZeroTravelSegmentConstant', 'TimeParamDegenerate.HugeLimitsReachShortDurationFloor')
  }
  upstreamComparison = [pscustomobject]@{
    command = $normalCmd
    xml = (Bind "$ev/normal/trajectory-test.xml")
    log = (Bind "$ev/normal/trajectory-test-run.log")
    exitCode = 0
    selectedCases = @('TimeParamUpstreamCheck.QuinticRestToRestUpstreamCrossCheck', 'TimeParamUpstreamCheck.SepticRestToRestUpstreamCrossCheck', 'TimeParamUpstreamCheck.InteriorKnotNonZeroTangentUpstreamCrossCheck')
  }
  note = '正常/oracle/上游对照来自同一最终完整执行，原件只保存一次；真实断开另编另跑。独验未执行。'
}
SaveJson "$ev/production-timeparam.json" $prodTime

# ---------- A. functions/trajectory.json ----------
$fpath = "$design/reuse/functions/trajectory.json"
$m = ReadT 'RobWork/doc/industrial-robot-design/reuse/functions/trajectory.json' | ConvertFrom-Json
$remove = @('trajectory.hermiteH000', 'trajectory.hermiteH100', 'trajectory.hermiteH001', 'trajectory.hermiteH101',
  'trajectory.basisH0000', 'trajectory.basisH0010', 'trajectory.basisH0001', 'trajectory.basisH0011',
  'trajectory.septicS0', 'trajectory.septicS1', 'trajectory.septicS2', 'trajectory.septicS3',
  'trajectory.canDropPoint', 'trajectory.buildTangents', 'trajectory.measureDeviation', 'trajectory.smoothQuinticHermiteReuse', 'trajectory.timeParamHermiteBasesReuse')
$kept = @($m.functions | Where-Object { $remove -notcontains $_.functionId })
Write-Output ("kept rows: {0}" -f $kept.Count)

$rowSmooth = [pscustomobject]@{
  functionId = 'trajectory.smoothQuinticHermiteReuse'
  ownerUnit = 'trajectory'
  requirements = @('TRJ-04')
  sources = @((WholeSrc $smoothCpp 'hermiteQuinticBases' 'sdurws::ird::trajectory::(anonymous namespace)::hermiteQuinticBases'),
              (WholeSrc $smoothTest 'SmoothUpstreamPolynomialCrossCheck_REUSE_T09' ''))
  targets = @('sdurws_ird_trajectory')
  usageRole = 'adapter'
  permissionRef = 'ARCH:3.6.1'
  capabilityId = 'MATH-POLYNOMIAL'
  decision = 'adapter'
  status = 'migrated'
  search = [pscustomobject]@{
    command = 'rg -n "hermiteQuinticBases|Polynomial|evaluateDerivatives" RobWork/RobWorkStudio/src/rwslibs/industrialrobot/trajectory/src/Smooth.cpp RobWork/RobWork/src RobWork/RobWorkSim/src RobWork/RobWork/gtest'
    conclusion = '生产链smooth→QuinticHermiteGeometry::sampleAt消费上游evaluate基值；独立解析oracle（refH000族/refSampleThreePoint）、上游evaluateDerivatives直接对照、真实断开实验与性能/分配核查完成（REUSE-T09证据目录）；本行整文件区段吸收原hermiteH000~H101与canDropPoint/buildTangents/measureDeviation审计行'
  }
  differences = '基值求值上游化（升幂系数表+Horner evaluate）；λ收缩切线、端点/必经点保护、共线零偏差NoChange位级判定保留；端点与结点位级还原由整数系数Horner在u=0/1无舍入保持（迁移说明§2）；evaluateDerivatives因每调用堆分配不入热路径'
  inputs = @((Bind $trajCmake))
  production = [pscustomobject]@{ entry = 'hermiteQuinticBases'; upstream = 'Polynomial'; consumer = 'hermiteQuinticBases'; target = 'sdurws_ird_trajectory' }
  evidence = (Bind "$ev/production-smooth.json")
}
$rowTime = [pscustomobject]@{
  functionId = 'trajectory.timeParamHermiteBasesReuse'
  ownerUnit = 'trajectory'
  requirements = @('TRJ-05', 'TRJ-06', 'TRJ-08-S1')
  sources = @((WholeSrc $timeCpp 'evalBasis' 'sdurws::ird::trajectory::(anonymous namespace)::evalBasis'),
              (WholeSrc $timeTest 'InteriorKnotNonZeroTangentUpstreamCrossCheck' ''))
  targets = @('sdurws_ird_trajectory')
  usageRole = 'adapter'
  permissionRef = 'ARCH:3.6.1'
  capabilityId = 'MATH-POLYNOMIAL'
  decision = 'adapter'
  status = 'migrated'
  search = [pscustomobject]@{
    command = 'rg -n "quinticBases|septicBases|evalBasis|Polynomial|evaluateDerivatives" RobWork/RobWorkStudio/src/rwslibs/industrialrobot/trajectory/src/TimeParam.cpp RobWork/RobWork/src RobWork/RobWorkSim/src RobWork/RobWork/gtest'
    conclusion = '生产链timeParameterize→evaluateSpanDerivs消费上游基族表（derivative链+evaluate）；独立解析oracle（refH0001/refS2族）、上游evaluateDerivatives直接对照、真实断开实验与性能/分配核查完成（REUSE-T09证据目录）；本行整文件区段吸收原basisH0000~H0011与septicS0~S3审计行'
  }
  differences = '五次/七次基值与一~三阶导求值上游化（derivative构造导数链+evaluate Horner）；驻留常值分支、结点二/三阶导恒0纪律、中心差分、初值闭式、缩放确认环、C²/C³守卫与素材保留；时间换算h^-k保留在业务合成处；evaluateDerivatives因每调用堆分配不入热路径'
  inputs = @((Bind $trajCmake))
  production = [pscustomobject]@{ entry = 'evalBasis'; upstream = 'Polynomial'; consumer = 'evalBasis'; target = 'sdurws_ird_trajectory' }
  evidence = (Bind "$ev/production-timeparam.json")
}
$m.functions = @($kept) + @($rowSmooth) + @($rowTime)
SaveJson 'RobWork/doc/industrial-robot-design/reuse/functions/trajectory.json' $m

# ---------- B. changes/REUSE-T09.json ----------
$chg = [pscustomobject]@{
  schemaVersion = 'ird-reuse-change/1'
  taskId = 'REUSE-T09'
  taskFile = "$design/tasks/REUSE-T09.json"
  files = @(
    [pscustomobject]@{ path = $smoothCpp; sha256 = (Dig (ReadT $smoothCpp)); mode = 'adapter'; capabilities = @('MATH-POLYNOMIAL'); boundary = '五次Hermite基值求值上游化（升幂系数表+evaluate Horner）；λ收缩、端点/必经点、共线零偏差NoChange位级判定、公共接口与诊断不变'; verify = "$ev/production-smooth.json" },
    [pscustomobject]@{ path = $timeCpp; sha256 = (Dig (ReadT $timeCpp)); mode = 'adapter'; capabilities = @('MATH-POLYNOMIAL'); boundary = '五次/七次基族表与导数链上游化（derivative+evaluate）；驻留分支/结点高阶导恒0/限值缩放/C²C³守卫/素材不变；evaluateDerivatives不入热路径（测试侧对照）'; verify = "$ev/production-timeparam.json" },
    [pscustomobject]@{ path = $smoothTest; sha256 = (Dig (ReadT $smoothTest)); mode = 'business'; capabilities = @(); boundary = '独立解析oracle保持＋上游evaluateDerivatives对照＋非有限路点拒绝用例'; verify = "$ev/normal/trajectory-test-run.log" },
    [pscustomobject]@{ path = $timeTest; sha256 = (Dig (ReadT $timeTest)); mode = 'business'; capabilities = @(); boundary = '独立解析oracle保持＋上游对照三件（五次/七次/非零切线）＋退化段/短时长用例'; verify = "$ev/normal/trajectory-test-run.log" }
  )
}
SaveJson "$design/reuse/changes/REUSE-T09.json" $chg
Write-Output 'DONE'
