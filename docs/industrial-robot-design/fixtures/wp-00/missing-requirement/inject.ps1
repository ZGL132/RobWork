# Fixture missing-requirement: delete one requirement row (UX-08) from requirements.md.
# Expected gate diagnostic keyword: 数量错误 / CSV 缺失 (requirement count mismatch).
param([string]$TempRoot)

. (Join-Path $PSScriptRoot '..\inject-helper.ps1')
$path = Join-Path $TempRoot 'requirements.md'
$file = Read-TextFile -Path $path
$text = $file.Text -replace '(?m)^\|\s*UX-08\s*\|[^\r\n]*\r?\n', ''
Write-TextFile -Path $path -Text $text -Encoding $file.Encoding
