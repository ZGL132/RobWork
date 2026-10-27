# REUSE-T32：刷新 T26/T09 三份 production.json 中 trajectory/CMakeLists.txt
# 的输入指纹（本包注册新测试文件改变该文件摘要的唯一机械性后果；运行内容
# 与 xml/log 结论不变）。其余绑定逐项复核不动。
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$ErrorActionPreference = 'Stop'
$RepoRoot = 'D:/10_Source_Repos/21_robot/RobWork'
$cmRel = 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/trajectory/CMakeLists.txt'
function Get-Digest([string]$text) {
  $bytes = [Text.Encoding]::UTF8.GetBytes(($text.TrimStart([char]0xFEFF) -replace "`r`n", "`n").TrimEnd("`n") + "`n")
  $sha = [Security.Cryptography.SHA256]::Create()
  try { return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant() } finally { $sha.Dispose() }
}
$newSha = Get-Digest ([IO.File]::ReadAllText((Join-Path $RepoRoot $cmRel)))
$targets = @('RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T26/production.json',
             'RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T09/production-smooth.json',
             'RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T09/production-timeparam.json')
foreach ($rel in $targets) {
  $full = Join-Path $RepoRoot $rel
  $doc = Get-Content $full -Raw -Encoding UTF8 | ConvertFrom-Json
  $hit = 0
  foreach ($b in $doc.bindings) {
    if ($b.path -eq $cmRel) { $b.sha256 = $newSha; $hit++ }
  }
  if ($hit -ne 1) { throw "$rel : expected exactly 1 CMakeLists binding, found $hit" }
  [IO.File]::WriteAllText($full, ($doc | ConvertTo-Json -Depth 40), [Text.UTF8Encoding]::new($false))
  Write-Output "refreshed $rel"
}
Write-Output "new CMake sha: $newSha"
