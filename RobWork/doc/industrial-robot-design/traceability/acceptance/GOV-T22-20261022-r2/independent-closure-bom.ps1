param([string]$RepoRoot,[string]$OutputDir)
$ErrorActionPreference='Stop'
[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false)
$OutputEncoding=[Console]::OutputEncoding
$sha='4281543becc1a64f83087e2a752826480f8d2ce4'
if ((& git -C $RepoRoot rev-parse HEAD).Trim() -ne $sha) { throw 'Run against fixed reviewed SHA before committing evidence.' }
[IO.Directory]::CreateDirectory($OutputDir)|Out-Null
$source=Join-Path $RepoRoot 'RobWork/scripts/industrialrobot/test-reuse-governance.ps1'
$text=[IO.File]::ReadAllText($source)
# 复用被验 SHA 的真实隔离初始化、结构夹具和报告收尾，仅选取两个新用例。
# 不修改校验器，不伪造产品验收；合成报告仅用于验证关闭记录解析行为。
$cut=$text.IndexOf("    Case '01-clean'")
$tail=$text.IndexOf("} catch {`r`n    `$driverFailure =",$cut)
if ($tail -lt 0) { $tail=$text.IndexOf("} catch {`n    `$driverFailure =",$cut) }
if ($cut -lt 0 -or $tail -lt 0) { throw 'Frozen driver extraction failed.' }
$prefix=$text.Substring(0,$cut).Replace('$expectedTests = 135','$expectedTests = 2')
$prefix=$prefix.Replace('$PSScriptRoot',"(Join-Path `$RepoRoot 'RobWork/scripts/industrialrobot')")
$prefix=$prefix.Replace('function Put([string]$path,[string]$text)', 'function OriginalPut([string]$path,[string]$text)')
$wrapper=@'
function Put([string]$path,[string]$text) {
    # 单一差异：合法 UTF-8 BOM 放在本夹具自己的 suitability 证据文本中。
    # 原 Hash 已剥 BOM，工作树及冻结 ReadText 已剥 BOM；提交冻结同一原字节。
    if ($script:injectBom -and $path -eq ($reuse+'fixture-suitability.txt')) { $text=[char]0xFEFF+$text }
    OriginalPut $path $text
}
'@
# wrapper 定义必须先于 try 初始化；所有原函数仍原样执行。
$prefix=$prefix.Replace("try {`r`n    & git clone",$wrapper+"`r`ntry {`r`n    & git clone")
if (-not $prefix.Contains('function Put(')) { $prefix=$prefix.Replace("try {`n    & git clone",$wrapper+"`ntry {`n    & git clone") }
$cases=@'
    $script:injectBom=$false
    Case 'independent-closure-no-bom-control' 'PASS' { [void](ClosureFixture) }
    $script:injectBom=$true
    Case 'independent-closure-bom-same-accepted-bytes' 'PASS' { [void](ClosureFixture) }
'@
$generated=Join-Path $OutputDir 'generated-closure-driver.ps1'
[IO.File]::WriteAllText($generated,$prefix+$cases+"`r`n"+$text.Substring($tail),[Text.UTF8Encoding]::new($true))
$runtime=(Get-Process -Id $PID).Path
& $runtime -NoProfile -File $generated -RepoRoot $RepoRoot -OutputDir $OutputDir
$code=$LASTEXITCODE
$report=Join-Path $OutputDir 'reuse-test-report.json'
if(Test-Path $report){
    $r=[IO.File]::ReadAllText($report)|ConvertFrom-Json
    $r|Add-Member reviewedCommit $sha
    $r|Add-Member independentProbeSha256 (Get-FileHash $PSCommandPath -Algorithm SHA256).Hash
    $r|Add-Member frozenFixtureDriverSha256 (Get-FileHash $source -Algorithm SHA256).Hash
    [IO.File]::WriteAllText($report,($r|ConvertTo-Json -Depth 15),[Text.UTF8Encoding]::new($false))
}
exit $code
