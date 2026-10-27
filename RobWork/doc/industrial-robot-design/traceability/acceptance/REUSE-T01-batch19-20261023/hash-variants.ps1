$root=Join-Path $PSScriptRoot 'repo'
$sha=[Security.Cryptography.SHA256]::Create()
foreach($ref in @('HEAD:RobWork/scripts/industrialrobot/validate-reuse.ps1','daee4f8905587b05cd1bab88bb570b8dc240097c:RobWork/scripts/industrialrobot/reuse-contract.ps1','eddb82a8e29f91660b048a69f4ec90c3d0a0e63a:RobWork/scripts/industrialrobot/reuse-contract.ps1')) {
 $p=[Diagnostics.Process]::new();$p.StartInfo.FileName='git';$p.StartInfo.WorkingDirectory=$root;$p.StartInfo.UseShellExecute=$false;$p.StartInfo.RedirectStandardOutput=$true
 foreach($a in @('cat-file','blob',$ref)){$p.StartInfo.ArgumentList.Add($a)}
 [void]$p.Start();$stream=[IO.MemoryStream]::new();$p.StandardOutput.BaseStream.CopyTo($stream);$p.WaitForExit();$b=$stream.ToArray()
 $text=[Text.Encoding]::UTF8.GetString($b);$lf=$text.Replace("`r`n","`n").TrimStart([char]0xFEFF)
 foreach($kind in @('raw','lf','crlf','lf-bom','crlf-bom')) {
  $v=$lf;if($kind.StartsWith('crlf')){$v=$lf.Replace("`n","`r`n")};if($kind.EndsWith('bom')){$v=[char]0xFEFF+$v}
  $bytes=[Text.Encoding]::UTF8.GetBytes($v);if($kind -eq 'raw'){$bytes=$b}
  Write-Output "$ref $kind $(([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-','')) bytes=$($bytes.Length)"
 }
}
