# Fixture table-shape: drop the trailing pipe of the WP-07 row in the master plan table.
# Expected gate diagnostic keyword: table separators.
param([string]$TempRoot)

. (Join-Path $PSScriptRoot '..\inject-helper.ps1')
$path = Join-Path $TempRoot 'development-task-breakdown.md'
$file = Read-TextFile -Path $path
$lines = $file.Text -split "`r?`n"
for ($i = 0; $i -lt $lines.Count; $i++) {
    if ($lines[$i] -match '^\|\s*WP-07\s*\|.*\|\s*$') {
        $lines[$i] = $lines[$i] -replace '\|\s*$', ''
        break
    }
}
Write-TextFile -Path $path -Text (($lines -join "`r`n") + "`r`n") -Encoding $file.Encoding
