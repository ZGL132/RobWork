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
# 规则夹具以已独验的补强治理树为固定输入，实际门禁代码仍取本次工作树。
# REUSE-T07 暴露：叠加全项目在途矩阵却不物化其源/设计/日志依赖，会让所有
# 用例提前报 MISSING。规则自测不承担当前产品适用性检查；后者由 validate-reuse
# 和 ird_gates 对真实工作树/冻结提交执行，不能用本夹具结果代替。
$fixtureCommit = 'e6f06e5b9451357b10001a4677b10b4ad9e454a6'
$validator = Join-Path $sandbox '.reuse-validator.ps1'
$design = 'RobWork/doc/industrial-robot-design/'
$reuse = $design+'reuse/'
$product = 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/'
$script:results = @(); $script:restore = @{}
$driverFailure = ''; $expectedTests = 172
$driverSha256=(Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash
$copiedValidatorSha256='';$copiedHelperSha256='';$copiedGoldenSha256=''
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
# 脚本结项形态夹具（migration §13.5.4 RC-10 规则批）：入口 dot-source 同目录
# helper（与 test-golden-assets.ps1 真实形态同构），completionEvidence 为沙箱内
# 真实落盘的执行记录文件；这是执行关系格式夹具，不冒充脚本语义验收。
function ScriptClosureMatrix([string]$status='migrated') {
    $dir='RobWork/scripts/industrialrobot/'
    $entryPath=$dir+'closure-fixture.ps1'
    $helperPath=$dir+'closure-helper.ps1'
    $helperText='# synthetic closure helper fixture only'
    $entryText="# synthetic governance closure fixture`n. (Join-Path `$PSScriptRoot 'closure-helper.ps1')`nfunction Invoke-ClosureFixtureStep {}"
    Put $helperPath $helperText
    Put $entryPath $entryText
    $log=$design+'traceability/reuse/REUSE-T59-FIXTURE/run.log'
    Put $log 'synthetic closure execution record fixture'
    $review=$design+'traceability/reuse/REUSE-T59-FIXTURE/nonoverlap.md'
    Put $review 'synthetic non-overlap fixture: governance-only script, no upstream primitive'
    $r=[pscustomobject]@{
        functionId='fixture.governance.closure';ownerUnit='foundation-governance';requirements=@('H6');
        sources=@([pscustomobject]@{path=$entryPath;symbol='Invoke-ClosureFixtureStep';startLine=1;endLine=3;sha256=(Hash $entryText)},
                  [pscustomobject]@{path=$helperPath;symbol='closure';startLine=1;endLine=1;sha256=(Hash $helperText)});
        targets=@();usageRole='owner';permissionRef='ARCH:3.6.4';capabilityId='NONE';decision='business-specific';status=$status;
        search=[pscustomobject]@{command='rg fixture';conclusion='脚本结项形态夹具，不冒充语义审计'};
        differences='无CMake编译target';
        inputs=@([pscustomobject]@{path=$entryPath;sha256=(Hash $entryText)},[pscustomobject]@{path=$helperPath;sha256=(Hash $helperText)});
        execution=[pscustomobject]@{kind='governance-script';entry=$entryPath;command=('pwsh -NoProfile -File '+$entryPath)};
        completionEvidence=[pscustomobject]@{path=$log;sha256=(Hash 'synthetic closure execution record fixture')};
        nonOverlapEvidence=[pscustomobject]@{path=$review;sha256=(Hash 'synthetic non-overlap fixture: governance-only script, no upstream primitive')}
    }
    return [pscustomobject]@{schemaVersion='ird-reuse-functions/1';unit='foundation-governance';functions=@($r)}
}
# closed 夹具的冻结序列：先提交 migrated 登记矩阵，再提交验收记录；提交仅在
# sandbox，Case finally reset＋字节恢复。返回验收引用所需 reviewed/recordPath。
function CommitClosureFixtureRegistration {
    $script:caseCommit=(& git -C $sandbox rev-parse HEAD).Trim()
    PutScriptMatrix (ScriptClosureMatrix 'migrated')
    & git -C $sandbox add -f -- ($reuse+'functions/foundation-governance.json')
    & git -C $sandbox -c user.name=ReuseFixture -c user.email=fixture@example.invalid commit --quiet -m 'isolated script closure registration'
    if ($LASTEXITCODE) { throw 'script closure registration commit failed' }
    return [pscustomobject]@{reviewed=(& git -C $sandbox rev-parse HEAD).Trim();recordPath=($design+'traceability/acceptance/REUSE-T59-fixture.json')}
}
function CommitClosureRecord($ref,[string]$taskId,[string]$verdict) {
    PutJson $ref.recordPath ([pscustomobject]@{taskId=$taskId;verdict=$verdict;reviewedCommit=$ref.reviewed;scope='synthetic script closure fixture only'})
    & git -C $sandbox add -- $ref.recordPath
    & git -C $sandbox -c user.name=ReuseFixture -c user.email=fixture@example.invalid commit --quiet -m 'isolated script closure record'
    if ($LASTEXITCODE) { throw 'script closure record commit failed' }
    return (& git -C $sandbox rev-parse HEAD).Trim()
}
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
function TestFixtureChange([string]$source) {
    # test-fixture 夹具例外（REUSE-T36 治理批）专用：与 FunctionalChange 同构，
    # 但 probe 落在产品树 /test/ 路径——该 origin 仅对此路径合法。
    $path=$product+'kinematics/test/PermissionProbeTest.cpp';$script:probePath=$path;Put $path $source
    $accept=[pscustomobject]@{commit=$script:fixtureAcceptanceCommit;path=($design+'traceability/acceptance/GOV-T22-fixture.json');reviewedCommit=$script:fixtureReviewedCommit}
    $t=[pscustomobject]@{taskId='KIN-T99';status='ready';allowedFiles=@($path);forbiddenFiles=@();governanceAcceptance=(GetJson ($design+'tasks/GOV-T22.json')).governanceAcceptance;reuseGateAcceptance=$accept;
        reuse=[pscustomobject]@{policy='H1-H6';manifest=($reuse+'changes/KIN-T99.json')}}
    PutJson ($design+'tasks/KIN-T99.json') $t
    PutJson ($reuse+'changes/KIN-T99.json') ([pscustomobject]@{schemaVersion='ird-reuse-change/1';taskId='KIN-T99';taskFile=($design+'tasks/KIN-T99.json');files=@([pscustomobject]@{path=$path;sha256=(Hash $source);mode='business';boundary='隔离权限夹具';verify='fixture';capabilities=@()})})
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
    & git -C $sandbox checkout --quiet --detach $fixtureCommit
    if ($LASTEXITCODE) { throw 'fixture checkout failed' }
    Copy-Item -LiteralPath $validatorSource -Destination $validator
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'reuse-contract.ps1') -Destination (Join-Path $sandbox 'reuse-contract.ps1')
    # validate-reuse.ps1:49 dot-source 的 golden-assets.ps1 也必须随行进沙箱副本，
    # 否则门禁在隔离检出加载即断（DESIGN-T01 引入该 dot-source 后的既有断裂，
    # REUSE-T59 规则批实测修复；拷贝完整性随报告钉哈希）。
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'golden-assets.ps1') -Destination (Join-Path $sandbox 'golden-assets.ps1')
    $copiedValidatorSha256=(Get-FileHash -LiteralPath $validator -Algorithm SHA256).Hash
    $copiedHelperSha256=(Get-FileHash -LiteralPath (Join-Path $sandbox 'reuse-contract.ps1') -Algorithm SHA256).Hash
    $copiedGoldenSha256=(Get-FileHash -LiteralPath (Join-Path $sandbox 'golden-assets.ps1') -Algorithm SHA256).Hash
    # 不叠加调用方在途数据；全部正反例在下方逐例修改固定夹具并恢复。
    $script:fixtureReviewedCommit=(& git -C $sandbox rev-parse HEAD).Trim()
    PutJson ($design+'traceability/acceptance/GOV-T22-fixture.json') ([pscustomobject]@{taskId='GOV-T22';verdict='pass';reviewedCommit=$script:fixtureReviewedCommit;scope='test fixture only'})
    & git -C $sandbox add -- ($design+'traceability/acceptance/GOV-T22-fixture.json')
    & git -C $sandbox -c user.name=ReuseFixture -c user.email=fixture@example.invalid commit --quiet -m 'fixture acceptance only'
    if ($LASTEXITCODE) { throw 'fixture acceptance commit failed' }
    $script:fixtureAcceptanceCommit=(& git -C $sandbox rev-parse HEAD).Trim()
    # HOST-WORKBENCH 宿主例外（2026-10-26 所有者批量批准扩面，REUSE-T41/F-967
    # 落地批登记）：门禁代码取工作树、数据面取冻结治理树的隔离口径不变——冻结
    # 树 e6f06e5b 的 permissions.json 尚无第 5 条宿主例外，本驱动在隔离副本补齐
    # HOST-WORKBENCH（UiPlugin.{hpp,cpp} 文件面），使全部正反例按当前封闭清单
    # （恰 5 项）运行；后续逐例改动仍以该状态为恢复基线。
    $p=GetJson ($reuse+'permissions.json')
    $p.hostExceptions += [pscustomobject]@{id='HOST-WORKBENCH';files=@(($product+'ui/plugin/UiPlugin.hpp'),($product+'ui/plugin/UiPlugin.cpp'));target='sdurws_ird_ui_plugin';permissionRef='ARCH:3.6.2'}
    PutJson ($reuse+'permissions.json') $p
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
    # 脚本结项形态（RC-10 规则批）：正例 migrated/closed 各一，负例逐一断言精确失败码——
    # completionEvidence 伪造（缺失/摘要不符/路径不存在）、acceptance 伪造（缺失/假 verdict/
    # 记录缺席/路径出域/被验 SHA 不可达）、sources 摘要过期、遗漏同目录 helper 区段、
    # targets 非空、audited 非准入状态。既有 pending/proposal 行为由上方 script-audit 组继续覆盖。
    Case 'script-closure-migrated-positive' 'PASS' { PutScriptMatrix (ScriptClosureMatrix) }
    Case 'script-closure-closed-positive' 'PASS' {
        $ref=CommitClosureFixtureRegistration
        $recordCommit=CommitClosureRecord $ref 'REUSE-T59' 'pass'
        $m=ScriptClosureMatrix 'closed'
        $m.functions[0] | Add-Member acceptance ([pscustomobject]@{commit=$recordCommit;path=$ref.recordPath;reviewedCommit=$ref.reviewed})
        PutScriptMatrix $m
    }
    Case 'script-closure-missing-completion-evidence' 'REUSE-EXECUTION' { $m=ScriptClosureMatrix;$m.functions[0].PSObject.Properties.Remove('completionEvidence');PutScriptMatrix $m }
    Case 'script-closure-stale-completion-evidence' 'REUSE-FRESHNESS' { $m=ScriptClosureMatrix;$m.functions[0].completionEvidence.sha256='0000';PutScriptMatrix $m }
    Case 'script-closure-missing-completion-file' 'REUSE-MISSING' { $m=ScriptClosureMatrix;$m.functions[0].completionEvidence.path=($design+'traceability/reuse/REUSE-T59-FIXTURE/absent-run.log');PutScriptMatrix $m }
    Case 'script-closure-entry-region-unbound' 'REUSE-EXECUTION' {
        $m=ScriptClosureMatrix;$r=$m.functions[0]
        $r.sources=@($r.sources | Where-Object { "$($_.path)" -notlike '*closure-fixture.ps1' });PutScriptMatrix $m
    }
    Case 'script-closure-missing-helper-region' 'REUSE-EXECUTION' {
        $m=ScriptClosureMatrix;$r=$m.functions[0]
        $r.sources=@($r.sources | Where-Object { "$($_.path)" -notlike '*closure-helper.ps1' });PutScriptMatrix $m
    }
    Case 'script-closure-stale-source-region' 'REUSE-FRESHNESS' { $m=ScriptClosureMatrix;$m.functions[0].sources[0].sha256='0000';PutScriptMatrix $m }
    Case 'script-closure-targets-nonempty' 'REUSE-EXECUTION' { $m=ScriptClosureMatrix;$m.functions[0].targets=@('sdurws_ird_core');PutScriptMatrix $m }
    Case 'script-closure-audited-not-admitted' 'REUSE-EXECUTION' { PutScriptMatrix (ScriptClosureMatrix 'audited') }
    Case 'script-closure-closed-without-acceptance' 'REUSE-CLOSURE' { PutScriptMatrix (ScriptClosureMatrix 'closed') }
    Case 'script-closure-forged-acceptance-rejected' 'REUSE-CLOSURE' {
        $ref=CommitClosureFixtureRegistration
        $recordCommit=CommitClosureRecord $ref 'REUSE-T59' 'fail'
        $m=ScriptClosureMatrix 'closed'
        $m.functions[0] | Add-Member acceptance ([pscustomobject]@{commit=$recordCommit;path=$ref.recordPath;reviewedCommit=$ref.reviewed})
        PutScriptMatrix $m
    }
    Case 'script-closure-acceptance-object-missing' 'REUSE-CLOSURE' {
        $ref=CommitClosureFixtureRegistration
        $recordCommit=CommitClosureRecord $ref 'REUSE-T59' 'pass'
        $m=ScriptClosureMatrix 'closed'
        $m.functions[0] | Add-Member acceptance ([pscustomobject]@{commit=$recordCommit;path=($design+'traceability/acceptance/REUSE-T59-absent.json');reviewedCommit=$ref.reviewed})
        PutScriptMatrix $m
    }
    Case 'script-closure-acceptance-path-outside' 'REUSE-CLOSURE' {
        $ref=CommitClosureFixtureRegistration
        $recordCommit=CommitClosureRecord $ref 'REUSE-T59' 'pass'
        $m=ScriptClosureMatrix 'closed'
        $m.functions[0] | Add-Member acceptance ([pscustomobject]@{commit=$recordCommit;path=($reuse+'fixture-production.json');reviewedCommit=$ref.reviewed})
        PutScriptMatrix $m
    }
    Case 'script-closure-acceptance-future-sha-rejected' 'REUSE-CLOSURE' {
        $ref=CommitClosureFixtureRegistration
        $recordCommit=CommitClosureRecord $ref 'REUSE-T59' 'pass'
        $m=ScriptClosureMatrix 'closed'
        $m.functions[0] | Add-Member acceptance ([pscustomobject]@{commit=$recordCommit;path=$ref.recordPath;reviewedCommit='0000000000000000000000000000000000000000'})
        PutScriptMatrix $m
    }
    Case '01-clean' 'PASS' {}
    Case '02-fixed-baseline' 'REUSE-BASELINE' { $j=GetJson ($reuse+'catalog.json'); $j.baselineCommit='HEAD'; PutJson ($reuse+'catalog.json') $j }
    Case '03-all-units' 'REUSE-UNITS' { $j=GetJson ($reuse+'catalog.json'); $j.units=@('kinematics'); PutJson ($reuse+'catalog.json') $j }
    Case '04-missing-source' 'REUSE-MISSING' { $j=GetJson ($reuse+'catalog.json'); $j.capabilities[0].upstream[0].path='RobWork/RobWork/src/rw/missing.hpp'; PutJson ($reuse+'catalog.json') $j }
    Case '05-stale-source' 'REUSE-UPSTREAM-HASH' { $j=GetJson ($reuse+'catalog.json'); $j.capabilities[0].upstream[0].sha256='0000'; PutJson ($reuse+'catalog.json') $j }
    Case '06-missing-symbol' 'REUSE-UPSTREAM-SYMBOL' { $j=GetJson ($reuse+'catalog.json'); $j.capabilities[0].upstream[0].symbols=@('NoSuchSymbol123'); PutJson ($reuse+'catalog.json') $j }
    Case '07-inventory-omission' 'REUSE-INVENTORY' { $j=GetJson ($reuse+'inventory.json'); $j.files=@($j.files | Select-Object -Skip 1); PutJson ($reuse+'inventory.json') $j }
    Case '08-forged-blob' 'REUSE-INVENTORY' { $j=GetJson ($reuse+'inventory.json'); $j.files[0].baselineBlob='0000'; PutJson ($reuse+'inventory.json') $j }
    Case '09-inventory-duplicate' 'REUSE-INVENTORY' { $j=GetJson ($reuse+'inventory.json'); $j.files += $j.files[0]; PutJson ($reuse+'inventory.json') $j }
    Case '10-inventory-unit' 'REUSE-INVENTORY' { $j=GetJson ($reuse+'inventory.json'); $j.files[0].unit='wrong'; PutJson ($reuse+'inventory.json') $j }
    Case '11-new-source' 'REUSE-UNDECLARED' { Put ($product+'kinematics/src/Undeclared.cpp') '// new source' }
    Case '12-stale-change-hash' 'REUSE-UNDECLARED' { Put ($product+'CMakeLists.txt') ([IO.File]::ReadAllText((Join-Path $sandbox ($product+'CMakeLists.txt')))+"`n# changed content`n") }
    Case '13-core-even-business' 'REUSE-DUPLICATE' { Declare 'Duplicate' 'struct Vec3 { double x; };' }
    Case '14-cpp-not-governance' 'REUSE-MODE' { Declare 'WrongMode' '// fixture' 'governance' }
    Case '15-missing-approval' 'REUSE-PATH' { Declare 'Unapproved' '// fixture' 'extension' }
    Case '16-unknown-capability' 'REUSE-CAPABILITY' { $j=GetJson ($reuse+'changes/GOV-T21.json'); $j.files[0].capabilities=@('NONE'); PutJson ($reuse+'changes/GOV-T21.json') $j }
    Case '17-path-traversal' 'REUSE-PATH' { $j=GetJson ($reuse+'changes/GOV-T21.json'); $j.files[0].path='../outside.cpp'; PutJson ($reuse+'changes/GOV-T21.json') $j }
    Case '18-missing-task-proof' 'REUSE-TASK' { $j=GetJson ($design+'tasks/GOV-T21.json'); $j.PSObject.Properties.Remove('reuse'); PutJson ($design+'tasks/GOV-T21.json') $j }
    Case '19-task-scope' 'REUSE-SCOPE' { $j=GetJson ($design+'tasks/GOV-T21.json'); $j.allowedFiles=@('none'); PutJson ($design+'tasks/GOV-T21.json') $j }
    Case '20-forbidden-scope' 'REUSE-SCOPE' { $j=GetJson ($design+'tasks/GOV-T21.json'); $j.forbiddenFiles=@($product+'CMakeLists.txt'); PutJson ($design+'tasks/GOV-T21.json') $j }
    Case '21-stage-not-approved' 'REUSE-STAGE' { $j=GetJson ($design+'tasks/REUSE-T01.json'); $j.status='ready';$j.PSObject.Properties.Remove('governanceAcceptance'); $j | Add-Member reuse ([pscustomobject]@{policy='H1-H6';manifest=($reuse+'changes/REUSE-T01.json')}) -Force; PutJson ($design+'tasks/REUSE-T01.json') $j } @('-TaskFile',($design+'tasks/REUSE-T01.json'))
    Case '22-final-debt-rejected' 'REUSE-MIGRATION' {} @('-RequireMigrationComplete')
    Case '23-governance-not-algorithm-approval' 'REUSE-GOV-SCOPE' { Declare 'Business' '//业务格式转换 fixture' }
    Case '24-upstream-readonly' 'REUSE-UPSTREAM-READONLY' { Put 'RobWork/RobWork/src/rw/Undeclared.cpp' '// forbidden upstream' }
    Case '25-missing-record-file' 'REUSE-MISSING' { $j=GetJson ($reuse+'changes/GOV-T21.json'); $j.taskFile=$design+'tasks/Absent.json'; PutJson ($reuse+'changes/GOV-T21.json') $j }
    Case '26-deletion-undeclared' 'REUSE-UNDECLARED' {
        $p=Join-Path $sandbox ($product+'kinematics/src/Fk.cpp'); $script:restore[$p]=[IO.File]::ReadAllBytes($p); Remove-Item -LiteralPath $p
    }
    Case '27-forged-debt-closure' 'REUSE-INVENTORY' { $j=GetJson ($reuse+'inventory.json'); $j.files[0].classification='existing-adapter'; PutJson ($reuse+'inventory.json') $j }
    Case '28-arbitrary-task-ready' 'REUSE-TASK' { PutJson ($design+'tasks/OTHER-T99.json') ([pscustomobject]@{taskId='OTHER-T99';status='ready'}) }
    Case '29-planned-not-executable' 'REUSE-TASK-STATE' { $j=GetJson ($design+'tasks/REUSE-T01.json');$j.status='planned';PutJson ($design+'tasks/REUSE-T01.json') $j } @('-TaskFile',($design+'tasks/REUSE-T01.json'))
    Case '30-forged-build-target' 'REUSE-UPSTREAM-TARGET' { $j=GetJson ($reuse+'catalog.json'); $j.capabilities[0].upstream[0].target='nonexistent'; PutJson ($reuse+'catalog.json') $j }
    # 上游已有测试入口也是可复用资产；仅放开真实gtest根CMake，不扩展到任意目录。
    Case 'upstream-gtest-build-target' 'PASS' {
        $j=GetJson ($reuse+'catalog.json');$u=$j.capabilities[0].upstream[0]
        $u.path='RobWork/RobWork/gtest/test-main.cpp';$u.symbols=@('main')
        $u.sha256=Hash ([IO.File]::ReadAllText((Join-Path $sandbox $u.path)))
        $u.target='sdurw-gtest-main';$u.targetFile='RobWork/RobWork/gtest/CMakeLists.txt'
        $u.targetSha256=Hash ([IO.File]::ReadAllText((Join-Path $sandbox $u.targetFile)))
        # v2 同时绑定能力的目标集合；替换候选来源后同步派生字段，避免正例自身不一致。
        $j.capabilities[0].build.targets=@($j.capabilities[0].upstream | ForEach-Object { $_.target } | Sort-Object -Unique)
        PutJson ($reuse+'catalog.json') $j
    }
    Case 'upstream-arbitrary-test-build-path-rejected' 'REUSE-UPSTREAM-TARGET' {
        $j=GetJson ($reuse+'catalog.json');$u=$j.capabilities[0].upstream[0]
        $u.target='sdurw-gtest-main';$u.targetFile='RobWork/RobWork/gtest/fake/CMakeLists.txt'
        Put $u.targetFile 'add_library(sdurw-gtest-main STATIC fake.cpp)'
        $u.targetSha256=Hash ([IO.File]::ReadAllText((Join-Path $sandbox $u.targetFile)))
        PutJson ($reuse+'catalog.json') $j
    }
    # 首轮验收真实发现.c绕过；每种代码入口均须被同一门禁拒绝，无目录豁免。
    foreach ($suffix in @('c','ipp','inl','tpp','mjs','ps1','py','html','ts','cu','inc','opaque')) {
        $caseSuffix = $suffix
        Case ("suffix-"+$suffix) 'REUSE-UNDECLARED' { Put ($product+'kinematics/src/Undeclared.'+$caseSuffix) '// undeclared executable input' }
    }
    # SA-19/逐功能数据补强：先证明旧门禁确实无法拒绝这些坏输入，不能只测 JSON 可解析。
    Case 'ad-unknown-catalog-schema' 'REUSE-BASELINE' { $j=GetJson ($reuse+'catalog.json'); $j.schemaVersion='ird-reuse/999'; PutJson ($reuse+'catalog.json') $j }
    Case 'ad-owner-missing' 'REUSE-CATALOG-V2' { $j=GetJson ($reuse+'catalog.json'); $j.capabilities[0].PSObject.Properties.Remove('ownerUnit'); PutJson ($reuse+'catalog.json') $j }
    Case 'ad-unknown-role' 'REUSE-CATALOG-V2' { $j=GetJson ($reuse+'catalog.json'); $j.capabilities[0].usages[0].usageRole='L2'; PutJson ($reuse+'catalog.json') $j }
    Case 'ad-fake-resource' 'REUSE-MISSING' { $j=GetJson ($reuse+'catalog.json'); $j.capabilities[0].tests=@([pscustomobject]@{path='RobWork/RobWork/gtest/absent.cpp';sha256='0000';symbols=@('absent')}); PutJson ($reuse+'catalog.json') $j }
    Case 'ad-permission-schema' 'REUSE-PERMISSIONS' { $j=GetJson ($reuse+'permissions.json'); $j.schemaVersion='wrong'; PutJson ($reuse+'permissions.json') $j }
    Case 'ad-owner-expansion' 'REUSE-PERMISSIONS' { $j=GetJson ($reuse+'permissions.json'); $j.modelOwner='kinematics'; PutJson ($reuse+'permissions.json') $j }
    Case 'ad-host-directory-exemption' 'REUSE-PERMISSIONS' { $j=GetJson ($reuse+'permissions.json'); $j.hostExceptions[0].files=@($product+'ui/plugin/*'); PutJson ($reuse+'permissions.json') $j }
    Case 'ad-empty-matrix' 'REUSE-FUNCTION' { PutJson ($reuse+'functions/kinematics.json') ([pscustomobject]@{schemaVersion='ird-reuse-functions/1';unit='kinematics';functions=@([pscustomobject]@{functionId='fake'})}) }
    Case 'ad-unknown-matrix-schema' 'REUSE-FUNCTION' { PutJson ($reuse+'functions/kinematics.json') ([pscustomobject]@{schemaVersion='wrong';unit='kinematics';functions=@()}) }
    Case 'ad-wrong-matrix-unit' 'REUSE-FUNCTION' { PutJson ($reuse+'functions/kinematics.json') ([pscustomobject]@{schemaVersion='ird-reuse-functions/1';unit='runtime';functions=@()}) }
    Case 'ad-self-approved-closure' 'REUSE-CLOSURE' { PutJson ($reuse+'closures.json') ([pscustomobject]@{schemaVersion='ird-reuse-closures/1';files=@([pscustomobject]@{path=$product+'kinematics/src/Fk.cpp';approved=$true})}) }
    Case 'ad-gov-supplement-scope' 'REUSE-GOV-SCOPE' { $j=GetJson ($design+'tasks/GOV-T22.json'); $j.allowedFiles+=($product+'kinematics/src/Fk.cpp'); PutJson ($design+'tasks/GOV-T22.json') $j }
    Case 'function-pending-positive' 'PASS' { PutMath (MathMatrix) }
    Case 'function-existing-restricted-pending-positive' 'PASS' { PutMath (RestrictedAuditMatrix) }
    Case 'function-existing-restricted-audited-rejected' 'REUSE-AD' { $m=RestrictedAuditMatrix;$m.functions[0].status='audited';PutMath $m }
    Case 'function-changed-restricted-pending-rejected' 'REUSE-AD' { FunctionalChange '#include <rw/models/WorkCell.hpp> // probe';ProbeMatrix 'pending' 'pending' }
    Case 'function-final-restricted-pending-rejected' 'REUSE-MIGRATION' { PutMath (RestrictedAuditMatrix) } @('-RequireMigrationComplete')
    Case 'function-stale-region' 'REUSE-FRESHNESS' { $m=MathMatrix; $m.functions[0].sources[0].sha256='0000'; PutMath $m }
    Case 'function-symbol-not-present' 'REUSE-FRESHNESS' { $m=MathMatrix; $m.functions[0].sources[0].symbol='not-a-real-function'; PutMath $m }
    Case 'function-region-outside-file' 'REUSE-FUNCTION' { $m=MathMatrix; $m.functions[0].sources[0].endLine=999999; PutMath $m }
    Case 'function-duplicate-relation' 'REUSE-FUNCTION' { $m=MathMatrix; $m.functions+= $m.functions[0]; PutMath $m }
    Case 'function-stale-build-input' 'REUSE-FRESHNESS' { $m=MathMatrix; $m.functions[0].inputs[0].sha256='0000'; PutMath $m }
    Case 'function-role-not-layer' 'REUSE-FUNCTION' { $m=MathMatrix; $m.functions[0].usageRole='L2'; PutMath $m }
    Case 'function-unapproved-permission' 'REUSE-PERMISSION-REF' { $m=MathMatrix; $m.functions[0].permissionRef='APPROVED-BY-ME'; PutMath $m }
    Case 'function-fake-capability' 'REUSE-FUNCTION' { $m=MathMatrix; $m.functions[0].capabilityId='fake'; PutMath $m }
    Case 'function-unresolved-not-migrated' 'REUSE-FUNCTION' { $m=MathMatrix; $m.functions[0].status='migrated'; PutMath $m }
    Case 'positive-evidence-required' 'REUSE-POSITIVE' { $m=MathMatrix; $m.functions[0].status='migrated'; $m.functions[0].decision='direct'; PutMath $m }
    Case 'business-specific-needs-evidence' 'REUSE-FUNCTION' { $m=MathMatrix; $m.functions[0].capabilityId='NONE'; $m.functions[0].decision='business-specific'; PutMath $m }
    Case 'proposal-needs-file' 'REUSE-FUNCTION' { $m=MathMatrix; $m.functions[0].decision='proposal'; PutMath $m }
    Case 'grant-readonly-positive' 'PASS' { $j=GetJson ($reuse+'permissions.json'); $j.grants=@(Grant); PutJson ($reuse+'permissions.json') $j }
    Case 'grant-stale-source' 'REUSE-FRESHNESS' { $j=GetJson ($reuse+'permissions.json'); $g=Grant; $g.source.sha256='0000';$j.grants=@($g);PutJson ($reuse+'permissions.json') $j }
    Case 'grant-business-owner-rejected' 'REUSE-AD' { $j=GetJson ($reuse+'permissions.json');$j.grants=@(Grant 'runtime-owner');PutJson ($reuse+'permissions.json') $j }
    Case 'grant-business-construction-rejected' 'REUSE-AD' { $j=GetJson ($reuse+'permissions.json');$j.grants=@(Grant 'runtime-borrow' @('construct-model'));PutJson ($reuse+'permissions.json') $j }
    Case 'grant-shared-state-write-rejected' 'REUSE-AD' { $j=GetJson ($reuse+'permissions.json');$j.grants=@(Grant 'runtime-borrow' @('write-state'));PutJson ($reuse+'permissions.json') $j }
    Case 'grant-unknown-target-rejected' 'REUSE-AD' { $j=GetJson ($reuse+'permissions.json');$j.grants=@(Grant 'runtime-borrow' @('query') 'sdurws_ird_kinematics_nonexistent');PutJson ($reuse+'permissions.json') $j }
    Case 'ad-real-constructor-rejected' 'REUSE-AD' { FunctionalChange 'void probe(){ auto cell = new rw::models::WorkCell("parallel"); }' }
    Case 'ad-real-factory-rejected' 'REUSE-AD' { FunctionalChange 'void probe(){ auto cell = createWorkCell(); }' }
    Case 'ad-real-loader-rejected' 'REUSE-AD' { FunctionalChange '#include <rw/loaders/WorkCellLoader.hpp>' }
    Case 'ad-include-without-grant-rejected' 'REUSE-AD' { FunctionalChange '#include <rw/models/WorkCell.hpp>' }
    Case 'ad-value-without-matrix-rejected' 'REUSE-COVERAGE' { FunctionalChange '#include <rw/math/Transform3D.hpp>' }
    Case 'ad-stage-missing-rejected' 'REUSE-SUPPLEMENT-STAGE' { FunctionalChange '// fixture'; $j=GetJson ($design+'tasks/KIN-T99.json');$j.PSObject.Properties.Remove('reuseGateAcceptance');PutJson ($design+'tasks/KIN-T99.json') $j }
    Case 'ad-stage-forged-verdict-rejected' 'REUSE-SUPPLEMENT-STAGE' { FunctionalChange '// fixture';$j=GetJson ($design+'tasks/KIN-T99.json');$j.reuseGateAcceptance.path=$design+'traceability/acceptance/absent.json';PutJson ($design+'tasks/KIN-T99.json') $j }
    Case 'ad-stage-future-sha-rejected' 'REUSE-SUPPLEMENT-STAGE' { FunctionalChange '// fixture';$j=GetJson ($design+'tasks/KIN-T99.json');$j.reuseGateAcceptance.reviewedCommit='0000000000000000000000000000000000000000';PutJson ($design+'tasks/KIN-T99.json') $j }
    Case 'ad-catalog-downgrade-rejected' 'REUSE-BASELINE' { $j=GetJson ($reuse+'catalog.json');$j.schemaVersion='ird-reuse/1';PutJson ($reuse+'catalog.json') $j }
    Case 'ad-usage-operation-expansion' 'REUSE-CATALOG-V2' { $j=GetJson ($reuse+'catalog.json');$j.capabilities[0].usages[0].operations=@('publish');PutJson ($reuse+'catalog.json') $j }
    Case 'ad-math-value-positive' 'PASS' { FunctionalChange 'void probe(){ /* value holder only */ }';ProbeMatrix }
    Case 'ad-changed-pending-rejected' 'REUSE-COVERAGE' { FunctionalChange 'void probe(){}';ProbeMatrix 'pending' 'pending' }
    Case 'ad-host-origin-bypass' 'REUSE-AD' { FunctionalChange '#include <rws/RobWorkStudio.hpp>';ProbeGrant 'task-private' @('host','graphics') @('render') }
    Case 'ad-value-origin-bypass' 'REUSE-AD' { FunctionalChange 'void probe(){}';ProbeGrant 'value' @('model') @('query') }
    Case 'ad-actual-shared-state-write' 'REUSE-AD' { FunctionalChange 'void probe(){ device->setQ(q, sharedState); }';ProbeGrant 'runtime-borrow' @('state') @('query') }
    Case 'ad-actual-model-mutation' 'REUSE-AD' { FunctionalChange 'void probe(){ cell->addDevice(device); }';ProbeGrant 'runtime-borrow' @('model') @('query') }
    Case 'ad-forward-model-type' 'REUSE-AD' { FunctionalChange 'rw::models::Device* probe();' }
    Case 'ad-probe-readonly-positive' 'PASS' { FunctionalChange 'rw::models::Device* probe();';ProbeGrant 'runtime-borrow' @('model') @('query');ProbeMatrix }
    Case 'ad-private-state-positive' 'PASS' { FunctionalChange 'void probe(){ device->setQ(q, privateState); }';ProbeGrant 'task-private' @('state') @('write-state');ProbeMatrix }
    Case 'production-structure-positive' 'PASS' { [void](ProductionFixture) }
    Case 'production-demo-entry-rejected' 'REUSE-POSITIVE' { $f=ProductionFixture;$f.matrix.functions[0].production.entry='demo-only';WriteProduction $f }
    Case 'production-unverified-capability' 'REUSE-SUITABILITY' { [void](ProductionFixture);$c=GetJson ($reuse+'catalog.json');@($c.capabilities | Where-Object { $_.id -eq 'MATH-ROT' })[0].review.status='candidate';PutJson ($reuse+'catalog.json') $c }
    Case 'production-missing-dependency' 'REUSE-FRESHNESS' { $f=ProductionFixture;$f.report.bindings=@($f.report.bindings | Select-Object -Skip 1);WriteProduction $f }
    Case 'production-stale-log' 'REUSE-POSITIVE' { $f=ProductionFixture;$f.report.normal.log.sha256='0000';WriteProduction $f }
    Case 'production-disconnect-stays-green' 'REUSE-POSITIVE' { $f=ProductionFixture;$f.report.disconnected.exitCode=0;WriteProduction $f }
    Case 'production-mutation-no-change' 'REUSE-POSITIVE' { $f=ProductionFixture;$f.report.disconnected.mutatedSource=[pscustomobject]@{path=$f.matrix.functions[0].sources[0].path;sha256=$f.report.disconnected.mutation.originalSha256};WriteProduction $f }
    Case 'production-wrong-mutation-source' 'REUSE-POSITIVE' { $f=ProductionFixture;$f.report.disconnected.mutation.source=$product+'kinematics/src/Fk.cpp';WriteProduction $f }
    Case 'production-empty-xml-shell' 'REUSE-POSITIVE' { $f=ProductionFixture;$xml='<testsuites><testsuite tests="1" failures="0"/></testsuites>';Put $f.report.normal.xml.path $xml;$f.report.normal.xml.sha256=Hash $xml;WriteProduction $f }
    Case 'production-oracle-failed' 'REUSE-POSITIVE' { $f=ProductionFixture;$f.report.oracle.exitCode=1;WriteProduction $f }
    Case 'production-upstream-comparison-failed' 'REUSE-POSITIVE' { $f=ProductionFixture;$f.report.upstreamComparison.exitCode=1;WriteProduction $f }
    Case 'ad-new-cmake-requires-functions' 'REUSE-COVERAGE' {
        FunctionalChange 'void probe(){}';ProbeMatrix
        $p=$product+'kinematics/HiddenGenerator.cmake';$s='execute_process(COMMAND fake_solver)';Put $p $s
        $t=GetJson ($design+'tasks/KIN-T99.json');$t.allowedFiles+=$p;PutJson ($design+'tasks/KIN-T99.json') $t
        $m=GetJson ($reuse+'changes/KIN-T99.json');$m.files += [pscustomobject]@{path=$p;sha256=(Hash $s);mode='governance';boundary='fixture';verify='fixture';capabilities=@()};PutJson ($reuse+'changes/KIN-T99.json') $m
    }
    Case 'function-deleted-replacement-unknown' 'REUSE-FUNCTION' { $m=MathMatrix;$m.functions[0] | Add-Member replaces @($product+'not-a-baseline.cpp');PutMath $m }
    Case 'production-review-version-fake' 'REUSE-SUITABILITY' { [void](ProductionFixture);$c=GetJson ($reuse+'catalog.json');@($c.capabilities | Where-Object { $_.id -eq 'MATH-ROT' })[0].review.version='0000000000000000000000000000000000000000';PutJson ($reuse+'catalog.json') $c }
    Case 'closure-current-positive' 'PASS' { [void](ClosureFixture) }
    Case 'closure-bom-log-positive' 'PASS' { [void](ClosureFixture -BomLog) }
    Case 'closure-deleted-replacement-positive' 'PASS' { [void](ClosureFixture -Replacement) }
    Case 'closure-changed-function-after-pass' 'REUSE-CLOSURE' { $f=ClosureFixture;$f.matrix.functions[0].differences='changed after acceptance';WriteProduction $f }
    Case 'closure-deletion-missing-replaces' 'REUSE-CLOSURE' { $f=ClosureFixture -Replacement;$f.matrix.functions[0].PSObject.Properties.Remove('replaces');WriteProduction $f }
    Case 'closure-accepted-matrix-missing' 'REUSE-CLOSURE' { [void](ClosureFixture);$c=GetJson ($reuse+'closures.json');$c.files[0].migrationCommit=$script:fixtureReviewedCommit;$c.files[0].acceptance.reviewedCommit=$script:fixtureReviewedCommit;$c.files[0].acceptance.commit=$script:fixtureAcceptanceCommit;$c.files[0].acceptance.path=$design+'traceability/acceptance/GOV-T22-fixture.json';PutJson ($reuse+'closures.json') $c }
    foreach ($hostId in @('HOST-RENDER','HOST-PRESENT','HOST-COMPILE','HOST-GATEWAY','HOST-WORKBENCH')) {
        $currentHost=$hostId
        Case ('grant-'+$hostId+'-positive') 'PASS' {
            $p=GetJson ($reuse+'permissions.json');$h=@($p.hostExceptions | Where-Object { $_.id -eq $currentHost })[0]
            $g=Grant 'host-presentation' @('query','render','write-state');$g.source.path=$h.files[1];$g.source.sha256=Hash ([IO.File]::ReadAllText((Join-Path $sandbox $g.source.path)))
            $g.targets=@($h.target);$g.categories=@('model','state','graphics','host');$g.permissionRef=$h.permissionRef;$g | Add-Member exceptionId $h.id
            $g.build.path=$product+'ui/CMakeLists.txt';$g.build.sha256=Hash ([IO.File]::ReadAllText((Join-Path $sandbox $g.build.path)))
            $p.grants=@($g);PutJson ($reuse+'permissions.json') $p
        }
    }
    # 封闭清单扩面后负例仍然成立：清单外 ID（HOST-NOPE）即使给出合法文件面与
    # 目标/权限引用，也必须被机读表拒绝——扩面只能经本脚本 $names 表+ARCH §3.6.2
    # 登记完成，permissions.json 不能自扩例外。
    Case 'ad-host-unknown-id-rejected' 'REUSE-PERMISSIONS' {
        $j=GetJson ($reuse+'permissions.json')
        $j.hostExceptions += [pscustomobject]@{id='HOST-NOPE';files=@(($product+'ui/plugin/UiPlugin.hpp'),($product+'ui/plugin/UiPlugin.cpp'));target='sdurws_ird_ui_plugin';permissionRef='ARCH:3.6.2'}
        PutJson ($reuse+'permissions.json') $j
    }
    Case 'grant-runtime-owner-positive' 'PASS' {
        $p=GetJson ($reuse+'permissions.json');$g=Grant 'runtime-owner' @('construct-model','load-model','mutate-model') 'sdurws_ird_runtime';$g.usageRole='owner'
        $g.source.path=$product+'runtime/src/WorkCellCompiler.cpp';$g.source.sha256=Hash ([IO.File]::ReadAllText((Join-Path $sandbox $g.source.path)))
        $g.build.path=$product+'runtime/CMakeLists.txt';$g.build.sha256=Hash ([IO.File]::ReadAllText((Join-Path $sandbox $g.build.path)));$p.grants=@($g);PutJson ($reuse+'permissions.json') $p
    }
    foreach ($origin in @('runtime-borrow','task-private')) {
        $workerOrigin=$origin
        Case ('grant-worker-'+$origin+'-rejected') 'REUSE-AD' {
            $p=GetJson ($reuse+'permissions.json');$g=Grant $workerOrigin @('query') 'sdurws_ird_execution_worker'
            $g.source.path=$product+'execution/worker/main.cpp';$g.source.sha256=Hash ([IO.File]::ReadAllText((Join-Path $sandbox $g.source.path)))
            if ($workerOrigin -eq 'task-private') { $g.categories=@('graphics') }
            $g.build.path=$product+'execution/CMakeLists.txt';$g.build.sha256=Hash ([IO.File]::ReadAllText((Join-Path $sandbox $g.build.path)));$p.grants=@($g);PutJson ($reuse+'permissions.json') $p
        }
    }
    Case 'grant-commented-target-rejected' 'REUSE-AD' {
        $p=GetJson ($reuse+'permissions.json');$g=Grant 'runtime-borrow' @('query') 'sdurws_ird_kinematics_fake'
        $txt=[IO.File]::ReadAllText((Join-Path $sandbox $g.build.path))+"`n# add_library(sdurws_ird_kinematics_fake STATIC src/Fk.cpp)`n"
        Put $g.build.path $txt;$g.build.sha256=Hash $txt;$p.grants=@($g);PutJson ($reuse+'permissions.json') $p
    }
    # test-fixture 夹具例外（REUSE-T36 治理批）：/test/ 路径+夹具 grant 正例，
    # 非 test 路径冒用该 origin 与 test 文件模型操作无 grant 两个负例。
    Case 'fixture-test-fixture-grant-positive' 'PASS' {
        TestFixtureChange ('#include <rw/models/WorkCell.hpp>'+"`n"+'void probe(){ auto cell = std::make_shared<rw::models::WorkCell>("fixture"); cell->addDevice(nullptr); }'+"`n")
        ProbeMatrix 'business-specific' 'migrated'
        $p=GetJson ($reuse+'permissions.json');$g=Grant 'test-fixture' @('query','construct-model','mutate-model') 'sdurws_ird_kinematics_test'
        $g.source.path=$script:probePath;$g.source.sha256=Hash ([IO.File]::ReadAllText((Join-Path $sandbox $g.source.path)))
        $g.categories=@('model')
        $g.build.path=$product+'kinematics/CMakeLists.txt';$g.build.sha256=Hash ([IO.File]::ReadAllText((Join-Path $sandbox $g.build.path)))
        $p.grants=@($g);PutJson ($reuse+'permissions.json') $p
    }
    Case 'fixture-test-fixture-origin-outside-test-rejected' 'REUSE-AD' {
        FunctionalChange '#include <rw/models/WorkCell.hpp>'
        ProbeGrant 'test-fixture' @('model') @('query')
    }
    Case 'fixture-test-model-operation-without-grant-rejected' 'REUSE-AD' {
        TestFixtureChange ('#include <rw/models/WorkCell.hpp>'+"`n"+'void probe(){ auto cell = std::make_shared<rw::models::WorkCell>("fixture"); cell->addDevice(nullptr); }'+"`n")
        $p=GetJson ($reuse+'permissions.json');$p.grants=@();PutJson ($reuse+'permissions.json') $p
    }
    Case 'closure-new-file-no-independent-record' 'REUSE-MIGRATION : new/current file lacks independent closure' { IsolateFinalClosureMatrices;FunctionalChange 'void probe(){}';ProbeMatrix 'business-specific' 'closed' } @('-RequireMigrationComplete')
    Case 'closure-baseline-cannot-use-absent' 'REUSE-CLOSURE' { [void](ClosureFixture);$c=GetJson ($reuse+'closures.json');$c.files[0].baselineBlob='absent';PutJson ($reuse+'closures.json') $c }
    Case 'closure-new-file-cannot-use-blob' 'REUSE-CLOSURE' { [void](ClosureFixture -Replacement);$c=GetJson ($reuse+'closures.json');$c.files[1].baselineBlob=$c.files[0].baselineBlob;PutJson ($reuse+'closures.json') $c }
    Case 'closure-new-file-omitted-final' 'REUSE-MIGRATION : new/current file lacks independent closure' { IsolateFinalClosureMatrices;[void](ClosureFixture -Replacement);$c=GetJson ($reuse+'closures.json');$c.files=@($c.files[0]);PutJson ($reuse+'closures.json') $c } @('-RequireMigrationComplete')
    Case 'closure-mutable-reference-rejected' 'REUSE-CLOSURE' { [void](ClosureFixture);$c=GetJson ($reuse+'closures.json');$c.files[0].migrationCommit='HEAD';PutJson ($reuse+'closures.json') $c }
    Case 'closure-record-path-rejected' 'REUSE-CLOSURE' { [void](ClosureFixture);$c=GetJson ($reuse+'closures.json');$c.files[0].acceptance.path=$reuse+'fixture-production.json';PutJson ($reuse+'closures.json') $c }
    Case 'ad-real-qt-item-positive' 'PASS' {
        $m=MathMatrix;$r=$m.functions[0];$path=$product+'ui/src/CommandPalette.cpp';$txt=[IO.File]::ReadAllText((Join-Path $sandbox $path))
        $m.unit='ui';$r.ownerUnit='ui';$r.functionId='fixture.ui.command-palette';$r.sources=@([pscustomobject]@{path=$path;symbol='CommandPalettePanel::';startLine=1;endLine=@($txt -split "`n").Count;sha256=(Hash $txt)})
        $r.targets=@('sdurws_ird_ui');$build=$product+'ui/CMakeLists.txt';$r.inputs=@([pscustomobject]@{path=$build;sha256=(Hash ([IO.File]::ReadAllText((Join-Path $sandbox $build))))})
        PutJson ($reuse+'functions/ui.json') $m
    }
    Case 'ad-camera-transform-positive' 'PASS' { FunctionalChange 'void probe(){ viewer->setTransform(camera); }';ProbeMatrix }
    Case 'ad-type-name-in-message-positive' 'PASS' { FunctionalChange 'void probe(){ const char* message="WorkCell State"; }';ProbeMatrix }
    Case 'ad-state-data-shared-write-rejected' 'REUSE-AD' { FunctionalChange 'void probe(rw::kinematics::StateData& data, rw::kinematics::State& sharedState){ data.setData(sharedState, values); }';ProbeGrant 'runtime-borrow' @('state') @('query') }
    Case 'ad-state-data-private-write-positive' 'PASS' { FunctionalChange 'void probe(rw::kinematics::StateData& data, rw::kinematics::State& privateState){ data.setData(privateState, values); }';ProbeGrant 'task-private' @('state') @('write-state');ProbeMatrix }
    Case 'ad-bom-script-worktree-positive' 'PASS' { FunctionalChange ([char]0xFEFF+'function probe {}') 'ps1';ProbeMatrix }
    Case 'ad-bom-script-frozen-positive' 'PASS' {
        FunctionalChange ([char]0xFEFF+'function probe {}') 'ps1';ProbeMatrix
        $script:caseCommit=(& git -C $sandbox rev-parse HEAD).Trim()
        & git -C $sandbox add -f -- $reuse ($design+'tasks') $script:probePath
        & git -C $sandbox -c user.name=ReuseFixture -c user.email=fixture@example.invalid commit --quiet -m 'isolated BOM script binding'
        if($LASTEXITCODE){throw 'BOM frozen fixture commit failed'}
    } @('-Head','HEAD')
    # CI/验收读取冻结Git对象，不能仅测试工作树；提交仅存在于将销毁的夹具中。
    & git -C $sandbox add -- $reuse ($design+'tasks/GOV-T21.json') ($design+'tasks/GOV-T22.json') ($design+'tasks/REUSE-T01.json') ($design+'tasks/REUSE-T02.json') ($design+'tasks/REUSE-T03.json') ($design+'tasks/REUSE-T04.json') ($design+'tasks/REUSE-T05.json') ($design+'tasks/REUSE-T06.json') ($product+'CMakeLists.txt')
    if ($LASTEXITCODE) { throw 'fixture stage failed' }
    # 干净CI检出已包含这些治理输入，允许空提交仍能冻结一个真实Git树；不依赖未提交改动。
    & git -C $sandbox -c user.name=ReuseFixture -c user.email=fixture@example.invalid commit --allow-empty --quiet -m 'fixture frozen tree'
    if ($LASTEXITCODE) { throw 'fixture commit failed' }
    Case '31-frozen-head' 'PASS' {} @('-Head','HEAD')
    Case '32-frozen-head-ignores-dirty-tree' 'PASS' { Put ($reuse+'catalog.json') '{}' } @('-Head','HEAD')
    Case '33-frozen-head-repeatable' 'PASS' {} @('-Head','HEAD')
} catch {
    $driverFailure = $_.Exception.Message
} finally {
    $failed=@($script:results | Where-Object { -not $_.passed }).Count
    $notRun = [Math]::Max(0,$expectedTests-$script:results.Count)
    $report=[ordered]@{schemaVersion='ird-reuse-tests/1';fixtureCommit=$fixtureCommit;validatorSha256=$copiedValidatorSha256;helperSha256=$copiedHelperSha256;goldenAssetsSha256=$copiedGoldenSha256;driverSha256=$driverSha256;expected=$expectedTests;executed=$script:results.Count;notRun=$notRun;driverFailure=$driverFailure;passed=($script:results.Count-$failed);skipped=0;failed=$failed;complete=($notRun -eq 0 -and -not $driverFailure);tests=$script:results}
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
