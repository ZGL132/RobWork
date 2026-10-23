# 在共享对象的隔离 Git 检出中执行真实门禁；核对具名错误，不修改调用方工作区。
param([string]$RepoRoot = '', [string]$OutputDir = '')
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$OutputEncoding = [Console]::OutputEncoding
if (-not $RepoRoot) { $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path }
if (-not $OutputDir) { $OutputDir = Join-Path $RepoRoot '.cache/reuse-tests' }
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
[IO.Directory]::CreateDirectory($OutputDir) | Out-Null
$sandbox = Join-Path ([IO.Path]::GetTempPath()) ('ird-reuse-'+[guid]::NewGuid().ToString('N'))
$ps = (Get-Process -Id $PID).Path
$validatorSource = Join-Path $PSScriptRoot 'validate-reuse.ps1'
$validator = Join-Path $sandbox '.reuse-validator.ps1'
$design = 'RobWork/doc/industrial-robot-design/'
$reuse = $design+'reuse/'
$product = 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/'
$script:results = @(); $script:restore = @{}
$driverFailure = ''; $expectedTests = 20
$driverSha256=(Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash
$copiedValidatorSha256='';$copiedHelperSha256=''
# 首次写入记录原字节；每例结束恢复，避免负例相互污染。
function Put([string]$path,[string]$text) {
    $full = Join-Path $sandbox $path
    if (-not $script:restore.ContainsKey($full)) {
        $old = $null; if (Test-Path -LiteralPath $full) { $old = [IO.File]::ReadAllBytes($full) }
        $script:restore[$full] = $old
    }
    [IO.Directory]::CreateDirectory((Split-Path -Parent $full)) | Out-Null
    [IO.File]::WriteAllText($full,$text,[Text.UTF8Encoding]::new($false))
}
function GetJson([string]$p) { return ([IO.File]::ReadAllText((Join-Path $sandbox $p)) | ConvertFrom-Json) }
function PutJson([string]$p,$j) { Put $p ($j | ConvertTo-Json -Depth 30) }
function Hash([string]$s) {
    $sha=[Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes(($s.TrimStart([char]0xFEFF) -replace "`r`n","`n").TrimEnd("`n")+"`n")))).Replace('-','').ToLowerInvariant() } finally { $sha.Dispose() }
}
function Declare([string]$name,[string]$source,[string]$mode='business') {
    $p=$product+'kinematics/src/'+$name+'.cpp'; Put $p $source
    $m=GetJson ($reuse+'changes/GOV-T21.json')
    $m.files += [pscustomobject]@{path=$p;sha256=(Hash $source);mode=$mode;boundary='fixture only';verify='fixture';capabilities=@('KIN-FK')}
    PutJson ($reuse+'changes/GOV-T21.json') $m
    $t=GetJson ($design+'tasks/GOV-T21.json'); $t.allowedFiles += $p; $t.forbiddenFiles=@(); PutJson ($design+'tasks/GOV-T21.json') $t
}
# 代表性“待审计”功能样本只存在于隔离检出，不将既有手写数学误标为已迁移。
function MathMatrix {
    $path=$product+'runtime/src/BaseWorldTransform.cpp'
    $text=[IO.File]::ReadAllText((Join-Path $sandbox $path))
    $buildPath=$product+'runtime/CMakeLists.txt'
    $row=[pscustomobject]@{
        functionId='fixture.runtime.rotation';ownerUnit='runtime';requirements=@('MDL-22');
        sources=@([pscustomobject]@{path=$path;symbol='multiplyRotation';startLine=1;endLine=@(($text -replace "`r`n","`n") -split "`n").Count;sha256=(Hash $text)});
        targets=@('sdurws_ird_runtime');usageRole='adapter';permissionRef='ARCH:3.6.1';capabilityId='MATH-ROT';decision='pending';status='pending';
        search=[pscustomobject]@{command='rg fixture';conclusion='隔离门禁样本；不是生产适用性证据'};differences='参考系/确定性待审';
        inputs=@([pscustomobject]@{path=$buildPath;sha256=(Hash ([IO.File]::ReadAllText((Join-Path $sandbox $buildPath))))})
    }
    return [pscustomobject]@{schemaVersion='ird-reuse-functions/1';unit='runtime';functions=@($row)}
}
function PutMath($m) { PutJson ($reuse+'functions/runtime.json') $m }
# 治理脚本的执行关系不伪装成CMake目标；仅在隔离副本写入代表性审计行。
# 这不是脚本语义验收，反例验证该标记不能放宽产品目录或权限红线。
function ScriptMatrix {
    $path='RobWork/scripts/industrialrobot/commit-lint.ps1'
    $text='# synthetic governance audit fixture only';Put $path $text
    $r=[pscustomobject]@{
        functionId='fixture.governance.entry';ownerUnit='foundation-governance';requirements=@('H6');
        sources=@([pscustomobject]@{path=$path;symbol='audit';startLine=1;endLine=1;sha256=(Hash $text)});
        targets=@();usageRole='owner';permissionRef='ARCH:3.6.4';capabilityId='NONE';decision='pending';status='pending';
        search=[pscustomobject]@{command='rg fixture';conclusion='仅治理执行关系格式夹具，不冒充语义审计'};
        differences='无CMake编译target';inputs=@([pscustomobject]@{path=$path;sha256=(Hash $text)});
        execution=[pscustomobject]@{kind='governance-script';entry=$path;command=('pwsh -NoProfile -File '+$path)}
    }
    return [pscustomobject]@{schemaVersion='ird-reuse-functions/1';unit='foundation-governance';functions=@($r)}
}
function PutScriptMatrix($m) { PutJson ($reuse+'functions/foundation-governance.json') $m }
function IsolateFinalClosureMatrices {
    # 这两项专测“新增文件缺独立关闭记录”，不能被真实审计中的 pending 抢先截断。
    # 仅清空本 Case 的隔离副本；PutJson 保留原字节并在 finally 恢复。
    # 01-clean 与 22-final-debt-rejected 仍使用完整真实矩阵，不豁免生产债务。
    foreach ($file in Get-ChildItem -LiteralPath (Join-Path $sandbox ($reuse+'functions')) -Filter '*.json' -File) {
        $path=$reuse+'functions/'+$file.Name;$m=GetJson $path;$m.functions=@();PutJson $path $m
    }
}
function RestrictedAuditMatrix {
    # 使用真实未改动的 owner 编译器记录待审事实，不能要求先伪造授权才能登记债务。
    $m=MathMatrix;$r=$m.functions[0];$path=$product+'runtime/src/WorkCellCompiler.cpp'
    $text=[IO.File]::ReadAllText((Join-Path $sandbox $path))
    $r.functionId='fixture.runtime.compiler';$r.capabilityId='NONE';$r.usageRole='owner'
    $r.sources=@([pscustomobject]@{path=$path;symbol='compileWorkCell';startLine=1;endLine=@($text -split "`n").Count;sha256=(Hash $text)})
    return $m
}
function Grant([string]$origin='runtime-borrow',[string[]]$operations=@('query'),[string]$target='sdurws_ird_kinematics') {
    $path=$product+'kinematics/src/Fk.cpp'; $build=$product+'kinematics/CMakeLists.txt'
    return [pscustomobject]@{id='fixture-grant';source=[pscustomobject]@{path=$path;sha256=(Hash ([IO.File]::ReadAllText((Join-Path $sandbox $path))))};
        targets=@($target);categories=@('model','state');operations=$operations;origin=$origin;lifetime='任务样本，无生产授权';permissionRef='ARCH:3.6.1';usageRole='adapter';
        build=[pscustomobject]@{path=$build;sha256=(Hash ([IO.File]::ReadAllText((Join-Path $sandbox $build))))}}
}
function FunctionalChange([string]$source,[string]$suffix='cpp') {
    # 验收对象仅为隔离夹具；接受记录不离开 sandbox，不能用它放行真实生产代码。
    $path=$product+'kinematics/src/PermissionProbe.'+$suffix;$script:probePath=$path;Put $path $source
    $accept=[pscustomobject]@{commit=$script:fixtureAcceptanceCommit;path=($design+'traceability/acceptance/GOV-T22-fixture.json');reviewedCommit=$script:fixtureReviewedCommit}
    $t=[pscustomobject]@{taskId='KIN-T99';status='ready';allowedFiles=@($path);forbiddenFiles=@();governanceAcceptance=(GetJson ($design+'tasks/GOV-T22.json')).governanceAcceptance;reuseGateAcceptance=$accept;
        reuse=[pscustomobject]@{policy='H1-H6';manifest=($reuse+'changes/KIN-T99.json')}}
    PutJson ($design+'tasks/KIN-T99.json') $t
    PutJson ($reuse+'changes/KIN-T99.json') ([pscustomobject]@{schemaVersion='ird-reuse-change/1';taskId='KIN-T99';taskFile=($design+'tasks/KIN-T99.json');files=@([pscustomobject]@{path=$path;sha256=(Hash $source);mode='business';boundary='隔离权限夹具';verify='fixture';capabilities=@()})})
}
function ProbeMatrix([string]$decision='business-specific',[string]$status='migrated') {
    $path=$script:probePath;$m=MathMatrix;$r=$m.functions[0]
    $txt=[IO.File]::ReadAllText((Join-Path $sandbox $path))
    $r.functionId='fixture.kinematics.probe';$r.ownerUnit='kinematics';$r.sources=@([pscustomobject]@{path=$path;symbol='probe';startLine=1;endLine=@($txt -split "`n").Count;sha256=(Hash $txt)})
    $r.targets=@('sdurws_ird_kinematics');$r.capabilityId='NONE';$r.decision=$decision;$r.status=$status
    $p=$product+'kinematics/CMakeLists.txt';$r.inputs=@([pscustomobject]@{path=$p;sha256=(Hash ([IO.File]::ReadAllText((Join-Path $sandbox $p))))})
    $r | Add-Member nonOverlapEvidence ([pscustomobject]@{path=($reuse+'probe-review.txt');sha256=(Hash '隔离结构夹具：空函数或值载体，无核心算法')})
    Put ($reuse+'probe-review.txt') '隔离结构夹具：空函数或值载体，无核心算法'
    $m.unit='kinematics';PutJson ($reuse+'functions/kinematics.json') $m
}
function ProbeGrant([string]$origin,[string[]]$categories,[string[]]$ops) {
    $g=Grant $origin $ops;$g.source.path=$script:probePath;$g.source.sha256=Hash ([IO.File]::ReadAllText((Join-Path $sandbox $g.source.path)));$g.categories=$categories
    $p=GetJson ($reuse+'permissions.json');$p.grants=@($g);PutJson ($reuse+'permissions.json') $p
}
function WriteProduction($fixture) {
    PutJson $fixture.path $fixture.report
    $fixture.matrix.functions[0].evidence.sha256=Hash ([IO.File]::ReadAllText((Join-Path $sandbox $fixture.path)))
    PutMath $fixture.matrix
}
function ProductionFixture([switch]$Replacement) {
    # 仅测试证据解析器的结构夹具，XML 为显式合成数据，不是产品通过/数值正确性证据。
    # 真实迁移仍须编译生产目标、执行断开实验，并经冻结 SHA 独立语义验收。
    $m=MathMatrix;$r=$m.functions[0];$r.decision='direct';$r.status='migrated'
    if ($Replacement) {
        $old=$r.sources[0].path;$new=$product+'runtime/src/RotationAdapterProbe.cpp'
        $oldSource=[IO.File]::ReadAllText((Join-Path $sandbox $old));$source='void multiplyRotation(){} // synthetic closure parser fixture only';Put $new $source
        Put $old $oldSource;Remove-Item -LiteralPath (Join-Path $sandbox $old)
        $r.sources[0].path=$new;$r.sources[0].startLine=1;$r.sources[0].endLine=1;$r.sources[0].sha256=Hash $source;$r | Add-Member replaces @($old)
        $t=[pscustomobject]@{taskId='REUSE-T99';status='ready';allowedFiles=@($old,$new);forbiddenFiles=@();governanceAcceptance=(GetJson ($design+'tasks/GOV-T22.json')).governanceAcceptance;
            reuseGateAcceptance=[pscustomobject]@{commit=$script:fixtureAcceptanceCommit;path=($design+'traceability/acceptance/GOV-T22-fixture.json');reviewedCommit=$script:fixtureReviewedCommit};
            reuse=[pscustomobject]@{policy='H1-H6';manifest=($reuse+'changes/REUSE-T99.json')}}
        PutJson ($design+'tasks/REUSE-T99.json') $t
        PutJson ($reuse+'changes/REUSE-T99.json') ([pscustomobject]@{schemaVersion='ird-reuse-change/1';taskId='REUSE-T99';taskFile=($design+'tasks/REUSE-T99.json');files=@(
            [pscustomobject]@{path=$old;sha256='deleted';mode='remove';boundary='isolated parser fixture';verify='fixture';capabilities=@()},
            [pscustomobject]@{path=$new;sha256=(Hash $source);mode='direct';boundary='isolated parser fixture';verify='fixture';capabilities=@('MATH-ROT')})})
    }
    $r | Add-Member production ([pscustomobject]@{entry='multiplyRotation';consumer='multiplyRotation';upstream='toRotation3D';target='sdurws_ird_runtime'})
    $e=$reuse+'fixture-production.json';$r | Add-Member evidence ([pscustomobject]@{path=$e;sha256='pending'})
    $cat=GetJson ($reuse+'catalog.json');$cap=@($cat.capabilities | Where-Object { $_.id -eq 'MATH-ROT' })[0]
    Put ($reuse+'fixture-suitability.txt') 'parser fixture only'
    $cap.review.status='verified';$cap.review | Add-Member evidence ([pscustomobject]@{path=($reuse+'fixture-suitability.txt');sha256=(Hash 'parser fixture only')});PutJson ($reuse+'catalog.json') $cat
    $bindings=@();foreach ($p in @($r.sources.path)+@($r.inputs.path)+@($cap.upstream | ForEach-Object { $_.path; $_.targetFile })) { $bindings += [pscustomobject]@{path=$p;sha256=(Hash ([IO.File]::ReadAllText((Join-Path $sandbox $p))))} }
    $report=[pscustomobject]@{schemaVersion='ird-reuse-production/1';functionId=$r.functionId;target=$r.production.target;bindings=$bindings}
    foreach ($kind in @('normal','disconnected','oracle','upstreamComparison')) {
        $fail=0;$child='';$exit=0;if ($kind -eq 'disconnected') { $fail=1;$child='<failure message="isolated fixture assertion"/>';$exit=1 }
        $xp=$reuse+'fixture-'+$kind+'.xml';$lp=$reuse+'fixture-'+$kind+'.log'
        $xml='<testsuites><testsuite tests="1" failures="'+$fail+'" errors="0" skipped="0"><testcase name="parser-fixture">'+$child+'</testcase></testsuite></testsuites>'
        Put $xp $xml;Put $lp 'synthetic parser fixture; not a product test'
        $run=[pscustomobject]@{command='synthetic parser fixture only';xml=[pscustomobject]@{path=$xp;sha256=(Hash $xml)};log=[pscustomobject]@{path=$lp;sha256=(Hash 'synthetic parser fixture; not a product test')};exitCode=$exit}
        if ($kind -eq 'disconnected') {
            $original=[IO.File]::ReadAllText((Join-Path $sandbox $r.sources[0].path));$mutated='// isolated parser mutation'+"`n"+$original
            Put ($reuse+'fixture-mutated.cpp') $mutated
            $run | Add-Member mutation ([pscustomobject]@{source=$r.sources[0].path;originalSha256=(Hash $original);description='synthetic mutation binding only'})
            $run | Add-Member mutatedSource ([pscustomobject]@{path=($reuse+'fixture-mutated.cpp');sha256=(Hash $mutated)})
        }
        $report | Add-Member $kind $run
    }
    $fixture=[pscustomobject]@{matrix=$m;report=$report;path=$e};WriteProduction $fixture;return $fixture
}
function ClosureFixture([switch]$Replacement, [switch]$BomLog) {
    $script:caseCommit=(& git -C $sandbox rev-parse HEAD).Trim()
    $f=ProductionFixture -Replacement:$Replacement
    # 日志属于已验输入；真实 BOM 必须在 acceptedPaths 比较中与工作树保持同一摘要。
    if ($BomLog) { $p=$f.report.normal.log.path;Put $p ([char]0xFEFF+[IO.File]::ReadAllText((Join-Path $sandbox $p))) }
    # 报告 .log/.xml 可能匹配仓库忽略规则；这些显式合成夹具也必须进入被验 Git 对象。
    & git -C $sandbox add -f -- $reuse ($design+'tasks/GOV-T22.json')
    if ($Replacement) { & git -C $sandbox add -- ($product+'runtime/src/BaseWorldTransform.cpp') ($product+'runtime/src/RotationAdapterProbe.cpp') ($design+'tasks/REUSE-T99.json') }
    & git -C $sandbox -c user.name=ReuseFixture -c user.email=fixture@example.invalid commit --quiet -m 'isolated closure candidate'
    if ($LASTEXITCODE) { throw 'closure fixture candidate failed' }
    $reviewed=(& git -C $sandbox rev-parse HEAD).Trim()
    $recordPath=$design+'traceability/acceptance/REUSE-T99-fixture.json'
    PutJson $recordPath ([pscustomobject]@{taskId='REUSE-T99';verdict='pass';reviewedCommit=$reviewed;scope='synthetic parser fixture only'})
    & git -C $sandbox add -- $recordPath
    & git -C $sandbox -c user.name=ReuseFixture -c user.email=fixture@example.invalid commit --quiet -m 'isolated closure record'
    if ($LASTEXITCODE) { throw 'closure fixture acceptance failed' }
    $recordCommit=(& git -C $sandbox rev-parse HEAD).Trim()
    $path=$product+'runtime/src/BaseWorldTransform.cpp';$blob=(& git -C $sandbox rev-parse ('b12e8503cc1c2a1acc040f506842a02e126b9515:'+$path)).Trim()
    $hash=$f.matrix.functions[0].sources[0].sha256;if ($Replacement) { $hash='deleted' }
    $f.matrix.functions[0].status='closed';WriteProduction $f
    $closure=[pscustomobject]@{path=$path;baselineBlob=$blob;currentSha256=$hash;
        functionKeys=@('fixture.runtime.rotation|MATH-ROT');migrationCommit=$reviewed;acceptance=[pscustomobject]@{commit=$recordCommit;path=$recordPath;reviewedCommit=$reviewed}}
    $closures=@($closure)
    if ($Replacement) {
        $newClosure=($closure|ConvertTo-Json -Depth 20)|ConvertFrom-Json
        $newClosure.path=$f.matrix.functions[0].sources[0].path;$newClosure.baselineBlob='absent';$newClosure.currentSha256=$f.matrix.functions[0].sources[0].sha256;$closures+=$newClosure
    }
    PutJson ($reuse+'closures.json') ([pscustomobject]@{schemaVersion='ird-reuse-closures/1';files=$closures})
    return $f
}
function Case([string]$name,[string]$expected,[scriptblock]$edit,[string[]]$extra=@()) {
    $script:restore=@{}
    $script:caseCommit=''
    try {
        & $edit
        # 负例的 stderr 是被测输出；PS5.1 不应先把它当作测试驱动自身的终结错误。
        $savedPreference = $ErrorActionPreference
        try { $ErrorActionPreference = 'Continue'; $output = (& $ps -NoProfile -File $validator -RepoRoot $sandbox @extra 2>&1 | ForEach-Object { "$_" } | Out-String -Width 32767); $code=$LASTEXITCODE } finally { $ErrorActionPreference = $savedPreference }
        $ok=($code -eq 0 -and $expected -eq 'PASS') -or ($code -ne 0 -and $expected -ne 'PASS' -and $output.Contains($expected))
        $script:results += [pscustomobject]@{name=$name;passed=$ok;exitCode=$code;expected=$expected;output=$output}
        [IO.File]::WriteAllText((Join-Path $OutputDir ($name+'.log')),$output)
        Write-Output "$name : $ok (exit=$code expected=$expected)"
    } finally {
        # 关闭记录夹具的提交仅在 sandbox；回退引用/索引，原字节仍由下面统一恢复。
        if ($script:caseCommit) { & git -C $sandbox reset --mixed --quiet $script:caseCommit;if ($LASTEXITCODE) { throw 'closure fixture reset failed' } }
        foreach ($full in $script:restore.Keys) {
            if ($null -eq $script:restore[$full]) { if (Test-Path -LiteralPath $full) { Remove-Item -LiteralPath $full -Force } }
            else { [IO.File]::WriteAllBytes($full,$script:restore[$full]) }
        }
    }
}
try {
    & git clone --shared --quiet --no-checkout $RepoRoot $sandbox
    if ($LASTEXITCODE) { throw 'fixture clone failed' }
    & git -C $sandbox config core.autocrlf true
    & git -C $sandbox config core.safecrlf false
    & git -C $sandbox sparse-checkout init --cone
    # 上游测试入口也是复用对象；精确检出两套既有 gtest 根，避免隔离夹具漏文件而误报合法目标。
    & git -C $sandbox sparse-checkout set 'RobWork/RobWork/src' 'RobWork/RobWorkSim/src' 'RobWork/RobWork/gtest' 'RobWork/RobWorkSim/gtest' $product ($design+'reuse') ($design+'tasks') ($design+'traceability/acceptance')
    & git -C $sandbox checkout --quiet --detach HEAD
    if ($LASTEXITCODE) { throw 'fixture checkout failed' }
    Copy-Item -LiteralPath $validatorSource -Destination $validator
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'reuse-contract.ps1') -Destination (Join-Path $sandbox 'reuse-contract.ps1')
    $copiedValidatorSha256=(Get-FileHash -LiteralPath $validator -Algorithm SHA256).Hash
    $copiedHelperSha256=(Get-FileHash -LiteralPath (Join-Path $sandbox 'reuse-contract.ps1') -Algorithm SHA256).Hash
    # 工作树治理输入复制进夹具；冻结提交/CI下也执行相同测试。
    foreach ($relative in @('reuse','tasks')) {
        $from=Join-Path $RepoRoot ($design+$relative); $to=Join-Path $sandbox ($design+$relative)
        [IO.Directory]::CreateDirectory($to)|Out-Null
        Copy-Item -Path (Join-Path $from '*') -Destination $to -Recurse -Force
    }
    Copy-Item -LiteralPath (Join-Path $RepoRoot ($product+'CMakeLists.txt')) -Destination (Join-Path $sandbox ($product+'CMakeLists.txt')) -Force
    $script:fixtureReviewedCommit=(& git -C $sandbox rev-parse HEAD).Trim()
    PutJson ($design+'traceability/acceptance/GOV-T22-fixture.json') ([pscustomobject]@{taskId='GOV-T22';verdict='pass';reviewedCommit=$script:fixtureReviewedCommit;scope='test fixture only'})
    & git -C $sandbox add -- ($design+'traceability/acceptance/GOV-T22-fixture.json')
    & git -C $sandbox -c user.name=ReuseFixture -c user.email=fixture@example.invalid commit --quiet -m 'fixture acceptance only'
    if ($LASTEXITCODE) { throw 'fixture acceptance commit failed' }
    $script:fixtureAcceptanceCommit=(& git -C $sandbox rev-parse HEAD).Trim()
    Case 'script-audit-no-fake-target' 'PASS' { PutScriptMatrix (ScriptMatrix) }
    Case 'script-audit-sourced-helper' 'PASS' {
        $m=ScriptMatrix;$path='RobWork/scripts/industrialrobot/reuse-contract.ps1';$text='# audit helper fixture';Put $path $text
        $m.functions[0].sources[0].path=$path;$m.functions[0].sources[0].sha256=Hash $text
        $m.functions[0].inputs+=([pscustomobject]@{path=$path;sha256=(Hash $text)});PutScriptMatrix $m
    }
    Case 'script-audit-require-real-entry-binding' 'REUSE-EXECUTION' { $m=ScriptMatrix;$m.functions[0].execution.entry='RobWork/scripts/industrialrobot/other.ps1';$m.functions[0].execution.command='pwsh -File RobWork/scripts/industrialrobot/other.ps1';PutScriptMatrix $m }
    Case 'script-audit-reject-cmake-target' 'REUSE-EXECUTION' { $m=ScriptMatrix;$m.functions[0].targets=@('sdurws_ird_core');PutScriptMatrix $m }
    Case 'script-audit-reject-product-escape' 'REUSE-EXECUTION' { $m=ScriptMatrix;$m.functions[0].sources[0].path=$product+'kinematics/src/Fk.cpp';PutScriptMatrix $m }
    Case 'script-audit-reject-other-directory' 'REUSE-EXECUTION' { $m=ScriptMatrix;$m.functions[0].execution.entry='RobWork/other.ps1';PutScriptMatrix $m }
    Case 'script-audit-reject-fake-migration' 'REUSE-EXECUTION' { $m=ScriptMatrix;$m.functions[0].status='migrated';PutScriptMatrix $m }
    Case 'script-audit-require-own-context' 'REUSE-EXECUTION' { $m=ScriptMatrix;$m.functions[0].inputs=@();PutScriptMatrix $m }
    Case 'script-audit-stale-context' 'REUSE-FRESHNESS' { $m=ScriptMatrix;$m.functions[0].inputs[0].sha256='bad';PutScriptMatrix $m }
    Case 'script-audit-reject-production-disguise' 'REUSE-EXECUTION' { $m=ScriptMatrix;$m.functions[0].execution.command='pwsh -File missing.ps1';PutScriptMatrix $m }
    Case 'independent-owner-required' 'REUSE-EXECUTION' { $m=ScriptMatrix;$m.functions[0].usageRole='adapter';PutScriptMatrix $m }
    Case 'independent-capability-none-required' 'REUSE-EXECUTION' { $m=ScriptMatrix;$m.functions[0].capabilityId='KIN-FK';PutScriptMatrix $m }
    Case 'independent-closed-rejected' 'REUSE-EXECUTION' { $m=ScriptMatrix;$m.functions[0].status='closed';PutScriptMatrix $m }
    Case 'independent-audited-rejected' 'REUSE-EXECUTION' { $m=ScriptMatrix;$m.functions[0].status='audited';PutScriptMatrix $m }
    Case 'independent-business-rejected' 'REUSE-EXECUTION' { $m=ScriptMatrix;$m.functions[0].decision='business-specific';PutScriptMatrix $m }
    Case 'independent-kind-rejected' 'REUSE-EXECUTION' { $m=ScriptMatrix;$m.functions[0].execution.kind='production';PutScriptMatrix $m }
    Case 'independent-proposal-positive' 'PASS' { $m=ScriptMatrix;$m.functions[0].decision='proposal';$m.functions[0].status='proposal';$p=$reuse+'script-proposal.md';Put $p 'proposal audit fixture';$m.functions[0]|Add-Member proposal ([pscustomobject]@{path=$p;sha256=(Hash 'proposal audit fixture')});PutScriptMatrix $m }
    Case 'independent-final-pending-rejected' 'REUSE-MIGRATION' { PutScriptMatrix (ScriptMatrix) } @('-RequireMigrationComplete')
    # 冻结矩阵与入口只在随机隔离夹具提交；读冻结正例后让工作树矩阵变坏。
    $freeze=ScriptMatrix;PutScriptMatrix $freeze
    & git -C $sandbox add --sparse -- ($reuse+'functions/foundation-governance.json') 'RobWork/scripts/industrialrobot/commit-lint.ps1'
    if ($LASTEXITCODE) { throw 'independent freeze stage failed' }
    & git -C $sandbox -c user.name=IndependentAudit -c user.email=audit@example.invalid commit --quiet -m 'independent frozen fixture only'
    if ($LASTEXITCODE) { throw 'independent freeze commit failed' }
    Case 'independent-frozen-positive' 'PASS' {} @('-Head','HEAD')
    Case 'independent-frozen-ignores-dirty' 'PASS' { Put ($reuse+'functions/foundation-governance.json') '{}' } @('-Head','HEAD')
} catch {
    $driverFailure = $_.Exception.Message
} finally {
    $failed=@($script:results | Where-Object { -not $_.passed }).Count
    $notRun = [Math]::Max(0,$expectedTests-$script:results.Count)
    $report=[ordered]@{schemaVersion='ird-reuse-tests/1';validatorSha256=$copiedValidatorSha256;helperSha256=$copiedHelperSha256;driverSha256=$driverSha256;expected=$expectedTests;executed=$script:results.Count;notRun=$notRun;driverFailure=$driverFailure;passed=($script:results.Count-$failed);skipped=0;failed=$failed;complete=($notRun -eq 0 -and -not $driverFailure);tests=$script:results}
    $report | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $OutputDir 'reuse-test-report.json') -Encoding UTF8
    $xml=[xml]'<testsuite name="reuse-governance"/>'
    $xml.DocumentElement.SetAttribute('tests',"$($script:results.Count)"); $xml.DocumentElement.SetAttribute('failures',"$failed"); $xml.DocumentElement.SetAttribute('skipped','0')
    foreach ($r in $script:results) {
        $c=$xml.CreateElement('testcase'); $c.SetAttribute('name',$r.name)
        if (-not $r.passed) { $f=$xml.CreateElement('failure'); $f.InnerText=$r.output; [void]$c.AppendChild($f) }
        [void]$xml.DocumentElement.AppendChild($c)
    }
    if ($driverFailure -or $notRun) {
        $c=$xml.CreateElement('testcase'); $c.SetAttribute('name','driver-completion')
        $f=$xml.CreateElement('error'); $f.InnerText="driverFailure=$driverFailure; notRun=$notRun"; [void]$c.AppendChild($f); [void]$xml.DocumentElement.AppendChild($c)
        $xml.DocumentElement.SetAttribute('tests',"$($script:results.Count+1)"); $xml.DocumentElement.SetAttribute('errors','1')
    }
    $xml.Save((Join-Path $OutputDir 'reuse-governance.junit.xml'))
    # 只清理本次创建的随机临时目录，先验证绝对路径位于系统临时根。
    $tempRoot=[IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\','/')+[IO.Path]::DirectorySeparatorChar
    if ([IO.Path]::GetFullPath($sandbox).StartsWith($tempRoot,[StringComparison]::OrdinalIgnoreCase) -and (Split-Path -Leaf $sandbox) -like 'ird-reuse-*') { Remove-Item -LiteralPath $sandbox -Recurse -Force }
}
if ($failed -or $driverFailure -or $script:results.Count -ne $expectedTests) { throw "reuse tests failed/incomplete: failed=$failed executed=$($script:results.Count) expected=$expectedTests driver=$driverFailure" }
Write-Output "reuse selftests: passed=$($script:results.Count) skipped=0 failed=0"
