# Fixture duplicate-requirement: duplicate the UX-08 requirement row in requirements.md.
# Expected gate diagnostic keyword: IDs not unique.
param([string]$TempRoot)

. (Join-Path $PSScriptRoot '..\inject-helper.ps1')
$path = Join-Path $TempRoot 'requirements.md'
$file = Read-TextFile -Path $path
$text = $file.Text -replace '(?m)^(\|\s*UX-08\s*\|[^\r\n]*\r?\n)', '$1$1'
Write-TextFile -Path $path -Text $text -Encoding $file.Encoding
