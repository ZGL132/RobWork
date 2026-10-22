# H1—H6 的唯一机器校验入口。仅校验可机器证明的事实；语义等价和批准身份仍须独立审查。
# RepoRoot 为 Git 根；Head 指定时所有输入取自同一 Git 树，避免验收混入工作树内容。
param([string]$RepoRoot = '', [string]$Head = '', [string]$TaskFile = '', [switch]$RequireMigrationComplete)
$ErrorActionPreference = 'Stop'
# PS5.1 在 MSBuild/CI 重定向环境中默认按系统代码页解码 Git stdout，可能破坏中文 JSON。
# Git 对象采用 UTF-8；显式统一 native 交互编码，避免把解码故障误归为证据损坏。
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$OutputEncoding = [Console]::OutputEncoding
if (-not $RepoRoot) { $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path }
$RepoRoot = [IO.Path]::GetFullPath($RepoRoot)
$baseline = 'b12e8503cc1c2a1acc040f506842a02e126b9515'
$product = 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/'
$design = 'RobWork/doc/industrial-robot-design/'
function Fail([string]$code, [string]$detail) { throw "REUSE-$code : $detail" }
function SafePath([string]$path) {
    if (-not $path -or $path -match '[\\:*?\[\]]|(^|/)\.\.?(/|$)|^/' -or $path.Contains('//')) { Fail 'PATH' $path }
    return $path
}
function ReadText([string]$path, [switch]$Optional) {
    $path = SafePath $path
    if ($Head) {
        # Windows PS5.1 把 native stderr 包装成错误；这里按 Git 退出码归因，避免吞掉具名诊断。
        $savedPreference = $ErrorActionPreference
        try { $ErrorActionPreference = 'Continue'; $v = & git -C $RepoRoot show "${Head}:$path" 2>$null; $gitCode = $LASTEXITCODE } finally { $ErrorActionPreference = $savedPreference }
        if ($gitCode -ne 0) { if ($Optional) { return $null }; Fail 'MISSING' $path }
        return (($v -join "`n") + "`n").TrimStart([char]0xFEFF)
    }
    $full = Join-Path $RepoRoot $path
    if (-not (Test-Path -LiteralPath $full -PathType Leaf)) { if ($Optional) { return $null }; Fail 'MISSING' $path }
    # 不跟随产品外的符号链接/junction 来逃逸仓库。
    $item = Get-Item -LiteralPath $full
    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { Fail 'PATH' $path }
    $parent = $item.Directory
    while ($parent -and $parent.FullName.Length -gt $RepoRoot.Length) {
        if ($parent.Attributes -band [IO.FileAttributes]::ReparsePoint) { Fail 'PATH' $path }
        $parent = $parent.Parent
    }
    return ([IO.File]::ReadAllText($full)).TrimStart([char]0xFEFF)
}
function Digest([string]$text) {
    # 已验版本比较也会直接接收 git show 文本；摘要入口统一规范化，避免绕过 ReadText。
    $bytes = [Text.Encoding]::UTF8.GetBytes(($text.TrimStart([char]0xFEFF) -replace "`r`n", "`n").TrimEnd("`n") + "`n")
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant() } finally { $sha.Dispose() }
}
function Json([string]$path) { return ((ReadText $path) | ConvertFrom-Json) }
# 私有数据契约函数复用本入口的冻结树读取/规范化摘要；禁止另起可绕过的校验入口。
. (Join-Path $PSScriptRoot 'reuse-contract.ps1')
function GitLines([string[]]$arguments) {
    $v = @(& git -C $RepoRoot @arguments)
    if ($LASTEXITCODE -ne 0) { Fail 'GIT' ($arguments -join ' ') }
    return $v
}
# 同一谓词用于基线库存和新文件检查，不能通过后缀把算法藏进C/模板/脚本/页面。
# 这是源文件发现范围，语义查重仍由独立审查完成，不宣称能识别任意改名算法。
function IsProduct([string]$p) { return $p.StartsWith($product) -and ($p -match '\.(c|cpp|hpp|h|cc|cxx|hxx|ipp|inl|tpp|ixx|m|mm|cu|cuh|cmake|py|ps1|psm1|psd1|js|mjs|cjs|ts|tsx|jsx|html|htm|vue|svelte|rs|go|cs|java|lua|jl|r|rb|swift|kt|sh|bat|cmd)$' -or $p.EndsWith('/CMakeLists.txt')) }
function CheckAcceptance($t) {
    $a = $t.governanceAcceptance
    if (-not $a -or "$($a.commit)" -notmatch '^[0-9a-f]{40}$' -or "$($a.reviewedCommit)" -notmatch '^[0-9a-f]{40}$') { Fail 'STAGE' "$($t.taskId): no frozen governance acceptance" }
    $p = SafePath "$($a.path)"
    if (-not $p.StartsWith($design + 'traceability/acceptance/')) { Fail 'STAGE' 'acceptance path' }
    $savedPreference = $ErrorActionPreference
    try { $ErrorActionPreference = 'Continue'; $raw = & git -C $RepoRoot show "$($a.commit):$p" 2>$null; $gitCode = $LASTEXITCODE } finally { $ErrorActionPreference = $savedPreference }
    if ($gitCode -ne 0) { Fail 'STAGE' 'acceptance object missing' }
    $r = ($raw -join "`n") | ConvertFrom-Json
    if ($r.taskId -ne 'GOV-T21' -or $r.verdict -ne 'pass' -or $r.reviewedCommit -ne $a.reviewedCommit) { Fail 'STAGE' 'acceptance verdict/sha mismatch' }
    $tip = 'HEAD'; if ($Head) { $tip = $Head }
    & git -C $RepoRoot merge-base --is-ancestor $a.reviewedCommit $tip
    if ($LASTEXITCODE -ne 0) { Fail 'STAGE' 'accepted governance is not ancestor' }
}
function CheckTask($t) {
    if ($t.status -notin @('ready','done')) { Fail 'TASK-STATE' "$($t.taskId): execution needs ready/done contract" }
    if (-not $t.reuse -or $t.reuse.policy -ne 'H1-H6' -or -not $t.reuse.manifest) { Fail 'TASK' "$($t.taskId): reuse contract required" }
    if ($t.taskId -ne 'GOV-T21') { CheckAcceptance $t }
    if ($t.taskId -eq 'GOV-T22') {
        # 治理补强仅能修改既有门禁、数据和文档；不能通过新增 bootstrap 任务自批产品算法。
        $allowed = @('RobWork/scripts/industrialrobot/validate-reuse.ps1','RobWork/scripts/industrialrobot/reuse-contract.ps1','RobWork/scripts/industrialrobot/test-reuse-governance.ps1',
            'RobWork/doc/industrial-robot-design/reuse/catalog.json','RobWork/doc/industrial-robot-design/reuse/permissions.json','RobWork/doc/industrial-robot-design/reuse/migration.md',
            'RobWork/doc/industrial-robot-design/reuse/audit-contract.md','RobWork/doc/industrial-robot-design/reuse/policy.md','RobWork/doc/industrial-robot-design/reuse/changes/GOV-T22.json',
            'RobWork/doc/industrial-robot-design/tasks/GOV-T22.json','.github/workflows/upstream-reuse-governance.yml','RobWork/scripts/industrialrobot/ci/upstream-reuse-governance.github-workflow.yml',
            ($product+'cmake/ird_gates.cmake'))
        foreach ($path in $t.allowedFiles) { if ($path -notin $allowed) { Fail 'GOV-SCOPE' "GOV-T22 cannot authorize $path" } }
    } elseif ($t.taskId -ne 'GOV-T21') { CheckSupplementAcceptance $t }
    if ($t.reuse.manifest -ne ($design+'reuse/changes/'+$t.taskId+'.json')) { Fail 'MANIFEST' "$($t.taskId): canonical manifest required" }
    $proof = Json $t.reuse.manifest
    if ($proof.taskId -ne $t.taskId -or $proof.schemaVersion -ne 'ird-reuse-change/1' -or -not $proof.PSObject.Properties['files']) { Fail 'MANIFEST' "$($t.taskId): missing manifest identity/files" }
}
try {
    if ($Head) { $Head = @(GitLines @('rev-parse', '--verify', "${Head}^{commit}"))[0] }
    $catalog = Json ($design + 'reuse/catalog.json')
    $inventory = Json ($design + 'reuse/inventory.json')
    if ($catalog.schemaVersion -ne 'ird-reuse/2' -or $inventory.schemaVersion -ne 'ird-reuse-inventory/1' -or $catalog.baselineCommit -ne $baseline -or $inventory.baselineCommit -ne $baseline) { Fail 'BASELINE' 'fixed baseline/schema mismatch (catalog v2 required; no silent downgrade)' }
    $units = @('core','diagnostics','drivetrain','dynamics','evidence','execution','io','kinematics','modeling','optimization','policy','project','reporting','requirements','runtime','selection','testkit','trajectory','ui','workflow')
    if (@(Compare-Object $units @($catalog.units)).Count) { Fail 'UNITS' 'all 20 units required' }
    $caps = @{}
    foreach ($c in $catalog.capabilities) {
        if (-not $c.id -or $caps.ContainsKey($c.id) -or -not $c.search -or -not $c.boundary -or @($c.upstream).Count -eq 0) { Fail 'CATALOG' "$($c.id)" }
        $caps[$c.id] = $c
        foreach ($u in $c.upstream) {
            if ("$($u.path)" -notmatch '^RobWork/(RobWork|RobWorkSim)/(src|gtest)/' -or -not $u.target -or @($u.symbols).Count -eq 0) { Fail 'UPSTREAM' "$($c.id): path/target/symbols" }
            $txt = ReadText $u.path
            if ((Digest $txt) -ne $u.sha256) { Fail 'UPSTREAM-HASH' $u.path }
            foreach ($s in $u.symbols) { if (-not $txt.Contains("$s")) { Fail 'UPSTREAM-SYMBOL' "$($u.path): $s" } }
            # 测试入口/夹具亦须优先复用；仅允许两上游既有gtest根目标登记，仍拒绝任意路径。
            if ("$($u.targetFile)" -notmatch '^RobWork/(RobWork|RobWorkSim)/(src/.+|gtest)/CMakeLists\.txt$') { Fail 'UPSTREAM-TARGET' "$($c.id): target evidence required" }
            $targetText = ReadText $u.targetFile
            if ((Digest $targetText) -ne $u.targetSha256 -or -not $targetText.Contains("$($u.target)")) { Fail 'UPSTREAM-TARGET' "$($u.target): build registration missing/stale" }
        }
    }
    CheckCatalogV2 $catalog
    $grants = CheckPermissions
    $tree = @{}; $allBaselineProduct = @{}
    foreach ($line in (GitLines @('ls-tree','-r',$baseline,'--',$product))) {
        if ($line -match '^\d+ blob ([a-f0-9]+)\t(.+)$') {
            $blob = $Matches[1]; $path = $Matches[2]; $allBaselineProduct[$path] = $blob
            if (IsProduct $path) { $tree[$path] = $blob }
        }
    }
    $seen = @{}; $debt = 0; $audit = 0
    # 这份库存是固定历史事实。不能仅改 classification 把未迁移文件洗成“已有适配”。
    $knownDebt = @('kinematics/src/Fk.cpp','kinematics/src/Ik.cpp','dynamics/src/InverseDynamics.cpp','dynamics/src/ForwardDynamics.cpp','modeling/src/CouplingMath.hpp','runtime/src/DescriptionValidator.cpp','drivetrain/src/MappingCore.cpp','runtime/src/BaseWorldTransform.cpp','modeling/src/CanonicalBridge.cpp','modeling/src/DhConvert.cpp','modeling/src/RpyMath.hpp','kinematics/src/Sampling.cpp','trajectory/src/Ptp.cpp','trajectory/src/CartesianLine.cpp','trajectory/src/Smooth.cpp','trajectory/src/TimeParam.cpp')
    $knownAdapters = @('trajectory/src/Planner.cpp','policy/src/RobWorkCollisionEvaluator.cpp')
    foreach ($e in $inventory.files) {
        $p = SafePath "$($e.path)"
        if ($seen.ContainsKey($p) -or -not $tree.ContainsKey($p) -or $tree[$p] -ne $e.baselineBlob) { Fail 'INVENTORY' $p }
        $seen[$p] = $true
        $unit = ($p.Substring($product.Length) -split '/')[0]
        if ($unit -notin $units) { $unit = 'foundation-governance' }
        if ($e.unit -ne $unit -or $e.classification -notin @('migration-debt','audit-required','reuse-candidate','existing-adapter') -or -not $e.migrationTask) { Fail 'INVENTORY' "classification/unit/task: $p" }
        $expected = 'audit-required'; $rel = $p.Substring($product.Length)
        if ($rel -in $knownDebt) { $expected = 'migration-debt' }
        if ($rel -in $knownAdapters) { $expected = 'existing-adapter' }
        if ($e.classification -ne $expected) { Fail 'INVENTORY' "baseline classification is immutable: $p" }
        foreach ($c in $e.capabilities) { if (-not $caps.ContainsKey($c)) { Fail 'CAPABILITY' "$p : $c" } }
        if ($e.classification -eq 'migration-debt') { $debt++ }
        if ($e.classification -ne 'existing-adapter') { $audit++ }
    }
    if ($seen.Count -ne $tree.Count) { Fail 'INVENTORY' "coverage $($seen.Count)/$($tree.Count)" }
    # 基线债务不可通过直接删库存关闭。终验需另行批准迁移结项数据协议，不能让默认门禁静默放行。
    # 固定库存不改分类；最终模式由 CheckFunctions 的独立关闭记录判定，不能永远锁死历史计数。
    $tip = 'HEAD'; if ($Head) { $tip = $Head }
    $changed = @(GitLines @('diff','--name-only',$baseline,$tip,'--',$product))
    $manifestPaths = @(GitLines @('ls-tree','-r','--name-only',$tip,'--',($design+'reuse/changes/')))
    if (-not $Head) {
        $changed += @(GitLines @('diff','--name-only','HEAD','--',$product))
        $changed += @(GitLines @('ls-files','--others','--exclude-standard','--',$product))
        $manifestPaths = @(Get-ChildItem (Join-Path $RepoRoot ($design+'reuse/changes')) -Filter '*.json' | ForEach-Object { $design+'reuse/changes/'+$_.Name })
    }
    $records = @{}; $governanceFiles = @{}; $governanceBootstrap = @{}
    foreach ($mp in $manifestPaths) {
        if (-not $mp.EndsWith('.json')) { continue }
        $m = Json $mp
        if (-not $m.taskId -or -not $m.taskFile -or -not $m.PSObject.Properties['files']) { Fail 'MANIFEST' $mp }
        $t = Json $m.taskFile
        if ($t.taskId -ne $m.taskId -or $t.reuse.manifest -ne $mp) { Fail 'TASK' $mp }
        CheckTask $t
        foreach ($f in $m.files) {
            $p = SafePath "$($f.path)"
            if (-not (IsProduct $p) -or $f.mode -notin @('governance','direct','adapter','business','remove','extension','new-core') -or -not $f.boundary -or -not $f.verify) { Fail 'MANIFEST' $p }
            if (-not @($t.allowedFiles | Where-Object { $p -like $_ }).Count -or @($t.forbiddenFiles | Where-Object { $p -like $_ }).Count) { Fail 'SCOPE' $p }
            if ($f.mode -eq 'governance' -and -not ($p.EndsWith('/CMakeLists.txt') -or $p.EndsWith('.cmake'))) { Fail 'MODE' "governance cannot cover algorithms: $p" }
            if ($f.mode -in @('direct','adapter','extension','new-core') -and @($f.capabilities).Count -eq 0) { Fail 'CAPABILITY' $p }
            foreach ($c in $f.capabilities) { if (-not $caps.ContainsKey($c)) { Fail 'CAPABILITY' "$p : $c" } }
            if ($f.mode -in @('extension','new-core')) {
                $a = Json (SafePath "$($f.approval)")
                if ($a.taskId -ne $m.taskId -or -not $a.ownerQuote -or -not $a.date -or $p -notin @($a.allowedFiles) -or (Digest (ReadText $a.proposal)) -ne $a.proposalSha256) { Fail 'APPROVAL' $p }
            }
            if (-not $records.ContainsKey($p)) { $records[$p] = @() }
            $records[$p] += $f
            if ($m.taskId -eq 'GOV-T21') { $governanceFiles[$p] = $true }
            if ($m.taskId -in @('GOV-T21','GOV-T22') -and $f.mode -eq 'governance' -and
                $p -in @(($product+'CMakeLists.txt'),($product+'cmake/ird_gates.cmake'))) { $governanceBootstrap[$p]=$true }
        }
    }
    # 指纹仅止增，不冒充语义查重。即使声明为 business，也不能增加已知自持核心标志。
    $fingerprint = '\b(JacobiSVD|rneaTorques|constrainedDampedLeastSquaresStep|dampedLeastSquaresStep|solveLinearSystem|rotationFromCustomEaa|rk4-fixed|struct\s+Vec3|struct\s+Mat3)\b'
    $count = 0
    foreach ($p in @($changed | Sort-Object -Unique)) {
        if (-not (IsProduct $p)) {
            # 未知后缀/无后缀的新产品文件不能默认放行；先登记发现规则与复用契约。
            # 基线非代码资产仍由既有文档/数据校验负责，不扩大为算法合规声明。
            if ($p.StartsWith($product) -and -not $allBaselineProduct.ContainsKey($p)) { Fail 'UNDECLARED' "new product file type requires governance registration: $p" }
            continue
        }
        $count++
        $txt = ReadText $p -Optional
        $hash = 'deleted'; if ($null -ne $txt) { $hash = Digest $txt }
        $match = @($records[$p] | Where-Object { $_ -and $_.sha256 -eq $hash })
        if (-not $match.Count) { Fail 'UNDECLARED' "$p : $hash" }
        if ($null -eq $txt -and -not @($match | Where-Object { $_.mode -eq 'remove' }).Count) { Fail 'DELETE' $p }
        $old = ''
        if ($tree.ContainsKey($p)) { $old = (GitLines @('show',"${baseline}:$p")) -join "`n" }
        if ([regex]::Matches("$txt",$fingerprint).Count -gt [regex]::Matches($old,$fingerprint).Count) { Fail 'DUPLICATE' $p }
        if ($governanceFiles.ContainsKey($p) -and ($p -ne ($product+'CMakeLists.txt') -or @($match | Where-Object { $_.mode -ne 'governance' }).Count)) { Fail 'GOV-SCOPE' "governance authorization cannot cover product algorithms: $p" }
    }
    $functionalChanges = @($changed | Sort-Object -Unique | Where-Object {
        (IsProduct $_) -and -not $governanceBootstrap.ContainsKey($_)
    })
    CheckFunctions $grants $functionalChanges
    # 框架源码只读；最小扩展也必须另走 SA-02，不可借本轮治理许可直改。
    $upstreamDiff = @(GitLines @('diff','--name-only',$baseline,$tip,'--','RobWork/RobWork/src','RobWork/RobWorkSim/src'))
    if (-not $Head) { $upstreamDiff += @(GitLines @('diff','--name-only','HEAD','--','RobWork/RobWork/src','RobWork/RobWorkSim/src')); $upstreamDiff += @(GitLines @('ls-files','--others','--exclude-standard','--','RobWork/RobWork/src','RobWork/RobWorkSim/src')) }
    if ($upstreamDiff.Count) { Fail 'UPSTREAM-READONLY' ($upstreamDiff -join ',') }
    # 新任务进入 ready/done 需阶段门；历史契约仅保留历史，不自动重新授权执行。
    $taskPaths = @(GitLines @('diff','--name-only',$baseline,$tip,'--',($design+'tasks/')))
    if (-not $Head) {
        $taskPaths += @(GitLines @('diff','--name-only','HEAD','--',($design+'tasks/')))
        $taskPaths += @(GitLines @('ls-files','--others','--exclude-standard','--',($design+'tasks/')))
    }
    foreach ($tp in @($taskPaths | Sort-Object -Unique)) {
        if (-not $tp.EndsWith('.json')) { continue }
        $raw = ReadText $tp -Optional
        if ($null -eq $raw) { continue }
        foreach ($t in @($raw | ConvertFrom-Json)) {
            if ($t.taskId -and $t.status -in @('ready','done')) { CheckTask $t }
        }
    }
    if ($TaskFile) {
        $full = $TaskFile
        if (-not [IO.Path]::IsPathRooted($full)) { $full = Join-Path $RepoRoot $full }
        $full = [IO.Path]::GetFullPath($full)
        if (-not $full.StartsWith($RepoRoot+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) { Fail 'PATH' $TaskFile }
        $rel = $full.Substring($RepoRoot.Length+1).Replace('\','/')
        CheckTask (Json $rel)
    }
    Write-Output "validate-reuse: PASS governance only; inventory=$($tree.Count) units=20 changed=$count DEBT=$debt UNRESOLVED=$audit (not algorithm compliance)"
    exit 0
} catch { [Console]::Error.WriteLine($_.Exception.Message); exit 1 }
