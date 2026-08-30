# Fixture empty-acceptance: remove all requirement-acceptance trace rows from section 16.
# Expected gate diagnostic keyword: no acceptance trace.
param([string]$TempRoot)

. (Join-Path $PSScriptRoot '..\inject-helper.ps1')
$path = Join-Path $TempRoot 'requirements.md'
$file = Read-TextFile -Path $path
$newline = if ($file.Text -match "`r`n") { "`r`n" } else { "`n" }
$lines = $file.Text -split "`r?`n"
$out = New-Object System.Collections.Generic.List[string]
$inTrace = $false
foreach ($line in $lines) {
    if ($line -match '^## 16\.') { $inTrace = $true; $out.Add($line); continue }
    if ($inTrace -and $line -match '^## 17\.') { $inTrace = $false; $out.Add($line); continue }
    if ($inTrace -and $line -match '^\|') { continue }
    $out.Add($line)
}
Write-TextFile -Path $path -Text (($out -join $newline)) -Encoding $file.Encoding
