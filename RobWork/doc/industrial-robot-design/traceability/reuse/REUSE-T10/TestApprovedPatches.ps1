# REUSE-T10：加载真实validator/helper，在隔离Git仓库核验批准补丁，不复制执法实现。
param([string]$OutputDir='')
$ErrorActionPreference='Stop'
$sourceRoot=(& git -C $PSScriptRoot rev-parse --show-toplevel).Trim()
if (-not $OutputDir) { $OutputDir=Join-Path $sourceRoot '.cache/t10-patch-tests' }
[void](New-Item -ItemType Directory -Force -Path $OutputDir)
$OutputDir=[IO.Path]::GetFullPath($OutputDir)
$fixture=Join-Path $OutputDir ('fixture-'+[guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $fixture)
$source='RobWork/RobWork/src/rw/math/Test.cpp'
$patchPath='RobWork/RobWorkStudio/src/rwslibs/industrialrobot/patches/0004-test.patch'
$registryPath='RobWork/doc/industrial-robot-design/reuse/upstream-patches.json'
$approvalPath='approval.json'; $proposalPath='proposal.md'
function Put([string]$p,[string]$text) {
    $full=Join-Path $fixture $p
    [void](New-Item -ItemType Directory -Force -Path (Split-Path $full))
    [IO.File]::WriteAllText($full,$text,[Text.UTF8Encoding]::new($false))
}
Put $source "int baseline = 1;`n"
& git -C $fixture init -q
& git -C $fixture config user.name 'Isolated gate fixture'
& git -C $fixture config user.email 'fixture@example.invalid'
& git -C $fixture config core.autocrlf false
& git -C $fixture add .
& git -C $fixture commit -qm baseline
$fixtureBaseline=(& git -C $fixture rev-parse HEAD).Trim()
$validator=[IO.File]::ReadAllText((Join-Path $sourceRoot 'RobWork/scripts/industrialrobot/validate-reuse.ps1'))
$cut=$validator.IndexOf("`ntry {`n    if (`$Head)")
if ($cut -lt 0) { $cut=$validator.IndexOf("`r`ntry {`r`n    if (`$Head)") }
if ($cut -lt 0) { throw 'cannot locate real validator definition preamble' }
$prefix=$validator.Substring(0,$cut).Replace(". (Join-Path `$PSScriptRoot 'reuse-contract.ps1')", ". (Join-Path `$sourceRoot 'RobWork/scripts/industrialrobot/reuse-contract.ps1')")
. ([scriptblock]::Create($prefix)) -RepoRoot $fixture
$baseline=$fixtureBaseline
$goodSource="int baseline = 2;`n"
Put $source $goodSource
Put $proposalPath "owner-reviewed precise proposal`n"
Put ($product+'patches/PATCHES.md') "0004-test.patch`n"
$goodPatch=((GitLines @('diff','--no-ext-diff','--no-color','--no-renames','--full-index',$baseline,'--',$source)) -join "`n")+"`n"
Put $patchPath $goodPatch
$goodApproval=@{taskId='REUSE-T10';ownerQuote='所有者明确批准该精确扩展';date='2026-10-23';proposal=$proposalPath;proposalSha256=(Digest (ReadText $proposalPath));allowedFiles=@($source)} | ConvertTo-Json -Depth 10
Put $approvalPath $goodApproval
$goodRegistry=@{schemaVersion='ird-approved-upstream-patches/1';patches=@(@{
 taskId='REUSE-T10';proposal=@{path=$proposalPath;sha256=(Digest (ReadText $proposalPath))};
 approval=@{path=$approvalPath;sha256=(Digest $goodApproval)};
 patch=@{path=$patchPath;sha256=(Digest $goodPatch)};
 files=@(@{path=$source;baselineBlob=(@(GitLines @('rev-parse',"${baseline}:$source"))[0]);sha256=(Digest $goodSource)})
})} | ConvertTo-Json -Depth 15
function Restore-Fixture {
 $script:Head=''
 Put $source $goodSource;Put $patchPath $goodPatch;Put $approvalPath $goodApproval
 Put $proposalPath "owner-reviewed precise proposal`n"
 Put ($product+'patches/PATCHES.md') "0004-test.patch`n"
 Put $registryPath $goodRegistry
}
function Save-Registry($r) { Put $registryPath ($r|ConvertTo-Json -Depth 20) }
function Rebind-Approval($a,$r) {
 $v=$a|ConvertTo-Json -Depth 15;Put $approvalPath $v;$r.patches[0].approval.sha256=Digest $v;Save-Registry $r
}
$cases=@(
 @{name='exact approval and real diff';reject=$false;change={}},
 @{name='stale source';reject=$true;change={Put $source "int hidden = 3;`n"}},
 @{name='stale patch';reject=$true;change={Put $patchPath ($goodPatch+"extra`n")}},
 @{name='rehashed forged patch';reject=$true;change={ $r=$goodRegistry|ConvertFrom-Json;Put $patchPath ($goodPatch.Replace('baseline = 2','baseline = 9'));$r.patches[0].patch.sha256=Digest (ReadText $patchPath);Save-Registry $r }},
 @{name='wrong baseline';reject=$true;change={ $r=$goodRegistry|ConvertFrom-Json;$r.patches[0].files[0].baselineBlob='0'*40;Save-Registry $r }},
 @{name='missing registration';reject=$true;change={Put ($product+'patches/PATCHES.md') "no patch`n"}},
 @{name='empty owner quote';reject=$true;change={ $r=$goodRegistry|ConvertFrom-Json;$a=$goodApproval|ConvertFrom-Json;$a.ownerQuote='';Rebind-Approval $a $r }},
 @{name='wrong approval scope';reject=$true;change={ $r=$goodRegistry|ConvertFrom-Json;$a=$goodApproval|ConvertFrom-Json;$a.allowedFiles=@('RobWork/RobWork/src/rw/math/Other.cpp');Rebind-Approval $a $r }},
 @{name='stale proposal';reject=$true;change={Put $proposalPath "different proposal`n"}},
 @{name='duplicate record';reject=$true;change={ $r=$goodRegistry|ConvertFrom-Json;$r.patches+=@($r.patches[0]);Save-Registry $r }},
 @{name='unregistered file receives no grant';reject=$false;change={}},
 @{name='missing registry grants nothing';reject=$false;change={Remove-Item -LiteralPath (Join-Path $fixture $registryPath)}}
)
$results=@()
foreach($case in $cases) {
 Restore-Fixture
 & $case.change
 $rejected=$false;$detail=''
 try {
  $result=CheckApprovedUpstreamPatches
  if ($case.name -eq 'unregistered file receives no grant' -and $result.files.ContainsKey('RobWork/RobWork/src/rw/math/Other.cpp')) { throw 'unexpected other file grant' }
  if ($case.name -eq 'missing registry grants nothing' -and ($result.files.Count -or $result.assets.Count)) { throw 'unexpected grant without registry' }
 } catch { $rejected=$true;$detail=$_.Exception.Message }
 $pass=($rejected -eq $case.reject)
 $results+=@{name=$case.name;passed=$pass;expectedRejection=$case.reject;actualRejection=$rejected;detail=$detail}
 Write-Output "$pass : $($case.name) : $detail"
}
# Head模式必须读取冻结树；当前工作区篡改不能污染已冻结证据，也不能替其背书。
Restore-Fixture
& git -C $fixture add .
& git -C $fixture commit -qm approved
$Head=(& git -C $fixture rev-parse HEAD).Trim()
Put $source "int worktree_only = 999;`n"
$pass=$true;$detail=''
try { $null=CheckApprovedUpstreamPatches } catch { $pass=$false;$detail=$_.Exception.Message }
$results+=@{name='frozen Head ignores current worktree';passed=$pass;detail=$detail}
$report=@{schemaVersion='ird-gate-test/1';tests=$results;passed=@($results|Where-Object passed).Count;failed=@($results|Where-Object {-not $_.passed}).Count;skipped=0;notRun=0;fixture=$fixture;validatorSha256=(Get-FileHash (Join-Path $sourceRoot 'RobWork/scripts/industrialrobot/validate-reuse.ps1')).Hash;helperSha256=(Get-FileHash (Join-Path $sourceRoot 'RobWork/scripts/industrialrobot/reuse-contract.ps1')).Hash}
[IO.File]::WriteAllText((Join-Path $OutputDir 'report.json'),($report|ConvertTo-Json -Depth 20),[Text.UTF8Encoding]::new($false))
Write-Output "$($report.passed) passed; $($report.failed) failed; 0 skipped; 0 notRun"
if($report.failed){exit 1}
