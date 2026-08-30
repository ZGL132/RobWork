# Fixture stale-csv: edit the ARC-01 requirement summary without regenerating the CSV.
# Expected gate diagnostic keyword: CSV stale.
param([string]$TempRoot)

. (Join-Path $PSScriptRoot '..\inject-helper.ps1')
$path = Join-Path $TempRoot 'requirements.md'
$file = Read-TextFile -Path $path
$text = $file.Text -replace '(?m)^(\| ARC-01 \|[^|]*\|[^`]*`ProjectRevision`)', '$1X'
Write-TextFile -Path $path -Text $text -Encoding $file.Encoding
