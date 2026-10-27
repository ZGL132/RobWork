# Independent acceptance hash check (REUSE-T53-20261027-r1).
# Digest = BOM strip + CRLF->LF + trailing newline, SHA256 over UTF-8 bytes.
function Get-Digest([string]$p) {
  $b = [System.IO.File]::ReadAllBytes($p)
  if ($b.Length -ge 3 -and $b[0] -eq 0xEF -and $b[1] -eq 0xBB -and $b[2] -eq 0xBF) { $b = $b[3..($b.Length-1)] }
  $s = [System.Text.Encoding]::UTF8.GetString($b)
  $s = $s.Replace("`r`n", "`n")
  if (-not $s.EndsWith("`n")) { $s += "`n" }
  $bytes = [System.Text.Encoding]::UTF8.GetBytes($s)
  $sha = [System.Security.Cryptography.SHA256]::Create()
  ([System.BitConverter]::ToString($sha.ComputeHash($bytes)) -replace '-','').ToLower()
}
$repo = 'D:\10_Source_Repos\21_robot\RobWork'
$ird  = Join-Path $repo 'RobWork\RobWorkStudio\src\rwslibs\industrialrobot'
$doc  = Join-Path $repo 'RobWork\doc\industrial-robot-design'
$checks = @(
  @((Join-Path $ird 'dynamics\src\ForwardDynamics.cpp'), 'f6951c8475aaa0a374511c77524bb38af881a882d43d3031fce5802e2d66ccff'),
  @((Join-Path $ird 'runtime\src\FdEngineSession.cpp'), '16ab18d0fb14247411422b1b670d6531f62e75cdc30f131d8e70ad9b351b5ee6'),
  @((Join-Path $ird 'runtime\include\sdurws\ird\runtime\FdEngineSession.hpp'), 'de57acd48234314f3ad27b8fcd40e58f8f04b73a43d6553bd2f93e383f8d7517'),
  @((Join-Path $repo 'RobWork\RobWorkSim\src\rwsim\simulator\PhysicsEngine.hpp'), 'd605f1c73c051264c9d494b519fcf7cc57aea581d8a9d240850016f64ff86d57'),
  @((Join-Path $repo 'RobWork\RobWorkSim\src\rwsim\CMakeLists.txt'), '4674b8b2d8d5bf9a90cb62cea11091a8804d05386be23f585b926181f6a402ec'),
  @((Join-Path $ird 'dynamics\CMakeLists.txt'), '4700c32bb7092755c72b3f64aef34cd4fafc3349250b1327aa999ee409ea5d26'),
  @((Join-Path $ird 'runtime\CMakeLists.txt'), '5ef2e38b8e00e67c8889e50f5a09f0146908d2f791d2b52f5d669089313577cd'),
  @((Join-Path $doc 'traceability\reuse\REUSE-T53\production\normal-full-suite.xml'), '333dfbefa6ee8e2113adc70f6a0b1e4745e560174d360eb32ba29f17e045b535'),
  @((Join-Path $doc 'traceability\reuse\REUSE-T53\production\normal-full-suite.log'), '7d319e5e85c1629c25d2f9e3c25dc6dc57c51719c899be8061503ed3e59183d8'),
  @((Join-Path $doc 'traceability\reuse\REUSE-T53\production\oracle.xml'), '7bef6498ed65da0f67cf1dc59cd2712b74b34c7ec7f1bbdd8d7a9d0d8f1aefbf'),
  @((Join-Path $doc 'traceability\reuse\REUSE-T53\production\oracle.log'), 'ccd7d38f4e2af0c86e4e184aad6c67482dc7fc9faef7c1d9e59883ef93015a07'),
  @((Join-Path $doc 'traceability\reuse\REUSE-T53\production\disconnected.xml'), '4951cc3530a936a9e19de7ab1ce903734316b3c7d15517cafe9cc9d88c1da935'),
  @((Join-Path $doc 'traceability\reuse\REUSE-T53\production\disconnected.log'), '9fe4b971f891d25afd684307689b789f1c46324b99aca0312a9cf28040e6024d'),
  @((Join-Path $doc 'traceability\reuse\REUSE-T53\production\disconnected-session.xml'), '726eb7a27dc9c87422704163271da4a817b15b992d0388b718b06c760f59478e'),
  @((Join-Path $doc 'traceability\reuse\REUSE-T53\production\disconnected-session.log'), 'b67b4ff8a591204a835d8f9180bf072376a0d251a8536340b54325c5e84e6f68'),
  @((Join-Path $doc 'traceability\reuse\REUSE-T53\production\disconnected-mutated-ForwardDynamics-zeroforce.cpp.txt'), 'b42dd4b1a10b3722c7d15eff05fe525dad2117655571e5c1a44acbaccef4a258'),
  @((Join-Path $doc 'traceability\reuse\REUSE-T53\production\disconnected-mutated-FdEngineSession-zeroforce.cpp.txt'), '3e3918084863c590547dcce5d8d4d3c07a720e3f24c5cf1192f889cef5304288'),
  @((Join-Path $doc 'traceability\reuse\REUSE-T53\production\run-fd.ps1'), 'e3d4df2ec09df532b0fc9e7f7085b1ba62e95e8e2013bdbb733e4647fb491575')
)
$fail = 0
foreach ($c in $checks) {
  $g = Get-Digest $c[0]
  if ($g -eq $c[1]) { Write-Output ("OK       " + (Split-Path $c[0] -Leaf) + " " + $g.Substring(0,12)) }
  else { $fail++; Write-Output ("MISMATCH " + $c[0] + " got=" + $g.Substring(0,16) + " expect=" + $c[1].Substring(0,16)) }
}
Write-Output ("TOTAL=" + $checks.Count + " MISMATCH=" + $fail)
if ($fail -gt 0) { exit 1 } else { exit 0 }
