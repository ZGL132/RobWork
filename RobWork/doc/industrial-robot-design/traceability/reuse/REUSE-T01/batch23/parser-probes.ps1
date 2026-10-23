param([string]$Script,[string]$OutDir)
$ErrorActionPreference='Stop'
[IO.Directory]::CreateDirectory($OutDir) | Out-Null
$b23Ast=[Management.Automation.Language.Parser]::ParseFile($Script,[ref]$null,[ref]$null)
$b23Function=$b23Ast.Find({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq 'Get-FailedTestNames'},$true)
. ([ScriptBlock]::Create($b23Function.Extent.Text))
$b23Cases=@(
    @{name='missing';xml=$null;expected=@('<xml-missing>')},
    @{name='malformed';xml='<testsuites';expected=@('<xml-unparsable>')},
    @{name='zero';xml='<testsuites tests="0" failures="0" errors="0"/>';expected=@('<xml-no-tests>')},
    @{name='healthy';xml='<testsuites><testsuite name="S"><testcase name="Healthy"/></testsuite></testsuites>';expected=@()},
    @{name='failure';xml='<testsuites><testsuite name="S"><testcase name="Failed"><failure/></testcase></testsuite></testsuites>';expected=@('S.Failed')},
    @{name='error';xml='<testsuites><testsuite name="S"><testcase name="Errored"><error/></testcase></testsuite></testsuites>';expected=@('S.Errored')},
    @{name='all-skipped';xml='<testsuites><testsuite name="S"><testcase name="Skipped"><skipped/></testcase></testsuite></testsuites>';expected=@('<xml-no-executed-tests>')}
)
$b23Results=@()
foreach($c in $b23Cases){
    $f=Join-Path $OutDir ($c.name+'.xml')
    if($null -ne $c.xml){[IO.File]::WriteAllText($f,$c.xml)}
    $actual=@(Get-FailedTestNames $f)
    $ok=($actual.Count -eq $c.expected.Count -and (($actual -join '|') -ceq ($c.expected -join '|')))
    $b23Results+=[pscustomobject]@{name=$c.name;passed=$ok;expected=@($c.expected);actual=@($actual)}
    Write-Output ($c.name+': '+$ok+' actual=['+($actual -join ',')+']')
}
$failed=@($b23Results|Where-Object{-not $_.passed}).Count
$r=[pscustomobject]@{expected=7;executed=$b23Results.Count;passed=($b23Results.Count-$failed);failed=$failed;skipped=0;scriptSha256=(Get-FileHash -LiteralPath $Script -Algorithm SHA256).Hash;driverSha256=(Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash;cases=$b23Results}
$r|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $OutDir 'report.json') -Encoding UTF8
if($failed){exit 1}
