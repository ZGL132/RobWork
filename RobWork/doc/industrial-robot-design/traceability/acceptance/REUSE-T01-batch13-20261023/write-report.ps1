$ErrorActionPreference='Stop'
$b=$PSScriptRoot;$r=Join-Path $b 'repo'
$sha=(& git -C $r rev-parse HEAD).Trim();$parent=(& git -C $r rev-parse HEAD^).Trim()
$files=@(& git -C $r diff --name-only HEAD^ HEAD)
$status=@(& git -C $r status --porcelain=v1)
$hash=@(Get-Content (Join-Path $b 'hash-checks.json') -Raw|ConvertFrom-Json)
$replays=@(Get-Content (Join-Path $b 'search-replay.json') -Raw|ConvertFrom-Json)
$tools=@('validate-reuse.ps1','reuse-contract.ps1','validate-task.ps1','validate-docs.ps1')|ForEach-Object {
 $p=Join-Path $r ('RobWork/scripts/industrialrobot/'+$_)
 [ordered]@{path=('RobWork/scripts/industrialrobot/'+$_);rawSha256=(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant()}
}
$drivers=@('check-b13.ps1','write-report.ps1')|ForEach-Object {[ordered]@{path=(Join-Path $b $_);rawSha256=(Get-FileHash -LiteralPath (Join-Path $b $_) -Algorithm SHA256).Hash.ToLowerInvariant()}}
$checks=@(
 [ordered]@{name='validate-task';status='PASS';exitCode=[int](Get-Content (Join-Path $b 'task.exit.txt'));command='pwsh -NoProfile -File <repo>/RobWork/scripts/industrialrobot/validate-task.ps1 -TaskFile <repo>/RobWork/doc/industrial-robot-design/tasks/REUSE-T01.json -RepoRoot <repo>';log='task.log'},
 [ordered]@{name='validate-docs';status='PASS';exitCode=[int](Get-Content (Join-Path $b 'docs.exit.txt'));command='pwsh -NoProfile -File <repo>/RobWork/scripts/industrialrobot/validate-docs.ps1 -RepoRoot <repo>/RobWork';log='docs.log'},
 [ordered]@{name='validate-reuse frozen';status='PASS';exitCode=[int](Get-Content (Join-Path $b 'reuse.exit.txt'));command=('pwsh -NoProfile -File <repo>/RobWork/scripts/industrialrobot/validate-reuse.ps1 -RepoRoot <repo> -Head '+$sha+' -TaskFile RobWork/doc/industrial-robot-design/tasks/REUSE-T01.json');log='reuse.log'},
 [ordered]@{name='scoped git diff --check';status='PASS';exitCode=[int](Get-Content (Join-Path $b 'diff-check.exit.txt'));command='git -C <repo> diff --check HEAD^ HEAD -- <seven changed governance files>';log='diff-check.exit.txt'}
)
if($status.Count -ne 0 -or @($hash|Where-Object {!$_.pass}).Count -ne 0 -or @($replays|Where-Object {!$_.matches}).Count -ne 0 -or @($checks|Where-Object {$_.exitCode -ne 0}).Count -ne 0){throw 'Evidence mismatch'}
$report=[ordered]@{
 schemaVersion='ird-independent-acceptance/1';taskId='REUSE-T01';batch='B13';result='PASS';scope='仅B13新增54关系与相关治理登记；不授予全T01退出、T02准入或生产迁移批准';reviewedCommit=$sha;reviewedParentCommit=$parent;repo=$r;clean=$true;changedFiles=$files;
 counts=[ordered]@{newRelations=54;execution=36;modeling=18;allRelations=416;pending=413;proposal=3;closedFiles=0;hashChecks=352;searchReplays=6;validationPassed=4;validationFailed=0;validationSkipped=0;cppNotRun=$true;guiNotRun=$true;performanceNotRun=$true};
 findings=@();checks=$checks;toolHashes=$tools;reviewDriverHashes=$drivers;
 semanticReview=@(
 'XacroExpand.cpp完整定义审读：emitElement 557-872为实际定义；匿名/外部作用域准确；普通XML层级不计展开深度，总字节护栏仅普通元素收尾，未证明峰值内存。',
 'ChannelProtocol.cpp完整定义审读：22-63确含put/get U16/U32/U64六函数，代表符号putU16的分组边界已明示；其余35条关系为真实完整定义。',
 'UTIL-XML真实存在load/save/getRootElement，BoostXMLParser实现由sdurw_core构建；PluginTest.loadDirectPlugin真实消费DOMParser。pugixml已有load_buffer/print/save/xml_writer，未误报无序列化。',
 'UTIL-BINARCHIVE标量写宿主表示、string格式化长度语义与固定LE协议不等价，候选排除边界正确；摘要委托core，hex只是文本映射。',
 'MDL-19与TASK-03引用匹配；输入字节/依赖树来自io、栈上Engine生命周期、未重读文件；worker纯值/身份边界及diagnostics文案权威保持待核，未新授权限。',
 '六条搜索按arguments数组真实重放，比较排序后的行多重集合与exitCode；命中行数376/10/134/160/16/14均一致。第六条读取现有外部安装pugixml头，非冻结Git内文件。'
 );
 limitations=@('全部新增关系pending，未作适用性运行探测、未跑上游测试或产品C++/GUI/性能。','attempt 24字节/无NUL、跨片type/identity、seq/fragmentSize极值和缓冲峰值均为静态待核，未声称已复现。','完整生产消息往返、Xacro输出适配字节回归、project/reporting及其他工具覆盖留后续。','本轮未改repo、未提交/推送/合入。')
}
$report|ConvertTo-Json -Depth 15|Set-Content (Join-Path $b 'acceptance.json') -Encoding utf8
$md=@'
# REUSE-T01 B13 独立验收

结论：**PASS，仅本批治理登记**。冻结提交 63e86ba5a633e93c9027165756298d76536166b9，父提交 d38e4472ec403d9d9f1b7201a0f8e2b4b83f7a0c。在独立 Temp clone 的 detached HEAD 现场审查，起始/结束均 clean；7 个变更文件均为设计治理文件，产品/框架/vcpkg 无变更。未修改实施现场、未提交/推送/合入。

- 新增54条关系：execution 36、modeling 18，全部 decision/status=pending。全矩阵416条＝413 pending＋3 proposal；关闭文件0（closures.json未建立，校验器按空处理）。库存和既有提案未改。
- 已审读 XacroExpand.cpp、ChannelProtocol.cpp 完整定义。emitElement 557–872 为真实函数定义；22–63确含put/get U16/U32/U64六函数组，代表符号与分组限制明确。其余区段、匿名命名空间和外部服务作用域与登记相符。
- 352项源码区段、inputs、auditEvidence、相关catalog与target摘要均一致。详见 hash-checks.json。
- 六条保存的arguments数组真实重放，退出码与排序后行多重集合全部匹配；行数376/10/134/160/16/14。详见 search-replay.json、search-N.log。第六条使用本机既有vcpkg公开pugixml头，其输出与记录一致，属于外部安装现场证据。
- UTIL-XML的load/save/getRootElement及BoostXMLParser实现真实，目标sdurw_core与PluginTest.loadDirectPlugin消费可核；pugixml已有print/save/xml_writer，审计未声称库无序列化。UTIL-BINARCHIVE宿主表示及格式化string长度不能直接代换固定LE业务协议；catalog保持candidate，未授权新通用平台。
- MDL-19、TASK-03与单元卡边界对应；Xacro消费io字节/依赖树、Engine栈上保活、无重新文件读取；摘要委托core。文案归diagnostics、worker只传值/身份的边界被明确保留，未用码值来源冒充正式中文文案合规。

| 验证 | 结果 | exit | 日志 |
| --- | --- | --- | --- |
| validate-task | PASS | 0 | task.log |
| validate-docs（普通模式，非DeepEvidence） | PASS | 0 | docs.log |
| validate-reuse（冻结Head） | PASS，governance only | 0 | reuse.log |
| 七文件git diff --check | PASS | 0 | diff-check.exit.txt |

上述门禁4 passed、0 failed、0 skipped；详细命令、validator/helper及验收driver原始SHA-256见 acceptance.json。reuse结果为inventory=1401、units=20、DEBT=16、UNRESOLVED=1399，仍是治理通过而非算法合规。既有HostView3DGateway的REUSE-AD-PENDING未产生grant。

未执行：C++/GUI/性能、上游完整测试、生产消息往返、pugixml输出适用性运行探测。审计正确把以下事项保留为静态待核：attempt固定24字节但解码拒填满无NUL、跨片type/identity一致性由外层保证与否、seq/fragmentSize极值及缓冲峰值；没有将它们写成已复现缺陷。Xacro总字节护栏只在普通元素收尾检查，不能证明峰值内存已被严格约束。project/reporting与其他工具覆盖仍需后续。

本报告不授予全REUSE-T01 PASS、REUSE-T02准入、算法替换或生产迁移批准。
'@
$md|Set-Content (Join-Path $b 'acceptance.md') -Encoding utf8
[pscustomobject]@{result=$report.result;json=(Join-Path $b 'acceptance.json');markdown=(Join-Path $b 'acceptance.md');checks=$checks.Count;hashes=$hash.Count;searches=$replays.Count}|ConvertTo-Json
