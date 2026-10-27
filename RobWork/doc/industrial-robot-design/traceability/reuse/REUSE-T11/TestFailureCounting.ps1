# 验证真实CheckProductionEvidence对gtest XML的计数语义；fixture不代表生产通过。
# gtest failures统计失败用例，不是同一用例内失败断言节点的总数。
param([string]$OutputFile='.cache/t11/failure-counting.json')
$ErrorActionPreference='Stop'
$repo=(git rev-parse --show-toplevel).Trim()
. (Join-Path $repo 'RobWork/scripts/industrialrobot/reuse-contract.ps1')
function Fail($code,$detail) { throw "REUSE-$code : $detail" }
function SafePath($path) { return $path }
function ReadText($path) { return $script:fixture[$path] }
function Json($path) { return ((ReadText $path)|ConvertFrom-Json) }
function Digest($value) {
    $sha=[Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes(($value -replace "`r`n","`n").TrimEnd("`n")+"`n")))).Replace('-','').ToLowerInvariant() } finally { $sha.Dispose() }
}
function Binding($path) { return @{path=$path;sha256=(Digest (ReadText $path))} }
$caps=@{TEST=[pscustomobject]@{review=[pscustomobject]@{status='verified'};upstream=@([pscustomobject]@{path='upstream.hpp';targetFile='upstream.hpp';symbols=@('upstream')})}}
$row=[pscustomobject]@{functionId='fixture';targets=@('test');production=[pscustomobject]@{entry='entry';consumer='entry';target='test';upstream='upstream'};capabilityId='TEST';inputs=@([pscustomobject]@{path='upstream.hpp'})}
$source=[pscustomobject]@{path='source.hpp';symbol='entry'}
$pass='<testsuites><testsuite tests="1" failures="0" errors="0" skipped="0"><testcase/></testsuite></testsuites>'
$one='<testsuites><testsuite tests="1" failures="1" errors="0" skipped="0"><testcase><failure/></testcase></testsuite></testsuites>'
$multiple=$one.Replace('<failure/>','<failure/><failure/>')
$results=@()
foreach($case in @(
    @{name='multiple-assertions-one-failing-case';xml=$multiple;accept=$true},
    @{name='one-failing-case';xml=$one;accept=$true},
    @{name='wrong-failure-count';xml=$one.Replace('failures="1"','failures="2"');accept=$false},
    @{name='empty-case-list';xml=$one.Replace('<testcase><failure/></testcase>','');accept=$false},
    @{name='failing-normal';xml=$one;normal=$one;accept=$false},
    @{name='skipped-normal';xml=$one;normal=$pass.Replace('<testcase/>','<testcase><skipped/></testcase>');accept=$false},
    @{name='error-node';xml=$one.Replace('<failure/>','<error/>');accept=$false},
    @{name='disconnect-has-no-failure';xml=$pass;accept=$false}
)) {
    $script:fixture=@{'source.hpp'='original';'upstream.hpp'='upstream';'mutated.hpp'='changed';'run.log'='fixture';'normal.xml'=$pass;'disconnect.xml'=$case.xml}
    if($case.normal) { $fixture['normal.xml']=$case.normal }
    $run=@{command='fixture';xml=(Binding 'normal.xml');log=(Binding 'run.log');exitCode=0}
    $dis=@{command='fixture';xml=(Binding 'disconnect.xml');log=(Binding 'run.log');exitCode=1;mutation=@{source='source.hpp';originalSha256=(Digest 'original');description='fixture'};mutatedSource=(Binding 'mutated.hpp')}
    $report=@{schemaVersion='ird-reuse-production/1';functionId='fixture';target='test';bindings=@((Binding 'source.hpp'),(Binding 'upstream.hpp'));normal=$run;oracle=$run;upstreamComparison=$run;disconnected=$dis}
    $fixture['report.json']=$report|ConvertTo-Json -Depth 12
    $row|Add-Member -NotePropertyName evidence -NotePropertyValue ([pscustomobject](Binding 'report.json')) -Force
    $accepted=$true;$message=''
    try { CheckProductionEvidence $row @($source) } catch { $accepted=$false;$message=$_.Exception.Message }
    $ok=($accepted -eq $case.accept) -and ($accepted -or $message.StartsWith('REUSE-POSITIVE :'))
    $results+=@{name=$case.name;pass=$ok;accepted=$accepted;message=$message}
}
$results|ConvertTo-Json -Depth 5|Set-Content $OutputFile -Encoding utf8
$failed=@($results|Where-Object { -not $_.pass }).Count
Write-Output "failure-counting: passed=$($results.Count-$failed) failed=$failed skipped=0"
if($failed) { exit 1 }
