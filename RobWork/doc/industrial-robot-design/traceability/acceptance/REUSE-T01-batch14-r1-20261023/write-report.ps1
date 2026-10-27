$ErrorActionPreference='Stop'
$b=$PSScriptRoot;$r=Join-Path $b 'repo'
$sha=(& git -C $r rev-parse HEAD).Trim();$parent=(& git -C $r rev-parse HEAD^).Trim()
$hash=@(Get-Content (Join-Path $b 'hash-checks.json') -Raw|ConvertFrom-Json)
$defs=@(Get-Content (Join-Path $b 'definition-checks.json') -Raw|ConvertFrom-Json)
$balance=@(Get-Content (Join-Path $b 'definition-balance.json') -Raw|ConvertFrom-Json)
$replay=@(Get-Content (Join-Path $b 'search-replay.json') -Raw|ConvertFrom-Json)
$installed=@(Get-Content (Join-Path $b 'installed-checks.json') -Raw|ConvertFrom-Json)
$archives=@(Get-Content (Join-Path $b 'archive-checks.json') -Raw|ConvertFrom-Json)
$facts=Get-Content (Join-Path $b 'final-facts.json') -Raw|ConvertFrom-Json
$toolHashes=@('validate-reuse.ps1','reuse-contract.ps1','validate-task.ps1','validate-docs.ps1')|ForEach-Object {
 $p=Join-Path $r ('RobWork/scripts/industrialrobot/'+$_)
 [ordered]@{path=('RobWork/scripts/industrialrobot/'+$_);rawSha256=(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant()}
}
$drivers=@('check-b14.ps1','check-final.ps1','write-report.ps1')|ForEach-Object {[ordered]@{path=(Join-Path $b $_);rawSha256=(Get-FileHash -LiteralPath (Join-Path $b $_) -Algorithm SHA256).Hash.ToLowerInvariant()}}
$checks=@(
 [ordered]@{name='validate-task';status='PASS';exitCode=[int](Get-Content (Join-Path $b 'task.exit.txt'));command=('pwsh -NoProfile -File '+$r+'/RobWork/scripts/industrialrobot/validate-task.ps1 -TaskFile '+$r+'/RobWork/doc/industrial-robot-design/tasks/REUSE-T01.json -RepoRoot '+$r);log='task.log'},
 [ordered]@{name='validate-docs';status='PASS';exitCode=[int](Get-Content (Join-Path $b 'docs.exit.txt'));command=('pwsh -NoProfile -File '+$r+'/RobWork/scripts/industrialrobot/validate-docs.ps1 -RepoRoot '+$r+'/RobWork');log='docs.log'},
 [ordered]@{name='validate-reuse frozen';status='PASS';exitCode=[int](Get-Content (Join-Path $b 'reuse.exit.txt'));command=('pwsh -NoProfile -File '+$r+'/RobWork/scripts/industrialrobot/validate-reuse.ps1 -RepoRoot '+$r+' -Head '+$sha+' -TaskFile RobWork/doc/industrial-robot-design/tasks/REUSE-T01.json');log='reuse.log'},
 [ordered]@{name='scoped git diff --check';status='PASS';exitCode=[int](Get-Content (Join-Path $b 'scoped-diff-check.exit.txt'));command=('git -C '+$r+' diff --check HEAD^ HEAD -- <changed-files.log minus diff-check-exclusions.log>');log='scoped-diff-check.log'}
)
$status=@(& git -C $r status --porcelain=v1)
if($status.Count -ne 0 -or @($hash|Where-Object {!$_.pass}).Count -ne 0 -or @($replay|Where-Object {!$_.matches}).Count -ne 0 -or @($checks|Where-Object {$_.exitCode -ne 0}).Count -ne 0 -or @($archives|Where-Object {!$_.pass}).Count -ne 0){throw 'Verification evidence changed'}
$finding=[ordered]@{
 id='B14-P2-01';priority=2;category='semantic-registration';file='RobWork/doc/industrial-robot-design/reuse/functions/project.json';line=2388;functionId='project.Codec.anonymous.requirePathField';
 title='路径接受面被误记为ASCII';
 observed='differences写4096字节ASCII路径字段profile。Codec.cpp 773-778明确允许空格/非ASCII/中文路径；779-794只要求非空、<=4096 UTF8字节，并拒绝字节<0x20或==0x7F。parseString/parseUnicodeEscape在进入requirePathField前已把JSON Unicode转义还原为UTF8，不能把磁盘JSON的ASCII字节形态当作路径值的ASCII限制。';
 impact='该关系是后续typed parser/schema适配的语义依据，错误profile可能导致迁移后拒绝现有合法中文路径，改变已有接受/拒绝契约。';
 evidence=@('RobWork/RobWorkStudio/src/rwslibs/industrialrobot/project/src/Codec.cpp:773-794','RobWork/RobWorkStudio/src/rwslibs/industrialrobot/project/src/Codec.cpp:299-399','RobWork/RobWorkStudio/src/rwslibs/industrialrobot/project/src/Codec.cpp:1416-1418','semantic-evidence.log');
 requestedChange='只纠正该关系的语义描述：解码后的UTF8路径值允许空格/非ASCII，非空、<=4096字节、拒C0/DEL；磁盘JSON仍为ASCII转义载体。保留io路径解析/存在性/安全权威边界，不改产品接受面。';
 reproducedProductDefect=$false;status='OPEN'
}
$report=[ordered]@{
 schemaVersion='ird-independent-acceptance/1';taskId='REUSE-T01';batch='B14';result='FAIL';verdict='FAIL';scope='仅B14新增132关系与相关治理登记，以及B12-r2/B13归档保真；不授全T01 PASS/T02准入/生产迁移批准';reviewedCommit=$sha;reviewedParentCommit=$parent;repo=$r;reportRoot=$b;
 counts=[ordered]@{newRelations=132;project=68;reporting=64;sourceRanges=161;definitionRecords=132;hashChecks=$hash.Count;hashFailures=0;definitionFailures=0;searchReplays=$replay.Count;searchFailures=0;installedCandidates=$installed.Count;installedFailures=0;archiveFiles=$archives.Count;archiveRawMatches=58;archiveNewlineOnly=5;archiveContentMismatches=0;allRelations=548;pending=545;proposal=3;closedFiles=0;gatePassed=4;gateFailed=0;gateSkipped=0;semanticBlockingFindings=1};
 findings=@($finding);checks=$checks;toolHashes=$toolHashes;reviewDriverHashes=$drivers;
 semanticReview=@(
 '逐定义审读project Codec.cpp及reporting ReportCodec.cpp。161区段first/last、范围、规范化摘要与132定义记录一致，花括号完整性辅助核对通过；六个canonical、七个dump、Reader两个构造均独立绑定，29组成对read/write的作用域准确。',
 'project四类JsonValue、递归下降、深度64、整数词法/代理对/UTF8/JSON writer与hex均为真实自持实现，不能因testkit禁用于产品而豁免H1。除B14-P2-01外审计对输出canonical与输入可接受多种转义形态、各记录版本差异、nested unknown传参及整数极值待核的限定符合源码。',
 'reporting大端原语、f64位搬运、0/1 presence、NUL拒绝、单位core查找及各叶子值/章节/评审成对搬运符合源码；parseFull确实复制Data字段读取，encodeFull嵌入encodeData结果；digestFull仅身份副本清currentness.computedAtUtc。',
 'BINArchive公共头/实现与sdurw_common目标真实，标量宿主表示及string格式化长度不能直接替换冻结字节。既有Boost.Endian公开load/store_big_u16/u32/u64和Boost.Algorithm hex_lower/unhex存在，unhex的大小写/异常差异已准确限定；安装候选不是RobWork源或新依赖批准。',
 '既有B8-r8公开API验收仅覆盖三项，其中PropertyTree类型/空容器及BOM/重复键反例的范围未被夸大。本批未重跑B8；Boost.JSON与nlohmann公开入口当前安装不存在，仅说明此安装供给，未推断整个成熟库能力缺失。',
 '真实消费者包含ProjectStoreImpl/TxEngine/DraftServiceImpl/ArchiveServiceImpl/StoreClone/QueryPortImpl及ReportModel/Builder；Codec.cpp与ReportCodec.cpp由原产品目标编译，绑定测试真实调用codec/ReportCodec；没有用静态grep充当本轮生产正向实验。',
 '73变更均在设计治理区，产品/框架/vcpkg diff为空。独立clone从冻结SHA起止clean；未触碰源工作树已有.cache/。B12-r2与B13归档63文件保真，58 raw相同、5仅CRLF/LF，scope不扩为全T01或当前B14 PASS。原始日志尾空白未清洗，33条日志在diff-check中精确排除，列表保存。'
 );
 changedFiles=@(Get-Content (Join-Path $b 'changed-files.log'));diffCheckExclusions=$facts.excludedLogs;
 clean=$true;repositoryModified=$false;committed=$false;pushed=$false;merged=$false;subagentsCreated=$false;wholeT01Pass=$false;T02Admission=$false;
 remoteDelivery=[ordered]@{status='NOT DELIVERED / NOT VERIFIED';reason='指派说明GitHub443当前不可达；本轮只验本地冻结SHA，未推送、未验证远程或CI。'};
 notRun=@('C++构建/产品测试','GUI','性能/数值/极大内存或超长边界实跑','Boost/API探测重跑','生产返回值扰动与独立oracle实验','全库DeepEvidence','全T01退出/T02准入','远程推送/CI');
 remainingStaticQuestions=@('integerInRange大于INT64_MAX正整数的实现相关转换','Reader向量reserve前内存预算与Writer字符串/向量size转u32超长拒绝','字符串仅检查NUL，完整UTF8验证并未成立','readTime关于MSVC纳秒无损的注释未实证');
 remainingQuestionsAreReproducedDefects=$false
}
$report|ConvertTo-Json -Depth 20|Set-Content (Join-Path $b 'acceptance.json') -Encoding utf8
$md=@'
# REUSE-T01 B14 独立验收

**FAIL：1项语义登记问题，门禁均通过。** 冻结提交 b83fa7a3fc8ff233e56736adabd53f11c914dbad，父提交6925608ccbc5c7764dc983f7e7e0276f1f12d55f。本地独立Temp clone，core.longpaths=true后detached checkout，起止clean。没有实施、改源工作树、提交、推送、合入或创建子代理。

## B14-P2-01：路径接受面误记为ASCII

reuse/functions/project.json:2388 的 project.Codec.anonymous.requirePathField 写“4096字节ASCII路径字段profile”。真实Codec.cpp:773–778明确允许空格/非ASCII中文路径；779–794只要求非空、长度≤4096 UTF8字节，并拒绝字节<0x20或==0x7F。parseString/parseUnicodeEscape先把JSON转义还原UTF8，parseDraftDocument:1418把解码值交给该函数。因此磁盘JSON纯ASCII不等于解码路径只能ASCII。

该登记将成为后续typed parser/schema迁移的语义依据，若按错误profile实施会拒绝现有合法中文路径。请只改该行描述，保留非空/长度/C0与DEL约束、空格和非ASCII接受面，以及io对路径解析/存在性/安全的权威边界；不改产品接受面。这是静态确证的登记错误，本轮未运行C++，不称产品缺陷已复现。源码/矩阵片段见semantic-evidence.log。

## 已通过的核验

- 新增132关系：project68、reporting64，全部pending；161个源码区段、132个definitions记录首尾/范围、958项摘要一致。真实完整定义、六个canonical重载、七个dump重载、Reader两个构造和29组成对read/write均核对，qualifiedSymbol作用域相符。
- 七条搜索真实重放，exitCode与排序后行多重集合一致；命中44/122/208/12/28/3/9。六项安装候选摘要或入口存在性检查一致。
- BINArchive宿主表示/string长度和固定大端报告字节不等价；Boost公开Endian与hex候选真实。PropertyTree既有B8-r8反例仅按原三项API证据引用，未重跑或扩大为所有JSON库无能力。未把外部安装头伪装上游RobWork源码。
- ReportCodec的parseFull确实重复Data读取，encodeFull复用encodeData；digestFull只对身份副本清currentness.computedAtUtc，编码仍保留原时间。单位、身份、摘要、当前性与诊断权威边界保持；向量reserve、u32超长、UTF8、readTime等待核事项未伪称实跑缺陷。
- 真实消费者、测试调用与原CMake注册均核对；静态绑定没有充当生产正向实验。73个变更均为治理设计文件，产品/框架/vcpkg无改。
- B12-r2/B13新增归档63文件保真：58原始字节一致，5仅CRLF/LF，内容差异0。归档scope未扩大。33条原始日志排除diff-check且未清洗，精确列表见diff-check-exclusions.log。

| 检查 | 结果 | exit |
| --- | --- | --- |
| validate-task | PASS | 0 |
| validate-docs，普通模式 | PASS | 0 |
| validate-reuse，冻结Head | PASS governance only | 0 |
| 范围git diff --check | PASS | 0 |

机器检查4 passed、0 failed、0 skipped；独立语义发现1项阻断。全矩阵548＝545 pending＋3 proposal，closedFiles=0；inventory=1401、units=20、DEBT=16、UNRESOLVED=1399。真实命令、日志与validator/helper/driver SHA-256见acceptance.json。

NOT RUN：C++/GUI/性能、极大内存/超长边界、API探测重跑、生产返回值扰动及独立oracle、DeepEvidence、远程推送/CI。GitHub443不可达是指派背景，本轮未验证远程交付。不得以本报告授全T01 PASS、T02准入或算法替换批准。
'@
$md|Set-Content (Join-Path $b 'acceptance.md') -Encoding utf8
[ordered]@{result=$report.result;finding=$finding.id;reportRoot=$b;json=(Join-Path $b 'acceptance.json');markdown=(Join-Path $b 'acceptance.md')}|ConvertTo-Json
