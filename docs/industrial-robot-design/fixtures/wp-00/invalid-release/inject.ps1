# Fixture invalid-release: set the ARC-01 trace row release to the illegal value R9.
# Expected gate diagnostic keyword: invalid release.
param([string]$TempRoot)

. (Join-Path $PSScriptRoot '..\inject-helper.ps1')
$path = Join-Path $TempRoot 'requirement-traceability.csv'
$file = Read-TextFile -Path $path
$text = $file.Text -replace '(?m)^("ARC-01".*),"R1","Planned"\r?$', '$1,"R9","Planned"'
Write-TextFile -Path $path -Text $text -Encoding $file.Encoding
