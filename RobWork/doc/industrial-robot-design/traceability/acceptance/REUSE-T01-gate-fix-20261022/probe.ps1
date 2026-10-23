param([string]$Root)
$ErrorActionPreference='Stop'
$repo=Join-Path $Root 'repo'
$sandbox=Join-Path $Root 'probe-repo3'
$design='RobWork/doc/industrial-robot-design/'
$reuse=$design+'reuse/'
$product='RobWork/RobWorkStudio/src/rwslibs/industrialrobot/'
$source=$product+'runtime/src/WorkCellCompiler.cpp'
$matrixPath=$reuse+'functions/runtime.json'
$manifestPath=$reuse+'changes/REUSE-T99.json'
$taskPath=$design+'tasks/REUSE-T99.json'
$proposalPath=$reuse+'acceptance-proposal.txt'
& git clone --shared --quiet --no-checkout $repo $sandbox
if($LASTEXITCODE){throw 'probe clone failed'}
& git -C $sandbox config core.longpaths true
& git -C $sandbox sparse-checkout init --cone
& git -C $sandbox sparse-checkout set 'RobWork/RobWork/src' 'RobWork/RobWorkSim/src' $product ($design+'reuse') ($design+'tasks') ($design+'traceability/acceptance') 'RobWork/scripts/industrialrobot'
& git -C $sandbox checkout --quiet --detach b4c03401e12400a095cc4639f9f9ecefff0a9dac
if($LASTEXITCODE){throw 'probe checkout failed'}
$original=[IO.File]::ReadAllBytes((Join-Path $sandbox $source))
$matrixOriginal=$null; [IO.Directory]::CreateDirectory((Split-Path -Parent (Join-Path $sandbox $matrixPath))) | Out-Null; if(Test-Path -LiteralPath (Join-Path $sandbox $matrixPath)){$matrixOriginal=[IO.File]::ReadAllBytes((Join-Path $sandbox $matrixPath))}
function Hash([string]$text){$sha=[Security.Cryptography.SHA256]::Create();try{return ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes(($text.TrimStart([char]0xFEFF) -replace "`r`n","`n").TrimEnd("`n")+"`n")))).Replace('-','').ToLowerInvariant()}finally{$sha.Dispose()}}
function Put([string]$path,[string]$text){[IO.Directory]::CreateDirectory((Split-Path -Parent (Join-Path $sandbox $path)))|Out-Null;[IO.File]::WriteAllText((Join-Path $sandbox $path),$text,[Text.UTF8Encoding]::new($false))}
function PutJson([string]$path,$object){Put $path ($object|ConvertTo-Json -Depth 30)}
$accept=Get-Content (Join-Path $sandbox ($design+'tasks/REUSE-T01.json')) -Raw|ConvertFrom-Json
$results=@()
function Probe([string]$name,[string]$decision,[string]$status,[string]$kind,[string]$expected,[switch]$Final,[switch]$Frozen){
    $path=$source;$text=[Text.Encoding]::UTF8.GetString($original)
    if($kind -eq 'changed'){$text+='// independent acceptance modification'+"`n";Put $source $text}
    if($kind -eq 'new'){$path=$product+'runtime/src/AcceptanceProbe.cpp';$text='#include <rw/models/WorkCell.hpp>'+"`n"+'void acceptanceProbe() {}';Put $path $text}
    $symbol='compileWorkCell';if($kind -eq 'new'){$symbol='acceptanceProbe'}
    $build=$product+'runtime/CMakeLists.txt';$buildText=[IO.File]::ReadAllText((Join-Path $sandbox $build))
    $cap='NONE';if($decision -eq 'adapter'){$cap='MATH-ROT'}
    $row=[ordered]@{functionId='acceptance.runtime.restricted';ownerUnit='runtime';requirements=@('MDL-22');sources=@([ordered]@{path=$path;symbol=$symbol;startLine=1;endLine=@(($text -replace "`r`n","`n") -split "`n").Count;sha256=(Hash $text)});targets=@('sdurws_ird_runtime');usageRole='owner';permissionRef='ARCH:3.6.1';capabilityId=$cap;decision=$decision;status=$status;search=@{command='independent acceptance probe';conclusion='synthetic acceptance probe only'};differences='unresolved probe';inputs=@(@{path=$build;sha256=(Hash $buildText)})}
    if($decision -eq 'proposal'){Put $proposalPath 'synthetic unresolved proposal';$row.proposal=@{path=$proposalPath;sha256=(Hash 'synthetic unresolved proposal')}}
    PutJson $matrixPath @{schemaVersion='ird-reuse-functions/1';unit='runtime';functions=@($row)}
    if($kind -ne 'baseline'){
        PutJson $taskPath @{taskId='REUSE-T99';status='ready';allowedFiles=@($path);forbiddenFiles=@();governanceAcceptance=$accept.governanceAcceptance;reuseGateAcceptance=$accept.reuseGateAcceptance;reuse=@{policy='H1-H6';manifest=$manifestPath}}
        PutJson $manifestPath @{schemaVersion='ird-reuse-change/1';taskId='REUSE-T99';taskFile=$taskPath;files=@(@{path=$path;sha256=(Hash $text);mode='business';boundary='independent synthetic acceptance probe';verify='acceptance only';capabilities=@()})}
    }
    $extra=@();if($Final){$extra+='-RequireMigrationComplete'}
    if($Frozen){
        & git -C $sandbox add -- $matrixPath $path $taskPath $manifestPath
        & git -C $sandbox -c user.name=IndependentAcceptance -c user.email=acceptance@example.invalid commit --quiet -m 'synthetic acceptance probe only'
        if($LASTEXITCODE){throw 'probe commit failed'}
        $extra+=@('-Head','HEAD')
    }
    $saved=$ErrorActionPreference
    try{$ErrorActionPreference='Continue';$output=(& pwsh -NoProfile -File (Join-Path $sandbox 'RobWork/scripts/industrialrobot/validate-reuse.ps1') -RepoRoot $sandbox @extra 2>&1|Out-String -Width 32767);$code=$LASTEXITCODE}finally{$ErrorActionPreference=$saved}
    $ok=if($expected -eq 'PASS'){($code -eq 0 -and $output.Contains('REUSE-AD-PENDING'))}else{($code -ne 0 -and $output.Contains($expected) -and -not $output.Contains('REUSE-AD-PENDING'))}
    $script:results += [pscustomobject]@{name=$name;expected=$expected;exitCode=$code;passed=$ok;output=$output}
    Set-Content -LiteralPath (Join-Path $Root ($name+'.log')) -Value $output -Encoding UTF8
    Write-Output "$name passed=$ok exit=$code expected=$expected"
    if($Frozen){& git -C $sandbox reset --hard --quiet b4c03401e12400a095cc4639f9f9ecefff0a9dac}
    [IO.File]::WriteAllBytes((Join-Path $sandbox $source),$original)
    if($null -eq $matrixOriginal){if(Test-Path -LiteralPath (Join-Path $sandbox $matrixPath)){Remove-Item -LiteralPath (Join-Path $sandbox $matrixPath)}}else{[IO.File]::WriteAllBytes((Join-Path $sandbox $matrixPath),$matrixOriginal)}
    foreach($p in @($manifestPath,$taskPath,$proposalPath,($product+'runtime/src/AcceptanceProbe.cpp'))){if(Test-Path -LiteralPath (Join-Path $sandbox $p)){Remove-Item -LiteralPath (Join-Path $sandbox $p)}}
}
Probe 'probe-pending-pending' pending pending baseline PASS
Probe 'probe-pending-proposal' pending proposal baseline PASS
Probe 'probe-proposal-pending' proposal pending baseline PASS
Probe 'probe-proposal-proposal' proposal proposal baseline PASS
Probe 'probe-status-audited' pending audited baseline REUSE-AD
Probe 'probe-decision-resolved' adapter pending baseline REUSE-AD
Probe 'probe-business-resolved' business-specific proposal baseline REUSE-AD
Probe 'probe-proposal-audited' proposal audited baseline REUSE-AD
Probe 'probe-changed-baseline' pending pending changed REUSE-AD
Probe 'probe-new-source' pending pending new REUSE-AD
Probe 'probe-final-pending' pending pending baseline REUSE-MIGRATION -Final
Probe 'probe-final-proposal' proposal proposal baseline REUSE-MIGRATION -Final
Probe 'probe-frozen-changed' pending pending changed REUSE-AD -Frozen
Probe 'probe-frozen-new' pending pending new REUSE-AD -Frozen
$report=[ordered]@{reviewedCommit='b4c03401e12400a095cc4639f9f9ecefff0a9dac';executed=$results.Count;passed=@($results|Where-Object passed).Count;failed=@($results|Where-Object {-not $_.passed}).Count;skipped=0;notRun=0;tests=$results}
$report|ConvertTo-Json -Depth 20|Set-Content (Join-Path $Root 'independent-probes.json') -Encoding UTF8
if($report.failed){exit 1}
