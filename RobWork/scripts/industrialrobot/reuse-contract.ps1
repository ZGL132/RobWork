# validate-reuse.ps1 的私有补强实现；共享 SafePath/ReadText/Json/Digest/Fail/GitLines，
# 没有第二个校验入口。依据 ARCH §3.6 / SA-19 与 reuse/audit-contract.md。
# 本模块只验证可观察结构/摘要/检出事实，语义覆盖及批准真实性仍需独立验收。

function RequireFields($value, [string[]]$names, [string]$code) {
    foreach ($name in $names) {
        if ($null -eq $value -or -not $value.PSObject.Properties[$name] -or
            $null -eq $value.$name -or "$($value.$name)" -eq '') { Fail $code "missing $name" }
    }
}
function ReuseFiles([string]$prefix) {
    if ($Head) { return @(GitLines @('ls-tree','-r','--name-only',$Head,'--',$prefix)) }
    $dir = Join-Path $RepoRoot $prefix
    if (-not (Test-Path -LiteralPath $dir)) { return @() }
    return @(Get-ChildItem -LiteralPath $dir -Recurse -File | ForEach-Object { $_.FullName.Substring($RepoRoot.Length+1).Replace('\','/') })
}
function CheckBinding($b, [string]$code = 'FRESHNESS') {
    RequireFields $b @('path','sha256') $code
    $p = SafePath "$($b.path)"
    if ((Digest (ReadText $p)) -ne $b.sha256) { Fail $code $p }
    return $p
}
function CheckPermissionRef([string]$ref) {
    if ($ref -notin @('ARCH:3.6','ARCH:3.6.1','ARCH:3.6.2','ARCH:3.6.3','ARCH:3.6.4')) { Fail 'PERMISSION-REF' $ref }
    if ((ReadText ($design+'ARCHITECTURE.md')) -notmatch '### 3\.6 .*SA-19') { Fail 'PERMISSION-REF' 'SA-19 authority missing' }
}
function CheckCatalogV2($catalog) {
    foreach ($c in $catalog.capabilities) {
        RequireFields $c @('ownerUnit','usages','operations','resourceSearch','build','limits','review') 'CATALOG-V2'
        if ($c.ownerUnit -notin $units -or $c.review.status -notin @('candidate','reviewed','verified') -or
            "$($c.review.version)" -notmatch '^[0-9a-f]{40}$' -or -not $c.resourceSearch.command -or -not $c.resourceSearch.conclusion) { Fail 'CATALOG-V2' $c.id }
        foreach ($u in $c.usages) {
            if ($u.consumerUnit -notin $units -or $u.usageRole -notin @('owner','adapter') -or -not @($u.operations).Count) { Fail 'CATALOG-V2' $c.id }
            foreach ($op in $u.operations) { if ($op -notin $c.operations) { Fail 'CATALOG-V2' "$($c.id): usage operation $op" } }
            CheckPermissionRef $u.permissionRef
        }
        foreach ($op in $c.operations) { if ($op -notin @('value','query','construct','load','clone','mutate','publish')) { Fail 'CATALOG-V2' "$($c.id): $op" } }
        if (@(Compare-Object @($c.upstream | ForEach-Object { $_.target } | Sort-Object -Unique) @($c.build.targets | Sort-Object -Unique)).Count -or -not $c.build.constraints -or -not $c.limits.semantics -or -not $c.limits.lifetime -or -not $c.limits.determinism) { Fail 'CATALOG-V2' "$($c.id): build/limits" }
        foreach ($kind in @('tests','examples','docs')) {
            if (-not $c.PSObject.Properties[$kind]) { Fail 'CATALOG-V2' "$($c.id): $kind" }
            foreach ($r in $c.$kind) {
                if ($r.path -notmatch '^RobWork/(RobWork|RobWorkSim)/') { Fail 'CATALOG-V2' 'resource outside upstream' }
                [void](CheckBinding $r 'UPSTREAM-HASH')
                foreach ($symbol in $r.symbols) { if (-not (ReadText $r.path).Contains("$symbol")) { Fail 'UPSTREAM-SYMBOL' "$($r.path): $symbol" } }
            }
        }
        # candidate 可保留空资源，但 verified 不能只有声明，必须绑定适用性报告。
        if ($c.review.status -ne 'candidate') {
            RequireFields $c.review @('evidence') 'SUITABILITY'
            [void](CheckBinding $c.review.evidence 'SUITABILITY')
            foreach ($path in @($c.upstream | ForEach-Object { $_.path; $_.targetFile }) | Sort-Object -Unique) {
                $saved=$ErrorActionPreference
                try { $ErrorActionPreference='Continue'; $raw=& git -C $RepoRoot show "$($c.review.version):$path" 2>$null; $rc=$LASTEXITCODE } finally { $ErrorActionPreference=$saved }
                if ($rc -or (Digest ($raw -join "`n")) -ne (Digest (ReadText $path))) { Fail 'SUITABILITY' "$($c.id): reviewed upstream version mismatch" }
            }
        }
    }
}
function CheckPermissions {
    $p = Json ($design+'reuse/permissions.json')
    if ($p.schemaVersion -ne 'ird-reuse-permissions/1' -or $p.authority -ne 'ARCH:3.6' -or $p.modelOwner -ne 'runtime' -or
        -not $p.PSObject.Properties['grants']) { Fail 'PERMISSIONS' 'schema/authority/owner/grants' }
    CheckPermissionRef $p.authority
    # 宿主例外封闭清单（ARCH §3.6.2 机读面）：ID 与文件族在此硬编码逐项比对，
    # permissions.json 只能引用本表 ID 并精确给出对应 .hpp/.cpp 文件面。
    # HOST-WORKBENCH（UiPlugin.{hpp,cpp}）为 2026-10-26 所有者批量批准的扩面
    # 登记（REUSE-T41/F-967 生产装配落地批，ARCHITECTURE.md v0.17 同批）：
    # 宿主插件装配体是 ARCH §3.6.2 呈现装配层（SA-18/B1-SPEC 装配特权边承载
    # 者），其受限面＝宿主类别 include 与借持模型指针、D8 Jog 桥任务私有
    # write-state；不授模型构造/结构写入，运行时模型 owner 红线不变。
    $names = @{'HOST-RENDER'='HostView3DPreviewBackend';'HOST-PRESENT'='HostPresentationAdapters';'HOST-COMPILE'='HostCompilePort';'HOST-GATEWAY'='HostView3DGateway';'HOST-WORKBENCH'='UiPlugin'}
    $hostSeen = @{}
    foreach ($h in $p.hostExceptions) {
        if (-not $names.ContainsKey("$($h.id)") -or $hostSeen.ContainsKey("$($h.id)") -or $h.target -ne 'sdurws_ird_ui_plugin' -or $h.permissionRef -ne 'ARCH:3.6.2') { Fail 'PERMISSIONS' 'unregistered host exception' }
        $hostSeen[$h.id] = $true
        $expected = @(); foreach ($ext in @('hpp','cpp')) { $expected += $product+'ui/plugin/'+$names[$h.id]+'.'+$ext }
        if (@(Compare-Object $expected @($h.files)).Count) { Fail 'PERMISSIONS' "$($h.id): exact files required" }
        foreach ($path in $expected) { [void](ReadText $path) }
    }
    if ($hostSeen.Count -ne 5) { Fail 'PERMISSIONS' 'host exception coverage' }
    $grants = @{}
    foreach ($g in $p.grants) {
        RequireFields $g @('id','source','targets','categories','operations','origin','lifetime','permissionRef','usageRole') 'PERMISSIONS'
        $path = CheckBinding $g.source
        if (-not $path.StartsWith($product) -or $grants.ContainsKey($path) -or $g.usageRole -notin @('owner','adapter')) { Fail 'PERMISSIONS' $path }
        CheckPermissionRef $g.permissionRef
        $unit = ($path.Substring($product.Length) -split '/')[0]
        if ($g.origin -notin @('runtime-owner','runtime-borrow','task-private','host-presentation','value','test-fixture')) { Fail 'PERMISSIONS' "$path : origin" }
        # 测试夹具例外（2026-10-26 所有者落地授权脉络，REUSE-T36 登记）：产品树
        # test/ 与 *_test/（contract_test/gui_test 等单元测试目录惯例，与下方
        # 目标名 _test/_contract_test/_gui_test 约定同口径）路径的测试脚手架
        # 构造/装配模型输入（gtest 夹具建 WorkCell 等）是任务输入装配而非模型
        # 所有权主张——ARCH §3.6/SA-19 的 runtime owner 红线约束产品代码路径，
        # 本例外不放宽之；非测试路径用 test-fixture 一律拒绝，防止借夹具名义
        # 洗白生产代码的模型操作。
        $isTestFixturePath = $path -match '(^|/)([a-z0-9]+_)?test/'
        if ($g.origin -eq 'test-fixture' -and -not $isTestFixturePath) { Fail 'AD' "$path : test-fixture origin outside /test/" }
        if (@($g.categories | Where-Object { $_ -in @('host','graphics') }).Count -and $g.origin -ne 'host-presentation') { Fail 'AD' "$path : presentation requires exact host exception" }
        if ($g.origin -eq 'value' -and (@($g.categories | Where-Object { $_ -ne 'value' }).Count -or @($g.operations | Where-Object { $_ -ne 'value' }).Count)) { Fail 'AD' "$path : value is not an ownership bypass" }
        if ($g.origin -eq 'runtime-owner' -and ($unit -ne 'runtime' -or $g.usageRole -ne 'owner')) { Fail 'AD' "$path : runtime is model owner" }
        if ($g.origin -eq 'host-presentation') {
            $hostMatch = @($p.hostExceptions | Where-Object { $_.id -eq $g.exceptionId -and $path -in $_.files })
            if ($hostMatch.Count -ne 1 -or @($g.targets | Where-Object { $_ -notmatch '_(test|contract_test|gui_test)$' -and $_ -ne $hostMatch[0].target }).Count) { Fail 'AD' "$path : host exception expansion" }
        }
        foreach ($category in $g.categories) { if ($category -notin @('value','model','state','solver','collision','simulation','loader','graphics','host')) { Fail 'PERMISSIONS' $path } }
        foreach ($op in $g.operations) {
            if ($op -notin @('value','query','construct-model','load-model','clone-model','mutate-model','publish-model','write-state','construct-session','render','borrow')) { Fail 'PERMISSIONS' "$path : $op" }
            # 模型所有权操作默认归 runtime-owner；唯一豁免＝test-fixture 且
            # 文件确在 /test/ 路径（见上方例外注释）。
            if ($op -match '^(construct|load|clone|mutate|publish)-model$' -and $g.origin -ne 'runtime-owner' -and -not ($g.origin -eq 'test-fixture' -and $isTestFixturePath)) { Fail 'AD' "$path : model ownership operation $op" }
            if ($op -eq 'write-state' -and $g.origin -notin @('task-private','host-presentation','runtime-owner')) { Fail 'AD' "$path : shared State write" }
        }
        RequireFields $g @('build') 'PERMISSIONS'
        [void](CheckBinding $g.build)
        if ($g.build.path -ne ($product+$unit+'/CMakeLists.txt')) { Fail 'AD' "$path : owning unit CMake required" }
        $cmake = [regex]::Replace((ReadText $g.build.path),'(?m)#[^\r\n]*',' ')
        foreach ($target in $g.targets) {
            if ($target -notmatch ('^sdurws_ird_'+[regex]::Escape($unit)+'($|_)') -or $cmake -notmatch ('\b(add_library|add_executable)\s*\(\s*'+[regex]::Escape($target)+'\b')) { Fail 'AD' "$path : target binding $target" }
        if ($target -match '_worker$' -and (@($g.categories | Where-Object { $_ -in @('host','graphics') }).Count -or $g.origin -eq 'runtime-borrow')) { Fail 'AD' "$path : worker presentation/borrowed handles" }
        }
        # CMake 动态注册与链接归属仍由既有构建门禁/独立接线核对，不能由字符串在场冒充编译证据。
        $grants[$path] = $g
    }
    return $grants
}
function CheckSourcePermission([string]$path, [string]$text, $grants, [switch]$AuditOnly) {
    # 注释不产生 include 权限；宏/别名无法静态解析时必须由功能记录给出完整来源。
    # REUSE-T07：CMake 的说明使用 #，不能把注释中的模型名误当对象操作并要求
    # 伪造模型 grant。仅对实际 CMake 后缀处理；C++ 的 #include 仍完整参与分类。
    if ($path -match '(^|/)CMakeLists\.txt$|\.cmake$') {
        $text = [regex]::Replace($text, '(?m)#[^\r\n]*', ' ')
    }
    $code = [regex]::Replace($text,'(?s)/\*.*?\*/|(?m)//[^\r\n]*',' ')
    $categories = @()
    foreach ($pair in @(@('model','rw/models/'),@('state','rw/kinematics/'),@('solver','rw/(invkin|trajectory|pathplanning)/'),@('collision','rw/proximity/'),@('simulation','rwsim/'),@('loader','rw/loaders/'),@('graphics','(rw/graphics|rwlibs/opengl)/'),@('host','rws/'))) {
        if ($code -cmatch ('(?m)^\s*#\s*include\s*[<"]'+$pair[1])) { $categories += $pair[0] }
    }
    # 类型/操作名出现在文案中不产生对象权限；保留上面已提取的 include 分类。
    $code = [regex]::Replace($code,'"(?:\\.|[^"\\])*"|''(?:\\.|[^''\\])*''',' ')
    if ($code -cmatch '\brw::models::|\b(WorkCell|DynamicWorkCell|RigidDevice)\b') { $categories += 'model' }
    if ($code -cmatch '\brw::kinematics::|\bState\b') { $categories += 'state' }
    if ($code -cmatch '\brw::invkin::|\bIKSolver\b') { $categories += 'solver' }
    if ($code -cmatch '\b(rwsim::|PhysicsEngine)') { $categories += 'simulation' }
    $ops = @()
    if ($code -cmatch '\b(new\s+(?:rw::models::|rwsim::dynamics::)?(?:WorkCell|DynamicWorkCell|SerialDevice|RigidDevice)|(?:make_shared|ownedPtr)\s*<\s*(?:rw::models::|rwsim::dynamics::)?(?:WorkCell|DynamicWorkCell|SerialDevice|RigidDevice))\b|\b(?:create|make|build)(?:WorkCell|DynamicWorkCell|SerialDevice|RigidDevice)\s*\(') { $ops += 'construct-model' }
    if ($code -cmatch '\b(?:WorkCellLoader|DynamicWorkCellLoader)\s*::\s*load') { $ops += 'load-model' }
    if ($code -cmatch '(?:->|\.)\s*(?:addFrame|addDevice|addBody|removeFrame|removeDevice|setMass|setInertia)\s*\(') { $ops += 'mutate-model' }
    if ($code -cmatch '\b(?:cloneWorkCell|cloneDynamicWorkCell)\s*\(') { $ops += 'clone-model' }
    if ($code -cmatch '(?:->|\.)\s*setQ\s*\(') { $ops += 'write-state' }
    # 同名不等于同类型：Qt item::setData 和相机 setTransform 不是关节 State 写。
    # 明确的上游类别/局部 StateData 类型可静态识别；别名/工厂返回值仍交功能来源审查。
    if (@($categories | Where-Object { $_ -in @('model','state') }).Count -and $code -cmatch '(?:->|\.)\s*setTransform\s*\(') { $ops += 'write-state' }
    if ($code -cmatch '\b(?:rw::kinematics::)?StateData::setData\s*\(') { $ops += 'write-state' }
    foreach ($declaration in [regex]::Matches($code,'\b(?:rw::kinematics::)?StateData\s*[*&]?\s*(\w+)')) {
        if ($code -cmatch ('\b'+[regex]::Escape($declaration.Groups[1].Value)+'\s*(?:->|\.)\s*setData\s*\(')) { $ops += 'write-state' }
    }
    if ($code -cmatch '\b(?:new\s+(?:rw::kinematics::)?State|(?:make_shared|ownedPtr)\s*<\s*(?:rw::kinematics::)?State)\b') { $ops += 'construct-session' }
    if (-not $categories.Count -and -not $ops.Count) { return }
    # 未变动基线的未决审计只登记债务，不制造 grant；生产变动与已解决记录永不走此分支。
    if ($AuditOnly) {
        Write-Output "REUSE-AD-PENDING : $path : categories=$($categories -join ',') operations=$($ops -join ','); no permission granted"
        return
    }
    if (-not $grants.ContainsKey($path)) { Fail 'AD' "$path : restricted upstream use needs exact file/target/operation grant (IRD-GATE-AD)" }
    $g = $grants[$path]
    foreach ($c in $categories) { if ($c -notin $g.categories) { Fail 'AD' "$path : category $c (IRD-GATE-AD)" } }
    foreach ($op in $ops) { if ($op -notin $g.operations) { Fail 'AD' "$path : operation $op (IRD-GATE-AD)" } }
}
function CheckSupplementAcceptance($task) {
    $a = $task.reuseGateAcceptance
    RequireFields $a @('commit','path','reviewedCommit') 'SUPPLEMENT-STAGE'
    if ("$($a.commit)" -notmatch '^[a-f0-9]{40}$' -or "$($a.reviewedCommit)" -notmatch '^[a-f0-9]{40}$' -or
        (SafePath $a.path) -notlike ($design+'traceability/acceptance/*.json')) { Fail 'SUPPLEMENT-STAGE' 'acceptance binding' }
    $saved = $ErrorActionPreference
    try { $ErrorActionPreference='Continue'; $raw = & git -C $RepoRoot show "$($a.commit):$($a.path)" 2>$null; $rc=$LASTEXITCODE } finally { $ErrorActionPreference=$saved }
    if ($rc) { Fail 'SUPPLEMENT-STAGE' 'acceptance Git object missing' }
    $record = ($raw -join "`n") | ConvertFrom-Json
    if ($record.taskId -ne 'GOV-T22' -or $record.verdict -ne 'pass' -or $record.reviewedCommit -ne $a.reviewedCommit) { Fail 'SUPPLEMENT-STAGE' 'wrong task/verdict/SHA' }
    $tip='HEAD'; if ($Head) { $tip=$Head }
    & git -C $RepoRoot merge-base --is-ancestor $a.reviewedCommit $tip
    if ($LASTEXITCODE) { Fail 'SUPPLEMENT-STAGE' 'accepted implementation not ancestor' }
}
function CheckProductionEvidence($row, $sources) {
    RequireFields $row @('production','evidence') 'POSITIVE'
    RequireFields $row.production @('entry','upstream','consumer','target') 'POSITIVE'
    if ($row.production.target -notin $row.targets -or $row.production.entry -notin @($sources.symbol) -or $row.production.consumer -notin @($sources.symbol)) { Fail 'POSITIVE' 'demo-only or unbound production entry/consumer' }
    $cap=$caps[$row.capabilityId]
    if ($cap.review.status -ne 'verified' -or $row.production.upstream -notin @($cap.upstream | ForEach-Object { $_.symbols })) { Fail 'SUITABILITY' $row.functionId }
    [void](CheckBinding $row.evidence 'POSITIVE')
    $report=Json $row.evidence.path
    RequireFields $report @('schemaVersion','functionId','target','bindings','normal','disconnected','oracle','upstreamComparison') 'POSITIVE'
    if ($report.schemaVersion -ne 'ird-reuse-production/1' -or $report.functionId -ne $row.functionId -or $report.target -ne $row.production.target) { Fail 'POSITIVE' 'identity/target' }
    $bound=@{}; foreach ($binding in $report.bindings) { $p=CheckBinding $binding; $bound[$p]=$true }
    $required=@($sources.path)+@($row.inputs.path)+@($cap.upstream | ForEach-Object { $_.path; $_.targetFile })
    foreach ($p in $required) { if (-not $bound.ContainsKey($p)) { Fail 'FRESHNESS' "missing production dependency $p" } }
    foreach ($kind in @('normal','disconnected','oracle','upstreamComparison')) {
        $run=$report.$kind
        RequireFields $run @('command','xml','log','exitCode') 'POSITIVE'
        [void](CheckBinding $run.log 'POSITIVE'); [void](CheckBinding $run.xml 'POSITIVE')
        try { $xml=[xml](ReadText $run.xml.path) } catch { Fail 'POSITIVE' 'invalid XML' }
        $suites=@($xml.SelectNodes('//testsuite')); $tests=0; $errors=0; $failures=0; $skips=0
        foreach ($suite in $suites) { $tests += [int]$suite.tests; $errors += [int]$suite.errors; $failures += [int]$suite.failures; $skips += [int]$suite.skipped; $skips += [int]$suite.disabled }
        # 同时核对真实 testcase 节点，拒绝只有 tests=1 的空壳报告；摘要只能防过期，不能证明报告作者身份。
        $cases=@($xml.SelectNodes('//testcase'))
        if ($cases.Count -ne $tests -or @($xml.SelectNodes('//testcase[failure]')).Count -ne $failures -or
            @($xml.SelectNodes('//testcase/error')).Count -or @($xml.SelectNodes('//testcase/skipped')).Count) { Fail 'POSITIVE' "$kind : XML counts/assertions inconsistent" }
        if ($tests -lt 1 -or $skips -or $errors) { Fail 'POSITIVE' "$kind : incomplete/non-test failure" }
        if ($kind -eq 'disconnected') {
            RequireFields $run @('mutation','mutatedSource') 'POSITIVE'
            [void](CheckBinding $run.mutatedSource 'POSITIVE')
            RequireFields $run.mutation @('source','originalSha256','description') 'POSITIVE'
            if ($run.mutation.source -notin @($sources.path) -or
                (Digest (ReadText $run.mutation.source)) -ne $run.mutation.originalSha256 -or
                $run.mutatedSource.sha256 -eq $run.mutation.originalSha256) { Fail 'POSITIVE' 'disconnect must bind and change production source' }
            if ($run.exitCode -eq 0 -or $failures -lt 1) { Fail 'POSITIVE' 'disconnect must fail a real assertion' }
        } elseif ($run.exitCode -ne 0 -or $failures) { Fail 'POSITIVE' "$kind : failing baseline/oracle/comparison" }
    }
}
function CheckFunctions($grants, [string[]]$changedSources) {
    $covered=@{}; $ids=@{}; $fileFunctions=@{}; $rows=@{}
    foreach ($file in (ReuseFiles ($design+'reuse/functions/'))) {
        if (-not $file.EndsWith('.json')) { Fail 'FUNCTION' "unexpected matrix file $file" }
        $matrix=Json $file
        if ($matrix.schemaVersion -ne 'ird-reuse-functions/1' -or $matrix.unit -notin @($units+'foundation-governance') -or
            $file -ne ($design+'reuse/functions/'+$matrix.unit+'.json') -or -not $matrix.PSObject.Properties['functions']) { Fail 'FUNCTION' $file }
        foreach ($r in $matrix.functions) {
            RequireFields $r @('functionId','ownerUnit','requirements','sources','usageRole','permissionRef','capabilityId','decision','status','search','differences') 'FUNCTION'
            $key="$($r.functionId)|$($r.capabilityId)"
            if ($ids.ContainsKey($key) -or $r.ownerUnit -ne $matrix.unit -or $r.usageRole -notin @('owner','adapter') -or
                $r.decision -notin @('direct','adapter','business-specific','pending','proposal') -or $r.status -notin @('audited','pending','proposal','migrated','closed') -or
                ($r.capabilityId -ne 'NONE' -and -not $caps.ContainsKey($r.capabilityId)) -or -not $r.search.command -or -not $r.search.conclusion) { Fail 'FUNCTION' $key }
            if ($r.capabilityId -eq 'NONE' -and $r.decision -notin @('business-specific','pending','proposal')) { Fail 'FUNCTION' 'NONE cannot claim upstream reuse' }
            if ($null -eq $r.PSObject.Properties['execution'] -and $r.status -in @('migrated','closed') -and $r.decision -in @('pending','proposal')) { Fail 'FUNCTION' 'unresolved cannot migrate' }
            CheckPermissionRef $r.permissionRef; $ids[$key]=$true; $rows[$key]=$r
            # 治理脚本是解释执行关系，没有CMake编译目标；只允许精确脚本目录
            # 的待审计登记，不能通过execution标签绕过产品源/权限或伪称迁移关闭。
            # 结项仍由最终模式拒绝pending/proposal，不把格式支持当治理语义PASS。
            $scriptAudit=$null -ne $r.PSObject.Properties['execution']
            if ($scriptAudit) {
                RequireFields $r.execution @('kind','entry','command') 'EXECUTION'
                $entry=SafePath "$($r.execution.entry)"
                if ($matrix.unit -ne 'foundation-governance' -or $r.usageRole -ne 'owner' -or
                    $r.execution.kind -ne 'governance-script' -or
                    $entry -notmatch '^RobWork/scripts/industrialrobot/[^/]+\.ps1$' -or
                    -not "$($r.execution.command)".Contains($entry) -or
                    $r.capabilityId -ne 'NONE' -or $r.decision -notin @('pending','proposal') -or
                    $r.status -notin @('pending','proposal') -or @($r.targets).Count -ne 0 -or
                    -not @($r.sources).Count -or $entry -notin @($r.inputs.path)) { Fail 'EXECUTION' $r.functionId }
                foreach ($s in $r.sources) {
                    # 允许真实入口dot-source的同目录私有helper；两者都须完整摘要绑定。
                    # 具体dot-source调用链由独立语义验收核对，字符串命中不是执行证明。
                    if ($s.path -notmatch '^RobWork/scripts/industrialrobot/[^/]+\.ps1$' -or $s.path -notin @($r.inputs.path)) { Fail 'EXECUTION' 'source/entry/context must bind exact governance scripts' }
                }
            } else {
                RequireFields $r @('targets','inputs') 'FUNCTION'
            }
            foreach ($input in $r.inputs) { [void](CheckBinding $input) }
            foreach ($target in $r.targets) {
                $registered=$false
                foreach ($input in $r.inputs) {
                    if ($input.path -match '(^|/)CMakeLists\.txt$|\.cmake$' -and ([regex]::Replace((ReadText $input.path),'(?m)#[^\r\n]*',' ')) -match ('\b(add_library|add_executable)\s*\(\s*'+[regex]::Escape($target)+'\b')) { $registered=$true }
                }
                if (-not $registered) { Fail 'FUNCTION' "$key : target needs bound CMake registration $target" }
            }
            foreach ($s in $r.sources) {
                RequireFields $s @('path','symbol','startLine','endLine','sha256') 'FUNCTION'
                $path=SafePath $s.path; $txt=ReadText $path
                if ((-not $scriptAudit -and -not $path.StartsWith($product)) -or [int]$s.startLine -lt 1 -or [int]$s.endLine -lt [int]$s.startLine) { Fail 'FUNCTION' $path }
                $lines=@(($txt -replace "`r`n","`n") -split "`n")
                if ([int]$s.endLine -gt $lines.Count) { Fail 'FUNCTION' "$path : region bounds" }
                $region=($lines[([int]$s.startLine-1)..([int]$s.endLine-1)] -join "`n")
                if ((Digest $region) -ne $s.sha256 -or -not $region.Contains("$($s.symbol)")) { Fail 'FRESHNESS' "$path : region/symbol" }
                if (-not $covered.ContainsKey($path)) { $covered[$path]=@{}; $fileFunctions[$path]=@() }
                foreach ($n in ([int]$s.startLine)..([int]$s.endLine)) { $covered[$path][$n]=$true }
                $fileFunctions[$path]+=$key
                $unresolvedBaseline=($tree.ContainsKey($path) -and $path -notin $changedSources -and
                    $r.status -in @('pending','proposal') -and $r.decision -in @('pending','proposal'))
                if ($RequireMigrationComplete -and $unresolvedBaseline) { Fail 'MIGRATION' "unresolved function $($r.functionId)" }
                if (-not $scriptAudit) { CheckSourcePermission $path $region $grants -AuditOnly:$unresolvedBaseline }
            }
            if ($r.replaces) {
                foreach ($old in $r.replaces) { $old=SafePath $old; if (-not $tree.ContainsKey($old)) { Fail 'FUNCTION' "unknown replaced baseline $old" } }
            }
            if ($r.decision -eq 'business-specific') { RequireFields $r @('nonOverlapEvidence') 'FUNCTION'; [void](CheckBinding $r.nonOverlapEvidence) }
            if ($r.decision -eq 'proposal') { RequireFields $r @('proposal') 'FUNCTION'; [void](CheckBinding $r.proposal) }
            if ($r.status -in @('migrated','closed') -and $r.decision -in @('direct','adapter')) { CheckProductionEvidence $r $r.sources }
        }
    }
    foreach ($path in $changedSources) {
        $txt=ReadText $path -Optional; if ($null -eq $txt) { continue }
        CheckSourcePermission $path $txt $grants
        if (-not $covered.ContainsKey($path)) { Fail 'COVERAGE' "$path : no functional matrix" }
        foreach ($key in $fileFunctions[$path]) {
            $row=$rows[$key]
            if ($row.decision -in @('pending','proposal') -or $row.status -notin @('migrated','closed')) { Fail 'COVERAGE' "$path : changed production function needs resolved migration evidence" }
        }
        # 要求改动文件的每个非空代码/注释行有区段归属，避免只登记公共入口而遗漏 helper。
        # 区段是否确为一个语义功能由独立审计核对，不把行覆盖率当语义完备证明。
        $n=0; foreach ($line in (($txt -replace "`r`n","`n") -split "`n")) { $n++; if ($line.Trim() -and -not $covered[$path].ContainsKey($n)) { Fail 'COVERAGE' "$path : uncovered line $n" } }
    }
    $closurePath=$design+'reuse/closures.json'
    $closureText=ReadText $closurePath -Optional; $closed=@{}
    if ($null -ne $closureText) {
        $closures=$closureText|ConvertFrom-Json
        if ($closures.schemaVersion -ne 'ird-reuse-closures/1' -or -not $closures.PSObject.Properties['files']) { Fail 'CLOSURE' 'schema' }
        foreach ($c in $closures.files) {
            RequireFields $c @('path','baselineBlob','currentSha256','functionKeys','migrationCommit','acceptance') 'CLOSURE'
            $path=SafePath $c.path
            if ($closed.ContainsKey($path) -or -not (IsProduct $path)) { Fail 'CLOSURE' $path }
            if ($tree.ContainsKey($path)) { if ($tree[$path] -ne $c.baselineBlob) { Fail 'CLOSURE' "$path : baseline blob" } }
            elseif ($c.baselineBlob -ne 'absent') { Fail 'CLOSURE' "$path : new file requires absent baseline" }
            $current=ReadText $path -Optional; $hash='deleted'; if ($null -ne $current) { $hash=Digest $current }
            if (-not $tree.ContainsKey($path) -and $null -eq $current) { Fail 'CLOSURE' "$path : new file must exist" }
            if ($hash -ne $c.currentSha256) { Fail 'CLOSURE' "$path : current hash" }
            foreach ($key in $c.functionKeys) {
                if (-not $rows.ContainsKey($key) -or $rows[$key].status -ne 'closed' -or
                    ($path -notin @($rows[$key].sources.path) -and -not ($null -eq $current -and $path -in @($rows[$key].replaces)))) { Fail 'CLOSURE' "$path : functional closure" }
            }
            # 存在的文件必须完整关联已关闭区段；删除文件允许指向明确 replaces 的新实现，不要求复活旧源码。
            if ($null -ne $current) {
                if (-not $covered.ContainsKey($path)) { Fail 'CLOSURE' "$path : no current functional coverage" }
                foreach ($key in $fileFunctions[$path]) { if ($key -notin $c.functionKeys) { Fail 'CLOSURE' "$path : omitted function" } }
                $n=0; foreach ($line in (($current -replace "`r`n","`n") -split "`n")) { $n++; if ($line.Trim() -and -not $covered[$path].ContainsKey($n)) { Fail 'CLOSURE' "$path : uncovered line $n" } }
            }
            RequireFields $c.acceptance @('commit','path','reviewedCommit') 'CLOSURE'
            if ("$($c.migrationCommit)" -notmatch '^[0-9a-f]{40}$' -or "$($c.acceptance.commit)" -notmatch '^[0-9a-f]{40}$' -or
                "$($c.acceptance.reviewedCommit)" -notmatch '^[0-9a-f]{40}$' -or (SafePath $c.acceptance.path) -notlike ($design+'traceability/acceptance/*.json')) { Fail 'CLOSURE' 'frozen commit and acceptance path required' }
            $recordRef=$c.acceptance; $saved=$ErrorActionPreference
            try { $ErrorActionPreference='Continue'; $raw=& git -C $RepoRoot show "$($recordRef.commit):$(SafePath $recordRef.path)" 2>$null; $rc=$LASTEXITCODE } finally { $ErrorActionPreference=$saved }
            if ($rc) { Fail 'CLOSURE' 'acceptance object missing' }
            $a=($raw -join "`n")|ConvertFrom-Json
            if ($a.verdict -ne 'pass' -or $a.reviewedCommit -ne $recordRef.reviewedCommit -or $a.reviewedCommit -ne $c.migrationCommit -or $a.taskId -notlike 'REUSE-*') { Fail 'CLOSURE' 'independent migration acceptance required' }
            $tip='HEAD'; if ($Head) { $tip=$Head }; & git -C $RepoRoot merge-base --is-ancestor $c.migrationCommit $tip
            if ($LASTEXITCODE) { Fail 'CLOSURE' 'migration not ancestor' }
            # 独立验收必须针对实际关闭内容，而非任意祖先的 pass；当前文件/替代文件与被验版本须一致。
            $acceptedPaths=@($path)
            foreach ($key in $c.functionKeys) {
                $row=$rows[$key]
                $acceptedPaths+=@($row.sources.path)+@($row.inputs.path)
                foreach ($binding in @($row.evidence,$row.nonOverlapEvidence,$row.proposal)) { if ($binding) { $acceptedPaths+=$binding.path } }
                if ($row.evidence) {
                    $report=Json $row.evidence.path
                    $acceptedPaths+=@($report.bindings.path)
                    foreach ($kind in @('normal','disconnected','oracle','upstreamComparison')) { $acceptedPaths+=@($report.$kind.xml.path,$report.$kind.log.path,$report.$kind.mutatedSource.path) }
                }
                if ($caps.ContainsKey($row.capabilityId)) { $cap=$caps[$row.capabilityId];$acceptedPaths+=@($cap.upstream | ForEach-Object { $_.path; $_.targetFile });if ($cap.review.evidence) { $acceptedPaths+=$cap.review.evidence.path } }
                # 比较同一功能记录；仅允许验收后的 migrated→closed 状态推进，不能更换来源/证据/适配边界。
                $matrixPath=$design+'reuse/functions/'+$row.ownerUnit+'.json'
                $saved=$ErrorActionPreference
                try { $ErrorActionPreference='Continue'; $raw=& git -C $RepoRoot show "$($c.migrationCommit):$matrixPath" 2>$null;$rc=$LASTEXITCODE } finally { $ErrorActionPreference=$saved }
                if ($rc) { Fail 'CLOSURE' "$key : accepted matrix missing" }
                $acceptedMatrix=($raw -join "`n")|ConvertFrom-Json
                $oldRow=@($acceptedMatrix.functions | Where-Object { "$($_.functionId)|$($_.capabilityId)" -eq $key })
                if ($oldRow.Count -ne 1 -or $oldRow[0].status -notin @('migrated','closed')) { Fail 'CLOSURE' "$key : accepted migrated function missing" }
                $copy=($row | ConvertTo-Json -Depth 40)|ConvertFrom-Json
                $copy.PSObject.Properties.Remove('status');$oldRow[0].PSObject.Properties.Remove('status')
                if (($copy|ConvertTo-Json -Depth 40 -Compress) -ne ($oldRow[0]|ConvertTo-Json -Depth 40 -Compress)) { Fail 'CLOSURE' "$key : accepted function changed" }
            }
            foreach ($closedPath in $acceptedPaths | Where-Object { $_ } | Sort-Object -Unique) {
                $saved=$ErrorActionPreference
                try { $ErrorActionPreference='Continue'; $raw=& git -C $RepoRoot show "$($c.migrationCommit):$closedPath" 2>$null; $rc=$LASTEXITCODE } finally { $ErrorActionPreference=$saved }
                $now=ReadText $closedPath -Optional
                if ($rc) { if ($null -ne $now) { Fail 'CLOSURE' "$closedPath : absent from accepted version" } }
                elseif ($null -eq $now -or (Digest ($raw -join "`n")) -ne (Digest $now)) { Fail 'CLOSURE' "$closedPath : accepted version mismatch" }
            }
            $closed[$path]=$true
        }
    }
    if ($RequireMigrationComplete) {
        foreach ($path in $changedSources) {
            if ($null -ne (ReadText $path -Optional) -and -not $closed.ContainsKey($path)) { Fail 'MIGRATION' "new/current file lacks independent closure: $path" }
        }
        foreach ($path in $tree.Keys) { if (-not $closed.ContainsKey($path)) { Fail 'MIGRATION' "unclosed baseline $path" } }
        foreach ($r in $rows.Values) { if ($r.status -ne 'closed') { Fail 'MIGRATION' "unresolved function $($r.functionId)" } }
        foreach ($path in $changedSources) { foreach ($key in $fileFunctions[$path]) { if ($rows[$key].status -ne 'closed') { Fail 'MIGRATION' "$path : not closed" } } }
    }
    Write-Output "reuse supplement: schema/permission/function evidence checked; functions=$($rows.Count) closedFiles=$($closed.Count) (semantic review required)"
}
