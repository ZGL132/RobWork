import pathlib,json,hashlib,collections,subprocess,datetime
B=pathlib.Path(__file__).parent;R=B/'repo';D=R/'RobWork/doc/industrial-robot-design'
def load(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def write(n,v): (B/n).write_text(v if isinstance(v,str) else json.dumps(v,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
facts=load(B/'facts.json'); hashes=load(B/'hash-checks.json'); archives=load(B/'archive-checks.json'); probes=load(B/'freshness-probe.json')
assert facts['reviewedCommit']=='435dbdb716b9eb86d0932a478eb086642206ac76'
assert facts['newExactlyMatchesDefinitions'] and not facts['previousRowsChanged'] and facts['allNewPending']
assert not any(facts[k] for k in ['hashFailures','definitionFailures','searchFailures','targetFailures','archiveMismatches','scopedDiffCheckExit'])
assert facts['sourceDiffEmpty'] and facts['clean'] and len(probes)==5 and all(p['allRegisteredHashesUnchanged'] for p in probes)
checks=[]
for name,args in [('task',['-TaskFile',str(D/'tasks/REUSE-T01.json'),'-RepoRoot',str(R)]),('docs',['-RepoRoot',str(R/'RobWork')]),('reuse',['-RepoRoot',str(R),'-Head',facts['reviewedCommit'],'-TaskFile','RobWork/doc/industrial-robot-design/tasks/REUSE-T01.json'])]:
 code=int((B/f'{name}.exit.txt').read_text(encoding='utf-8-sig').strip()); assert code==0
 checks.append({'name':'validate-'+name,'status':'PASS','exitCode':code,'command':['pwsh','-NoProfile','-File',str(R/f'RobWork/scripts/industrialrobot/validate-{name}.ps1'),*args],'log':name+'.log'})
checks.append({'name':'scoped git diff --check','status':'PASS','exitCode':0,'log':'scoped-diff-check.log','scope':'HEAD^..HEAD变动文件，排除已逐文件验证保真的原始归档.log与.frozen.txt；精确列表见diff-check-exclusions.log。首次只排除.log的exit2仅为原frozen摘录保留的尾空格，原输出见scoped-diff-check-initial.log。'})
finding={'id':'B16-P2-01','priority':2,'category':'evidence-freshness','status':'OPEN','title':'函数外决定性测试上下文未纳入摘要绑定','file':'RobWork/doc/industrial-robot-design/reuse/functions/dynamics.json','line':573,'relatedLocations':[{'file':'RobWork/doc/industrial-robot-design/reuse/functions/dynamics.json','lines':[573,738,793,848]},{'file':'RobWork/doc/industrial-robot-design/reuse/functions/modeling.json','lines':[2723]}],'affectedFunctions':[p['functionId'] for p in probes],'observed':'expectNearRel只绑定StatisticsTest.cpp:103–108，描述中的1e-12来自未绑定的97行kGoldenRelTol；prismAxialForce/revTorque/makePrismRevModel依赖184–189行质量、半径、惯量、重力常量，却仅绑定各函数定义及CMake/TwoLinkFixture.hpp。makeBasicSource只绑定XacroExpandTest.cpp:202–205，所消费kBasicXacro raw字符串186–200未绑定。相关inputs没有本cpp全文件。','impact':'改变测试容差、解析期望/模型参数或Xacro字节与真实摘要，可保留全部登记摘要不变，导致受影响语义证据不能按契约失效；函数区段本身完整并不能覆盖这些实际输入。','evidence':['freshness-probe.json','freshness-probe.py','definition-checks.json','hash-checks.json','RobWork/doc/industrial-robot-design/reuse/audit-contract.md:77'],'reproduction':'5项只在内存替换，均确认完整源码摘要改变而该关系sources/inputs/auditEvidence摘要全部不变；未写仓库文件、未声称真实门禁变异运行或C++缺陷复现。','requestedChange':'保留精确函数sources和qualifiedSymbol，为受影响关系补绑定真正决定行为的同文件常量/raw字符串；可将对应StatisticsTest.cpp或XacroExpandTest.cpp全文件加入inputs，或绑定精确依赖区段。同步相关摘要后仅复验受影响登记，无需改产品/原测试或放宽容差。','reproducedProductDefect':False}
semantic=[
 {'area':'定义与作用域','status':'PASS','observation':'43条新增关系与43个定义逐一一致。直接审读3份C++测试上下文、2个ESModule的全部20个命名函数及URDF顶层fixture组；匿名namespace、FakeContext成员作用域及模块身份正确。makeXacroSource:128–158包含默认includeBytes={}之后的真实函数体；makeBasicSource:202–205无raw字符串误截，fixtureGeneration:42–149明确是顶层语义组。此项不消除B16-P2-01的函数外上下文绑定缺口。'},
 {'area':'Pareto测试helper','status':'PASS','observation':'makeCid仅固定字节身份，candidate装两个指标槽，makeTwoObjectives调用生产makeObjectiveSet，idsOf仅投影既有entries。测试断言针对实际Pareto生产接口，无第二生产支配核心。'},
 {'area':'Xacro测试helper','status':'PASS_WITH_FINDING','observation':'真实复用ContentDigester、标准容器和sort；fake路径/mtime、缺叶/环是防御注入，不证明真实IO安全。makeBasicSource的raw常量缺绑定见B16-P2-01。'},
 {'area':'Statistics测试oracle','status':'PASS_WITH_FINDING','observation':'独立max(1e-15,1e-12*abs(expected))与core C4不同；rowAt五分项零而tauTotal可非零，登记没有继承旧恒等式注释。解析支撑力与回转力矩限定竖直移动静止/水平回转，单位N/N·m正确；CanonicalModelBuilder构模及FakeContext替身用途准确。相关决定性常量缺绑定见B16-P2-01。'},
 {'area':'动力学黄金oracle','status':'PASS','observation':'二连杆rneaReference实际为Y轴Lagrange标量M/C/G+摩擦、外力零，非递归RNE；非均匀逐区间梯形、严格大于首峰、JS===不保证逐bit等值、Complete与mandatory全在场限制均如实登记。消费者DynGoldenDatasetTest.cpp:432起读取expected，501–510有独立r×F第三静力矩锚。TrapMethod.hpp:38–49统一h及求和序、Random全局unsigned种子不等价，未授权新生产核心。'},
 {'area':'优化黄金oracle','status':'PASS','observation':'Node crypto/Buffer LE/-0归零、64位SplitMix与53bit采样、两维LHS和基线首位、9事实6可行、C4以b为reference、去重首见/非支配分层/严格三键排序均与源码一致。该独立定义式仅用于测试；零筛选固定事实不证明全优化编排。'},
 {'area':'生成器执行与消费target','status':'PASS','observation':'Node是已登记运行器，CMake实际编译3个消费测试目标，不编译或自动执行mjs。全部21条生成器关系targetRelation明确generated-artifact-consumer；对应CMake包含DynGoldenDatasetTest/OptGoldenDatasetTest/GoldenImportTest，消费者读取真实dataset expected。未重跑29脚本，其独立执行验收由另一验收者负责。'},
 {'area':'F-959与历史结论','status':'PASS','observation':'旧URDF脚本仍写unsupported=8和joint-velocity-limit×5，缺mappedVelocityCount；当前expected为unsupported=3/mappedVelocityCount=5，GoldenImportTest.cpp:430–440消费映射计数。登记明确29进程exit0不等于JSON一致/数学PASS，未宣称已运行C++消费者复现失败。保留F-959 open，B14/B15返工PASS仅关闭各自单项；全覆盖NOT PASS没有被抹除。'}
]
write('semantic-checks.json',semantic)
archiveGroups=[]
for name in sorted(set(x['archive'] for x in archives)):
 xs=[x for x in archives if x['archive']==name];archiveGroups.append({'archive':name,'files':len(xs),'rawMatches':sum(x['rawMatch'] for x in xs),'newlineOnly':sum(x['newlineOnly'] for x in xs),'mismatches':sum(not x['pass'] for x in xs)})
toolHashes=[]
for p in [*(R/f'RobWork/scripts/industrialrobot/{n}' for n in ['validate-task.ps1','validate-docs.ps1','validate-reuse.ps1','reuse-contract.ps1']),B/'audit-b16.py',B/'freshness-probe.py',B/'write-acceptance.py']:
 toolHashes.append({'path':str(p),'rawSha256':hashlib.sha256(p.read_bytes()).hexdigest()})
report={'schemaVersion':'ird-independent-acceptance/1','taskId':'REUSE-T01','batch':'B16','result':'FAIL','verdict':'FAIL','scope':'仅B16新增43关系的语义登记、定义/摘要/实际上下文、Node与产物消费目标、F-959表述、3组归档保真和3项门禁。不授全T01/T02，不包含29脚本独立重跑。','reviewedCommit':facts['reviewedCommit'],'reviewedParentCommit':facts['reviewedParentCommit'],'repo':str(R),'reportRoot':str(B),'createdAt':datetime.datetime.now(datetime.timezone.utc).isoformat(),'facts':facts,'counts':{'newRelations':43,'hashChecks':341,'hashFailures':0,'hashKinds':dict(collections.Counter(x['kind'] for x in hashes)),'searchReplays':6,'searchHitSetMismatches':0,'searchOrderOnly':5,'targetChecks':43,'archiveFiles':123,'archiveRawMatches':71,'archiveNewlineOnly':52,'archiveContentMismatches':0,'gatePassed':4,'gateSkipped':0,'gateFailed':0,'semanticBlockingFindings':1,'freshnessProbeCases':5},'findings':[finding],'checks':checks,'semanticChecks':semantic,'archiveGroups':archiveGroups,'notRun':['C++构建/消费者测试','GUI测试','数值/性能验收','DeepEvidence','29个生成器重跑（另验收者负责）','其余批次语义复验','远程推送/CI交付核验'],'boundaries':{'productChanged':False,'sourceDiffEmpty':True,'noCommitPushOrMerge':True,'cleanIndependentCheckout':True,'fullStagePass':False,'remoteVerified':False,'newRelationsRemainPending':True},'limitations':['validate-docs输出NOT AUDITED evidence。','validate-reuse仅治理检查，functions=612、closedFiles=0，不代表算法合规。','6个检索命中行集合一致，其中5个原始输出只有rg并行输出顺序差异。','原始归档.log/.frozen.txt按保真保留并从scoped whitespace检查排除；未清洗证据。','allRelations=612关系，distinctFunctionIds=611源于原有重复ID；新增43个ID唯一且无旧行变动，未将存量ID关系混作本批缺陷。'],'toolHashes':toolHashes}
write('acceptance.json',report)
write('acceptance.md',f'''# B16 独立语义登记验收

结论：**FAIL**。冻结 `{facts['reviewedCommit']}`，直接父 `{facts['reviewedParentCommit']}`。只覆盖B16新增43关系，不授全T01 PASS或T02准入；29脚本重跑由另一验收者负责。

**B16-P2-01（P2，OPEN）：5条helper未绑定函数外决定性测试上下文。**

- StatisticsTest.cpp:97的kGoldenRelTol不在expectNearRel登记区段103–108；184–189的质量/半径/惯量/重力常量不在prismAxialForce、revTorque、makePrismRevModel登记区段。inputs只有CMake与TwoLinkFixture.hpp。
- XacroExpandTest.cpp:186–200的kBasicXacro raw字符串不在makeBasicSource的202–205区段，inputs只有CMake与XacroExpand.hpp。
- 5项仅内存替换分别改变容差、解析期望、模型质量或Xacro字节/digest/名称，但对应全部sources/inputs/auditEvidence摘要不变。依据audit-contract.md:77，真实行为变化必须使相关证据失效。详见freshness-probe.json；没有写仓库文件，没有声称C++或真实门禁变异运行。

返工只需补绑定真正影响这些关系的上下文（可将相应cpp全文件加入inputs或绑定精确依赖区段），更新受影响摘要后独立复验。保持精确函数区段，不修改原测试、产品或容差。

其余已核：43个新增ID/定义精确对应，旧矩阵行无变动；optimization 4、modeling 9、dynamics 9、foundation-governance 21。43个函数/顶层组区段完整，匿名namespace/FakeContext/ESModule作用域准确，makeXacroSource默认{{}}没有误作函数体。341摘要全匹配、6检索命中集合一致、43目标与真实CMake/消费者关系一致。测试独立oracle用途及适用域、Node执行与generated-artifact-consumer区别、F-959未夸大，详见semantic-checks.json。已匹配摘要不消除上述新鲜度缺口。

| 验证 | 结果 |
| --- | --- |
| validate-task | PASS，exit0 |
| validate-docs | PASS，exit0；NOT AUDITED evidence |
| validate-reuse，冻结SHA | PASS，exit0；仅治理、closedFiles=0 |
| scoped git diff --check | PASS，exit0；排除保真原.log/.frozen.txt |

门禁passed=4、skipped=0、failed=0；语义阻断1项。初次只排除.log的whitespace检查exit2来自原frozen摘录的尾空格，日志保存在scoped-diff-check-initial.log；未清洗归档原件。

三组历史归档123文件：71原始字节相同、52仅CRLF/LF、0内容偏差。B14-r2为23（20原样+3换行）、B15-r2为33（31+2）、全覆盖为67（20+47）。B14/B15原单项返工PASS和全覆盖NOT PASS及原冻结SHA均保持原样；本批未扩大其结论。

零产品/上游/vcpkg变动，独立checkout干净，未修改实施现场、提交、推送或合入。NOT RUN：C++/GUI/数值性能、DeepEvidence、29生成器重跑、其余批次、远程交付核验。

完整结果与命令见acceptance.json；逐项证据见facts.json、hash-checks.json、definition-checks.json、semantic-checks.json、target-checks.json、search-replays.json、archive-checks.json、freshness-probe.json以及task/docs/reuse日志与exit文件。
''')
assert not subprocess.check_output(['git','-C',str(R),'status','--porcelain=v1'],text=True).strip()
print('Report written: FAIL; B16-P2-01 OPEN; frozen checkout clean.')
