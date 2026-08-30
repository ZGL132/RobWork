# Shared helpers for WP-00 fixture injection scripts (Task WP-00-T03).
# Each helper preserves the target file's original BOM state and CRLF line endings
# so that the only change in a fixture tree is the single injected violation.

function Get-TextFileEncoding {
    param([string]$Path)
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    $hasBom = ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF)
    return @{ HasBom = $hasBom; Encoding = (New-Object System.Text.UTF8Encoding($hasBom)) }
}

function Read-TextFile {
    param([string]$Path)
    $enc = Get-TextFileEncoding -Path $Path
    return @{ Text = [System.IO.File]::ReadAllText($Path); Encoding = $enc.Encoding }
}

function Write-TextFile {
    param([string]$Path, [string]$Text, [System.Text.Encoding]$Encoding)
    [System.IO.File]::WriteAllText($Path, $Text, $Encoding)
}
