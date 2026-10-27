$ErrorActionPreference='Stop'
$b=$PSScriptRoot
$r=Join-Path $b 'repo'
$sha='a78b41ff29261b17fc8379ee27e4afb1933ab827'
$facts=Get-Content -LiteralPath (Join-Path $b 'facts.json') -Raw|ConvertFrom-Json
$checks=@()
foreach($name in @('task','docs','reuse')) {
    $code=[int](Get-Content -LiteralPath (Join-Path $b "$name.exit.txt") -Raw).Trim()
    if($code -ne 0){throw "$name exit=$code"}
    $checks += [ordered]@{name="validate-$name";status='PASS';exitCode=$code;log="$name.log"}
}
if($facts.reviewedCommit -cne $sha -or !$facts.descriptionMatches -or !$facts.onlyExpectedMatrixChange -or !$facts.rowPending -or $facts.hashFailures -ne 0 -or !$facts.reworkSourceMatches -or $facts.archiveMismatches -ne 0 -or !$facts.originalFailPreserved -or $facts.scopedDiffCheckExit -ne 0 -or !$facts.sourceDiffEmpty -or !$facts.clean){throw 'facts validation failed'}
$head=((& git -C $r rev-parse HEAD).Trim())
$parent=((& git -C $r rev-parse HEAD^).Trim())
$status=@(& git -C $r status --porcelain=v1)
if($head -cne $sha -or $parent -cne '85e561539a4038e37231dcfcb506109a5a188151' -or $status.Count -ne 0){throw 'frozen checkout changed'}
$checks += [ordered]@{name='scoped git diff --check';status='PASS';exitCode=0;log='scoped-diff-check.log';scope='HEAD^..HEAD changed files, excluding original archived logs listed in diff-check-exclusions.log'}
$toolHashes=@()
foreach($file in @('validate-task.ps1','validate-docs.ps1','validate-reuse.ps1','reuse-contract.ps1')) {
    $path="RobWork/scripts/industrialrobot/$file"
    $toolHashes += [ordered]@{path=$path;rawSha256=(Get-FileHash -LiteralPath (Join-Path $r $path) -Algorithm SHA256).Hash.ToLowerInvariant()}
}
foreach($file in @('check-rework.ps1','write-acceptance.ps1')) {
    $toolHashes += [ordered]@{path=$file;rawSha256=(Get-FileHash -LiteralPath (Join-Path $b $file) -Algorithm SHA256).Hash.ToLowerInvariant()}
}
$report=[ordered]@{
    schemaVersion='ird-independent-acceptance/1'
    taskId='REUSE-T01'
    batch='B14-rework-r2'
    result='PASS'
    verdict='PASS，仅B14-P2-01返工复验'
    scope='仅核验B14-P2-01语义登记修正、原B14-r1 FAIL归档保真及相关task/docs/reuse门禁；不授整批B14或全T01 PASS，不授T02准入或生产迁移批准'
    reviewedCommit=$sha
    reviewedParentCommit=$parent
    comparisonBase=$facts.comparisonBase
    repo=$r
    reportRoot=$b
    createdAt=(Get-Date).ToUniversalTime().ToString('o')
    counts=[ordered]@{hashChecks=7;hashFailures=0;archiveFiles=44;archiveRawMatches=40;archiveNewlineOnly=4;archiveContentMismatches=0;gatePassed=4;gateSkipped=0;gateFailed=0;semanticBlockingFindings=0}
    findings=@([ordered]@{id='B14-P2-01';priority=2;status='CLOSED';functionId='project.Codec.anonymous.requirePathField';resolution='修正后的登记准确描述解码后UTF-8路径：允许中文与空格，非空、最多4096字节，拒绝C0(<0x20)与DEL(0x7F)。磁盘JSON的ASCII载体限制未错误施加到解码后路径。io解析/存在性/安全权威边界保留。';evidence=@('RobWork/RobWorkStudio/src/rwslibs/industrialrobot/project/src/Codec.cpp:299-399','RobWork/RobWorkStudio/src/rwslibs/industrialrobot/project/src/Codec.cpp:773-794','RobWork/RobWorkStudio/src/rwslibs/industrialrobot/project/src/Codec.cpp:1418','RobWork/doc/industrial-robot-design/reuse/functions/project.json:2388','RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01/batch14-rework.json','facts.json','hash-checks.json');reproducedProductDefect=$false})
    checks=$checks
    facts=$facts
    originalAcceptance=[ordered]@{path='RobWork/doc/industrial-robot-design/traceability/acceptance/REUSE-T01-batch14-r1-20261023/acceptance.json';result='FAIL';reviewedCommit=$facts.comparisonBase;originalFindingStatus='OPEN';preserved=$true;comparison='40 raw SHA256 matches; 4 CRLF/LF only; no content mismatch'}
    boundaries=[ordered]@{matrixChange='与原B14冻结SHA相比，project.json仅该条differences改变；decision/status仍pending';productChanged=$false;sourceDiff='原B14 SHA..当前SHA产品/上游/vcpkg差异为空';clean=$true;fullStagePass=$false;remoteVerified=$false}
    notRun=@('C++构建','GUI测试','数值/性能测试','DeepEvidence','B14全量关系复审','其他批次验收','远程交付核验')
    limitations=@('validate-docs输出NOT AUDITED evidence；未执行DeepEvidence。','validate-reuse仅治理检查通过，closedFiles=0，不代表算法合规或迁移授权。','原始归档日志未清洗，scoped diff --check排除项精确保存在diff-check-exclusions.log。','仅在Temp独立clone验收；未修改父现场、提交、推送或合入。')
    toolHashes=$toolHashes
}
$report|ConvertTo-Json -Depth 30|Set-Content -LiteralPath (Join-Path $b 'acceptance.json') -Encoding utf8
$md=@"
# B14-P2-01 返工独立复验

结论：**PASS，仅 B14-P2-01 返工复验，该项关闭**。本结论不重授整批 B14 / 全 T01 PASS，不授 T02 准入或生产迁移批准。

冻结 SHA：$sha；直接父 SHA：$parent。原 FAIL 比较基准：$($facts.comparisonBase)。独立现场：$r。

修正文案与 Codec.cpp 的真实行为一致：解码后 UTF-8 路径允许中文和空格；非空、最多 4096 字节，拒绝 C0 字节(<0x20)与 DEL(0x7F)。已核 parseString/parseUnicodeEscape、requirePathField(779–794) 及调用处；磁盘 JSON 的 ASCII 载体限制与解码后路径接受面已分清，io 权威边界保留。

与原 B14 SHA 比较，project.json 仅目标关系的 differences 改变，其余矩阵数据一致，decision/status 仍为 pending。返工记录 before/after、source 与矩阵相符。7 项摘要全部匹配（源码区段 1、inputs 4、auditEvidence 2）。本次未重做原 B14 的全部关系审计，未实跑复现产品缺陷。

原 FAIL 归档 44 文件：40 个原始字节摘要相同，4 个仅 CRLF/LF 差异，无内容偏差；原 acceptance 仍为 FAIL、原 SHA 与 OPEN finding 保持原样。原始日志未清洗。

| 检查 | 结果 |
| --- | --- |
| validate-task | PASS，exit 0 |
| validate-docs | PASS，exit 0；NOT AUDITED evidence |
| validate-reuse，冻结 SHA | PASS，exit 0；仅治理检查 |
| scoped git diff --check | PASS，exit 0；排除归档原日志，见精确排除表 |

四项门禁 passed=4、skipped=0、failed=0。validate-reuse 的 closedFiles=0 不代表算法合规。源码/上游/vcpkg 自原 B14 SHA 起无变化，独立 checkout 起止干净。

NOT RUN：C++ 构建、GUI、数值/性能、DeepEvidence、B14 全量关系重审、其他批次、远程交付核验。未修改父现场、提交、推送或合入。

详细证据：acceptance.json、facts.json、hash-checks.json、archive-checks.json、task/docs/reuse.log 与 exit 文件、scoped-diff-check.log、diff-check-exclusions.log、product-upstream-diff.log、final-status.log。验证脚本和报告生成脚本 SHA256 记录于 acceptance.json。
"@
$md|Set-Content -LiteralPath (Join-Path $b 'acceptance.md') -Encoding utf8
