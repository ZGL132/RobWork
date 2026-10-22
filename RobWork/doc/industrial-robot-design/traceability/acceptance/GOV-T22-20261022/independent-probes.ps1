param([string]$RepoRoot, [string]$OutputDir)
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$OutputEncoding = [Console]::OutputEncoding
$sha='0d018a0ba9d1695eef16442f76e9d325276328e7'
if ((& git -C $RepoRoot rev-parse HEAD).Trim() -ne $sha) { throw 'Run probes before committing acceptance records.' }
$validator=Join-Path $RepoRoot 'RobWork/scripts/industrialrobot/validate-reuse.ps1'
$helper=Join-Path $RepoRoot 'RobWork/scripts/industrialrobot/reuse-contract.ps1'
$tokens=$null; $errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($validator,[ref]$tokens,[ref]$errors)
if ($errors.Count) { throw 'Frozen validator failed parse.' }
# 原封不动加载被验函数，避免执行完整入口后退出；不修改实现。
foreach ($f in $ast.FindAll({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -in @('Fail','SafePath','ReadText','Digest')},$false)) {
    . ([scriptblock]::Create($f.Extent.Text))
}
. $helper
$results=@()
$path='RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/src/CommandPalette.cpp'
$lines=[IO.File]::ReadAllLines((Join-Path $RepoRoot $path))
$fragment=$lines[187..188] -join "`n"
foreach ($variant in @('actual-qt','same-qt-without-setData')) {
    $source=$fragment
    if ($variant -eq 'same-qt-without-setData') { $source=$lines[187] }
    $actual='allow';$diagnostic=''
    try { CheckSourcePermission $path $source @{} } catch { $actual='reject';$diagnostic=$_.Exception.Message }
    $results += [ordered]@{name=$variant;expected='allow';actual=$actual;passed=($actual -eq 'allow');path=$path;lines='188-189';source=$source;diagnostic=$diagnostic}
}
# 即便提供仅查询授权，Qt setData 仍被误当成必须额外授权的 State 写入。
$grant=@{};$grant[$path]=[pscustomobject]@{categories=@();operations=@('query')}
$actual='allow';$diagnostic=''
try { CheckSourcePermission $path $fragment $grant } catch { $actual='reject';$diagnostic=$_.Exception.Message }
$results += [ordered]@{name='actual-qt-query-grant';expected='allow';actual=$actual;passed=($actual -eq 'allow');diagnostic=$diagnostic}
$actual='allow';$diagnostic=''
try { CheckSourcePermission 'probe.cpp' 'void probe(){ device->setQ(q, sharedState); }' @{} } catch { $actual='reject';$diagnostic=$_.Exception.Message }
$results += [ordered]@{name='shared-state-rejection-control';expected='reject';actual=$actual;passed=($actual -eq 'reject');diagnostic=$diagnostic}
# ReadAllText 自动剥 BOM，冻结 Git 字符流保留 BOM；直接运行冻结实现的 ReadText/Digest 对比。
$path='RobWork/scripts/industrialrobot/validate-reuse.ps1'
$Head='';$working=ReadText $path;$workingHash=Digest $working
$Head=$sha;$frozen=ReadText $path;$frozenHash=Digest $frozen
$results += [ordered]@{name='bom-working-vs-frozen';expected='same normalized digest';actual=($workingHash -eq $frozenHash);passed=($workingHash -eq $frozenHash);path=$path;workingFirstCodePoint=[int]$working[0];frozenFirstCodePoint=[int]$frozen[0];workingHash=$workingHash;frozenHash=$frozenHash;note='同一个未修改且已追踪的BOM UTF-8文件；产品.ps1同样经过这些函数。'}
$report=[ordered]@{schemaVersion='ird-independent-probes/1';reviewedCommit=$sha;runtime=$PSVersionTable.PSVersion.ToString();validatorSha256=(Get-FileHash $validator).Hash;helperSha256=(Get-FileHash $helper).Hash;passed=@($results | Where-Object {$_.passed}).Count;failed=@($results | Where-Object {-not $_.passed}).Count;skipped=0;notRun=0;tests=$results}
$report | ConvertTo-Json -Depth 15 | Set-Content -LiteralPath (Join-Path $OutputDir 'independent-probes.json') -Encoding UTF8
$report | ConvertTo-Json -Depth 15
if ($report.failed) { exit 1 }
