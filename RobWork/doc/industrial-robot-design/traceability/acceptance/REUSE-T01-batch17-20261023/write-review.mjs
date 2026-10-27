import fs from 'node:fs';
import path from 'node:path';
import cp from 'node:child_process';
import crypto from 'node:crypto';
const out=path.dirname(new URL(import.meta.url).pathname.replace(/^\/([A-Z]:)/,'$1'));
const root=path.join(out,'repo');
const text=f=>fs.readFileSync(path.join(out,f),'utf8').replace(/^\uFEFF/,'');
const load=f=>JSON.parse(text(f));
const write=(f,x)=>fs.writeFileSync(path.join(out,f),typeof x==='string'?x:JSON.stringify(x,null,2)+'\n');
const hash=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const m=load('metadata-audit.json'), t=load('test-evidence-check.json');
const gateExitCodes=Object.fromEntries(['task','reuse','docs'].map(k=>[k,Number(text(k+'.exit.txt').trim())]));
if(Object.values(gateExitCodes).some(x=>x!==0))throw new Error('gate failed');
const status=cp.execFileSync('git',['status','--porcelain'],{cwd:root,encoding:'utf8'});
const sha=cp.execFileSync('git',['rev-parse','HEAD'],{cwd:root,encoding:'utf8'}).trim();
if(status!==''||sha!==m.reviewedCommit)throw new Error('frozen checkout changed');
write('git-status-end.txt',status);
const semanticChecks=[
 {area:'core Compare.cpp/Compare.hpp（7定义）',result:'PASS scoped audit',evidence:'C4定向reference公式、非有限原始输入拒绝、vector/Q逐元素与长度失败、C7/nullopt、Tolerance工厂与精确值相等逐一核实。Q受__has_include门控；未宣称本次Q专用测试已跑。公开聚合字段可绕过工厂的事实已保留。'},
 {area:'SetCheck.hpp（8定义）',result:'PASS scoped audit',evidence:'默认traits/MatchPair、compatible调用profile.resolve和core::closeWithin、Kuhn递归与回滚、重复/身份/数值两阶段、交错圈歧义与独立顺序契约均已读。未照抄文件头过期歧义说明；没有把模板或测试辅助视为DTO。'},
 {area:'Dataset.cpp（15定义）',result:'PASS scoped audit',evidence:'字段守卫/词表/std::stoi/值深比较、load schema与来源/单位/完整性、resolve membership到filesystem join逐一核对。sha256HexOf实际调用既有core::ContentDigester，未伪称其自持SHA核心已完成成熟替换。'},
 {area:'Dataset.hpp（9定义）',result:'PASS scoped audit',evidence:'逐字段==/!=、manifest const引用与toleranceProfileRef值返回按实际值转发/所有权边界审查；未把这些访问器当作schema或容差档案装载证明。'},
 {area:'上游/既有依赖候选及供给',result:'PASS bounded evidence',evidence:'Transform3D::equal的绝对precision与Q精确==不能直接承担C4定向相对+绝对公式。既有Boost 1.92公开checked_edmonds_maximum_cardinality_matching/匹配验证器及matching_size读到真实实现，并由原审计目标Boost::headers消费；2个小图API试验证明有限适用，不代表生产接入。'},
 {area:'39条矩阵/定义/上下文',result:'PASS freshness and identity',evidence:'相对435dbdb716b9eb86d0932a478eb086642206ac76新增core7/testkit32；每条定义与矩阵sources完全一致，规范化区段摘要、全限定符号及自身全文件inputs有效。17个唯一绑定（含3个审计证据文件）全部摘要匹配。5组检索重跑输出一致，4个既有Boost安装候选原始摘要一致。'}
];
const staticBoundaries=[
 {path:'testkit/include/sdurws/ird/testkit/SetCheck.hpp',lines:'190–199 / 206–223',observation:'重复键push和阶段1线性查找/嵌套循环与“同键只报一次”“O(n)”注释存在待核边界。',classification:'静态源码事实/风险；本次未运行三次同键或性能场景，不新增已复现缺陷结论。'},
 {path:'testkit/include/sdurws/ird/testkit/SetCheck.hpp',lines:'239–249 / 254–304 / 306–359',observation:'身份失败仍可进入数值阶段，但equivalent已false；歧义实际为交错圈闭包，matched由两阶段依次追加。',classification:'审计正确区别实现与旧注释；不把Boost删边探测等同已替换原歧义实现。'},
 {path:'testkit/include/sdurws/ird/testkit/SetCheck.hpp',lines:'400–431',observation:'长度不同时firstDivergence先设较短长度，早于该位置的失配与契约优先级尚需验证。',classification:'静态待核，未执行产品顺序断言。'},
 {path:'testkit/src/Dataset.cpp',lines:'401–430 / 455–483',observation:'执行代码仅额外要求inputs/expected的integrity membership；未见generatorCommitted时覆盖generatorScript的额外守卫。',classification:'文档已如实限定，不把注释当已实现保证；本次未跑manifest故障用例。'},
 {path:'testkit/src/Dataset.cpp',lines:'138–171 / 367–376 / 410–414',observation:'std::stoi异常转译、double转seed/sizeBytes/threadCount的整数与范围边界尚待专项验证。',classification:'静态待核；未执行溢出/非整数数据集用例，不宣称已复现。'},
 {path:'testkit/src/Dataset.cpp',lines:'199–213 / 469–471 / 488–506',observation:'ref.version/清单路径在join前的边界与membership不构成规范化沙箱；打开失败之外的读错误也未完整验证。',classification:'静态待核；未运行路径穿越/IO故障，不据此宣布安全通过。'}
];
const limitations=[
 '本结论只接受B17的只读逐定义审计、证据新鲜度、有限候选探测与已注册反例事实，不接受产品算法正确性或迁移结项。39条均保持pending；没有新增grant、closure或核心保留/替换批准。',
 'F-960仍OPEN：有限极值下数学差2M大于阈值1.5M，但原生产closeWithin返回true。独立运行保持真实failed；不能删用例、放宽容差或将其计为passed。',
 '本次只重跑新增3项；旧30项、Compare/SetCheck/Dataset产品测试、性能/100000规模、完整集成目标、GUI及DeepEvidence均NOT RUN。依赖库随原审计目标编译，不等于其产品用例通过。',
 'Boost候选只证明两项四顶点图API用例的行为；尚无原SetCheck生产入口接线/断开扰动/全部业务语义、见证、复杂度与内存验收，不能记作direct/adapter生产正向证据。',
 '五组搜索的有限范围与原件一致；无命中不证明不存在其他候选。17绑定中上下文摘要防陈旧，不表示其他源文件全部语义已穷尽审查。',
 '既有全项目覆盖NOT PASS原件不因B17补审改写；T01仍audit-in-progress，未授全T01完成或T02准入。未改仓库、矩阵、产品、上游或vcpkg，未提交、推送、合并或派代理。'
];
const driverPaths=['RobWork/scripts/industrialrobot/validate-task.ps1','RobWork/scripts/industrialrobot/validate-reuse.ps1','RobWork/scripts/industrialrobot/reuse-contract.ps1','RobWork/scripts/industrialrobot/validate-docs.ps1'];
const report={schemaVersion:'ird-independent-review/1',taskId:'REUSE-T01',batch:'B17',reviewDate:'2026-10-23',reviewedCommit:sha,parentCommit:m.parent,result:'PASS',resultScope:'仅B17局部审计登记/证据和原3项探测重现；产品与全T01不因此通过',blockingAuditFindings:[],knownOpenProductFindings:[m.finding],definitionsReviewed:39,coreDefinitions:7,testkitDefinitions:32,allDefinitionsRemainPending:true,semanticChecks,staticBoundaries,metadataChecks:m.checks,searchReplay:m.searchReplay,installedCandidateBindings:m.installedCandidateBindings,verification:{configureExit:0,buildExit:0,visualStudio:load('vs-environment.json'),runEnvironment:load('run-environment.json'),tests:t,gateExitCodes,gateCounts:{passed:3,failed:0,skipped:0},validatorBindings:driverPaths.map(p=>({path:p,rawSha256:hash(path.join(root,p))})),notRun:['旧30项审计探测','Compare/SetCheck/Dataset产品测试','性能与100000规模','完整集成构建','GUI','DeepEvidence']},repositoryClean:true,repositoryModified:false,committed:false,pushed:false,merged:false,subagentsCreated:false,fullT01Pass:false,coverageExitResult:'NOT PASS (原独立全覆盖结论保留，未在此复审全项目)',T02Admission:false,limitations,evidenceRoot:out};
report.reportToolingNotes=['首次TEMP摘录命名将operator==/!=映射为同名，最终自查检出；现已加唯一序号重提取39份，保留verify-review.initial-fail.log和exit。源码/矩阵及测试未修改。'];
write('review.json',report);
write('definitions-review.tsv',['functionId\tpath\tstartLine\tendLine\tstatus\tdefinitionHash\tfullFileContextHash',...m.definitions.map(d=>[d.functionId,d.source.path,d.source.startLine,d.source.endLine,d.status,d.source.sha256,d.sourceContextHash].join('\t'))].join('\n')+'\n');
let md=`# REUSE-T01 B17 独立验收\n\n冻结提交：\`${sha}\`（父提交\`${m.parent}\`）。结论：**PASS，仅B17局部审计登记与证据验收**。没有新增阻断此批审计的发现；原产品缺陷**F-960仍OPEN**，全T01覆盖退出条件仍未通过，**不授T02准入**。\n\n另建TEMP clone，只检出上述SHA；在独立构建目录用原审计CMake与原core/testkit目标重建。初末checkout干净，报告/构建/执行仅写TEMP；不修改仓库、矩阵、产品、上游或vcpkg，不提交/推送/合并/派代理。\n\n## 矩阵、源码与候选\n\nB17相对父提交新增39条：core7、testkit32，全部pending。batch17-definitions与矩阵sources逐条完全一致；39区段摘要与自身全文件inputs均匹配，17个唯一inputs/auditEvidence绑定有效。原环境清单的探测源码/CMake、历史XML/原生报告/stdout摘要也全部匹配。五组冻结搜索在新现场复跑均exit0且输出一致，4个既有Boost候选文件原始摘要一致。\n\n| 冻结文件 | 已核定义 | 语义边界 |\n| --- | ---: | --- |\n| core/src/Compare.cpp | 4 | C4、vector/Q、C7；数学极值失败保留 |\n| core/include/sdurws/ird/core/Compare.hpp | 3 | 工厂合法性与精确值==/!=；公开字段可绕工厂 |\n| testkit/src/Dataset.cpp | 15 | 守卫/词表/版本/深比较/load与resolve；完整性和路径实际边界 |\n| testkit/include/sdurws/ird/testkit/Dataset.hpp | 9 | 值比较、const快照引用及档案引用返回 |\n| testkit/include/sdurws/ird/testkit/SetCheck.hpp | 8 | traits/字段相容、Kuhn/回滚、两阶段/交错圈歧义、顺序 |\n\n前缀为\`RobWork/RobWorkStudio/src/rwslibs/industrialrobot/\`。每条完整符号、区段、输入/差异及冻结定义原文见metadata-audit.json、definitions-review.tsv和逐函数.txt。全限定符号/重载/模板与真实定义一致；字段相等和访问器未冒充load/匹配算法。\n\n`;
for(const c of semanticChecks)md+=`- **${c.area}**：${c.evidence}\n`;
md+=`\n## 独立构建与三项真实执行\n\nVS2022 x64/HostX64，MSVC14.42.34433；原CMake配置/构建均exit0。原core/testkit的Compare.cpp与Dataset.cpp从冻结clone编译，探测用原TestMainReport入口；Boost::headers来自既有vcpkg安装。构建有MSB8029临时目录增量警告，未造成此次fresh构建失败。\n\n绝对路径\`${out}/build/Release/ird_reuse_audit_probe.exe\`依次单独运行两个filter（ReuseAuditMatching.*、ReuseAuditCoreBoundary.*）。没有QApplication/Widgets路径；进程仍由VS x64环境启动，QT_QPA_PLATFORM=windows。\n\n| 运行 | passed | failed | skipped | notRun | exit |\n| --- | ---: | ---: | ---: | ---: | ---: |\n| 既有Boost公开模板API候选 | 2 | 0 | 0 | 0 | 0 |\n| 原core C4极值反例 | 0 | 1 | 0 | 0 | 1 |\n| 合计执行 | 2 | 1 | 0 | 0 | — |\n\n候选分别验证贪心陷阱下唯一完美匹配、删匹配边后真歧义/唯一匹配的对照。core用最大有限double M、v=M/ref=-M/relative=.5/absolute=M，独立缩放oracle为2>1.5；原core真实返回true导致EXPECT_FALSE失败。没有复制或修补生产比较实现，也没有把该失败改记为passed/skip。F-960登记与上述行为一致，owner=core/REUSE-T05，保持open。\n\nXML testcase、XML总计、原生ird-test-report与exit/stdout逐项核对；新旧测试身份和结果一致。test-evidence-check.json保留核对及新产物摘要。旧30项探测NOT RUN；产品SetCheck/Dataset/Compare测试及性能未运行。审计PASS只接受其如实复现和登记，不表示产品通过。\n\n## 静态疑点边界\n\n以下已定位原文并确认本批没有冒充动态复现或安全/性能通过；保留原审计待核，不另造重复finding。\n\n| 文件/区段（产品前缀同上） | 实际边界 | 本次判断 |\n| --- | --- | --- |\n`;
for(const b of staticBoundaries)md+=`| \`${b.path}:${b.lines}\` | ${b.observation} | ${b.classification} |\n`;
md+=`\n## 治理门禁与退出限制\n\n冻结SHA自身脚本执行：validate-task、validate-reuse -Head ${sha}、validate-docs均PASS/exit0，门禁passed=3、failed=0、skipped=0。复用输出functions=651、closedFiles=0，并明确governance only；此计数不是全功能覆盖证明。脚本/helper/driver摘要在review.json。DeepEvidence NOT RUN。\n\n`+limitations.map((x,i)=>`${i+1}. ${x}`).join('\n')+`\n\n证据根：\`${out}\`。主报告review.md/review.json；定义核对metadata-audit.json/definitions-review.tsv；运行核对test-evidence-check.json及candidates/、core-red/；真实日志configure.log/build.log/task.log/reuse.log/docs.log。可归档这些TEMP产物，排除repo/和build/。\n`;
md+=`
报告工具自查说明：首次TEMP摘录命名冲突已由最终自查检出并修复为39个唯一路径，重新核验全部摘录；首次失败日志verify-review.initial-fail.log及exit保留。未修改源码或测试。
`;
write('review.md',md);
console.log(JSON.stringify({result:report.result,scope:report.resultScope,definitions:39,probeCounts:t.totals,gateExitCodes,repositoryClean:true,fullT01Pass:false,T02Admission:false},null,2));
