# REUSE-T23 r1 独立验收：verification.json 中两个 -report.json 记录 SHA256 的再现性探测
# 变体：blob(LF) / 精确CRLF(仅LF->CRLF，保留末行状态) / UTF16LE / UTF16BOM
$rec = @{
  'red-report.json'   = 'd9f7ef183445754fa236dfb3731f1cab2cb2b227cf568341e5c3a6f815b3f970'
  'green-report.json' = '556e3ba7c84a20201b5b8e42ba90a119d8c264acc9c54ced53d764360d9a9590'
  'red.xml'           = '2df95d2389c07b48adca085f42a16ab6f2af5d4037c83954916702f9e78e53a9'
  'green.xml'         = 'cd9ead2ea485a28031dd32f70c6b014eb5c8ff983b9ed4983c2d3c7d965c58a4'
}
$base = 'RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T23/'
function Sha([byte[]]$b) { $s = [Security.Cryptography.SHA256]::Create(); ($s.ComputeHash($b) | ForEach-Object { $_.ToString('x2') }) -join '' }
foreach ($f in $rec.Keys) {
  $blob = & git cat-file blob "HEAD:$base$f"
  $lfText = ($blob -join "`n") + "`n"
  $lf  = Sha ([Text.Encoding]::UTF8.GetBytes($lfText))
  $crlfText = $lfText -replace "`n", "`r`n"
  $crlf = Sha ([Text.Encoding]::UTF8.GetBytes($crlfText))
  $u16  = Sha ([Text.Encoding]::Unicode.GetBytes($lfText))
  $want = $rec[$f]
  $verdict = if ($lf -eq $want) { 'LF-MATCH' } elseif ($crlf -eq $want) { 'CRLF-MATCH' } elseif ($u16 -eq $want) { 'UTF16-MATCH' } else { 'NO-MATCH' }
  Write-Output ("{0}: lf={1} crlf={2} utf16={3} -> {4}" -f $f, $lf.Substring(0,12), $crlf.Substring(0,12), $u16.Substring(0,12), $verdict)
}
