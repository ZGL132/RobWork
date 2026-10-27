# REUSE-T32 摘要助手：与 validate-reuse.ps1 的 ReadText/Digest 同一口径
# （BOM 剥离、CRLF→LF、尾部换行规范化），供矩阵/凭证/production 报告生成。
# 用法：pwsh -File t32-digest.ps1 -File <路径>            → 文件摘要
#       pwsh -File t32-digest.ps1 -RegionFile <路径> -Start n -End m → 区段摘要
param([string]$File, [string]$RegionFile, [int]$Start = 0, [int]$End = 0)
$ErrorActionPreference = 'Stop'
function Read-Normalized([string]$path) {
  $t = [IO.File]::ReadAllText($path)
  return $t.TrimStart([char]0xFEFF)
}
function Get-Digest([string]$text) {
  $bytes = [Text.Encoding]::UTF8.GetBytes(($text.TrimStart([char]0xFEFF) -replace "`r`n", "`n").TrimEnd("`n") + "`n")
  $sha = [Security.Cryptography.SHA256]::Create()
  try { return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant() } finally { $sha.Dispose() }
}
if ($RegionFile) {
  $t = Read-Normalized $RegionFile
  $lines = @(($t -replace "`r`n", "`n") -split "`n")
  if ($End -gt $lines.Count) { throw "region out of bounds: $End > $($lines.Count)" }
  $region = ($lines[($Start - 1)..($End - 1)] -join "`n")
  Write-Output (Get-Digest $region)
} else {
  Write-Output (Get-Digest (Read-Normalized $File))
}
