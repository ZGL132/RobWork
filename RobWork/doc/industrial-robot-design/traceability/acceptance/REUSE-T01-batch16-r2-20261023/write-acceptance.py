import pathlib,json,hashlib,subprocess,datetime,collections
B=pathlib.Path(__file__).parent;R=B/'repo';D=R/'RobWork/doc/industrial-robot-design'
def load(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def write(n,v):(B/n).write_text(v if isinstance(v,str) else json.dumps(v,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
f=load(B/'facts.json');assert f['reviewedCommit']=='3a8375eac74408d06ba8999013635428aa6c90e7'
assert all(f[k] for k in ['all22Bound','bindingManifestMatches','sourcesAllUnchanged','originalFailPreserved','generatorPassPreserved','sourceDiffEmpty','clean'])
assert all(f[k]==0 for k in ['matrixChangeFailures','hashFailures','archiveMismatches','scopedDiffCheckExit'])
assert f['originalProbesNowRejected']==5 and f['hashChecks']==154 and f['archiveFiles']==151
checks=[]
for n,args in [('task',['-TaskFile',str(D/'tasks/REUSE-T01.json'),'-RepoRoot',str(R)]),('docs',['-RepoRoot',str(R/'RobWork')]),('reuse',['-RepoRoot',str(R),'-Head',f['reviewedCommit'],'-TaskFile','RobWork/doc/industrial-robot-design/tasks/REUSE-T01.json'])]:
 code=int((B/f'{n}.exit.txt').read_text(encoding='utf-8-sig').strip());assert code==0
 checks.append({'name':'validate-'+n,'status':'PASS','exitCode':code,'command':['pwsh','-NoProfile','-File',str(R/f'RobWork/scripts/industrialrobot/validate-{n}.ps1'),*args],'log':n+'.log'})
checks.append({'name':'scoped git diff --check','status':'PASS','exitCode':0,'command':['git','-C',str(R),'diff','--check','HEAD^','HEAD','--','<changed-files.log minus diff-check-exclusions.log>'],'log':'scoped-diff-check.log','scope':'仅排除逐文件已核保真的归档原.log，未清洗原日志。'})
ars=load(B/'archive-checks.json');groups=[]
for name in sorted(set(x['archive'] for x in ars)):
 xs=[x for x in ars if x['archive']==name];groups.append({'path':name,'files':len(xs),'rawMatches':sum(x['rawMatch'] for x in xs),'newlineOnly':sum(x['newlineOnly'] for x in xs),'contentMismatches':sum(not x['pass'] for x in xs)})
toolHashes=[]
for p in [*(R/f'RobWork/scripts/industrialrobot/{n}' for n in ['validate-task.ps1','validate-docs.ps1','validate-reuse.ps1','reuse-contract.ps1']),B/'check-rework.py',B/'write-acceptance.py']:
 toolHashes.append({'path':str(p),'rawSha256':hashlib.sha256(p.read_bytes()).hexdigest()})
finding={'id':'B16-P2-01','priority':2,'status':'CLOSED','resolution':'本批全部22条C++ helper已各自补绑定对应来源cpp全文inputs，精确sources/全限定作用域不变；覆盖同文件常量、raw字符串、相邻helper、类默认成员及上下文。原5项内存反例仍保持精确source区段摘要不变，但新增来源全文inputs摘要均失配，已能使相关登记证据失效。','evidence':['binding-checks.json','matrix-change-checks.json','hash-checks.json','freshness-probe.json','RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01/batch16-rework.json'],'proofBoundary':'仅内存替换和逐字段摘要对照，未写仓库文件、未执行C++或真实门禁变异运行。'}
report={'schemaVersion':'ird-independent-acceptance/1','taskId':'REUSE-T01','batch':'B16-rework-r2','result':'PASS','verdict':'PASS，仅B16-P2-01返工独立复验','scope':'仅关闭B16-P2-01：22条C++ helper上下文绑定、5原反例、摘要与归档保真、相关门禁。不重授整批B16、全T01 PASS或T02准入，不复验B17，不重跑29脚本。','reviewedCommit':f['reviewedCommit'],'reviewedParentCommit':f['reviewedParentCommit'],'comparisonBase':f['comparisonBase'],'repo':str(R),'reportRoot':str(B),'createdAt':datetime.datetime.now(datetime.timezone.utc).isoformat(),'facts':f,'findings':[finding],'closedFindings':['B16-P2-01'],'newFindings':[],'checks':checks,'counts':{'affectedRelations':22,'distinctSourceFiles':3,'hashChecks':154,'hashFailures':0,'originalProbeCases':5,'originalProbesNowRejected':5,'archiveFiles':151,'archiveRawMatches':144,'archiveNewlineOnly':7,'archiveContentMismatches':0,'gatePassed':4,'gateSkipped':0,'gateFailed':0,'semanticBlockingFindingsInRework':0},'archiveGroups':groups,'historicalConclusionPreservation':{'originalSemanticAcceptance':{'path':'RobWork/doc/industrial-robot-design/traceability/acceptance/REUSE-T01-batch16-r1-20261023/acceptance.json','result':'FAIL','reviewedCommit':f['comparisonBase'],'originalFindingStatus':'OPEN'},'generatorAcceptance':{'path':'RobWork/doc/industrial-robot-design/traceability/acceptance/REUSE-T01-batch16-generators-20261023/acceptance.json','verdict':'PASS','scope':'仅29生成器隔离执行证据、91 dataset JSON对照与F-959复现及消费者源码核对；不含43行语义验收。','reviewedCommit':f['comparisonBase']},'F959':'仍为既有open；本复验不关闭或重新判断此项。'},'boundaries':{'productChanged':False,'sourcesUnchanged':True,'matrixScope':'仅B16涉及的dynamics/modeling/optimization/foundation-governance四份矩阵比较；除22个全文inputs新增外无变化。','B17':'父提交7eb991346含其他core登记，本复验不授其结论。','cleanIndependentCheckout':True,'fullStagePass':False,'remoteVerified':False,'noCommitPushOrMerge':True},'notRun':['C++构建/消费测试','GUI','数值/性能','DeepEvidence','29个生成器重跑','B16整批语义重新验收','B17或其他批次验收','远程推送/CI交付核验'],'limitations':['validate-docs输出NOT AUDITED evidence；validate-reuse仅治理，functions=651、closedFiles=0，不代表算法合规或阶段准入。','原归档.log仅从scoped whitespace检查中排除，精确排除表与保真比较均留存。','本次PASS是单项返工复验，不改写原FAIL或扩大生成器PASS。'],'toolHashes':toolHashes}
write('acceptance.json',report)
write('acceptance.md',f'''# B16-P2-01 返工独立复验

结论：**PASS，仅B16-P2-01返工复验，该项关闭。** 不重授整批B16、全T01 PASS或T02准入，不复验B17，不重跑29脚本。

冻结SHA：`{f['reviewedCommit']}`；直接父：`{f['reviewedParentCommit']}`；原FAIL比较基准：`{f['comparisonBase']}`。独立现场：{R}。

22条C++ helper均已绑定来源cpp全文inputs（StatisticsTest 9、XacroExpandTest 9、ParetoTest 4），原精确sources/全限定符号保持不变。对B16涉及的四份矩阵比较，只有这22处inputs增加，其余数据与原FAIL基准一致；batch16-rework.json的22项绑定完全对应。全文绑定覆盖同文件常量、raw字符串、相邻helper和类默认成员，无同批未补的C++ helper。

154项摘要全部匹配。5个原内存反例现在均会使新增全文inputs摘要失配：容差、质量、解析期望/模型参数以及Xacro字节改变时，证据可失效。精确函数区段摘要仍保持不变，证明此次补充确实覆盖原漏点。仅内存替换，没有写仓库文件，没有执行C++或真实门禁变异运行。

| 检查 | 结果 |
| --- | --- |
| validate-task | PASS，exit0 |
| validate-docs | PASS，exit0；NOT AUDITED evidence |
| validate-reuse，冻结SHA | PASS，exit0；仅治理、closedFiles=0 |
| scoped git diff --check | PASS，exit0；仅排除保真原.log |

门禁passed=4、skipped=0、failed=0。原FAIL与生成器报告共151文件：144原始字节相同、7仅CRLF/LF、无内容偏差。原语义FAIL保留45文件（42原样+3换行），生成器原报告保留106文件（102+4）；原FAIL/OPEN及生成器限域PASS保持不变，F-959未关闭。

自原FAIL基准起产品/上游/vcpkg无变动，独立checkout干净，未修改实施现场、提交、推送或合入。父提交包含B17其他登记，本复验不覆盖。NOT RUN：C++/GUI/数值性能、DeepEvidence、29生成器重跑、整批B16/B17或其他批次、远程交付核验。

详细命令、脚本SHA256与边界见acceptance.json；证据见facts.json、binding-checks.json、matrix-change-checks.json、hash-checks.json、freshness-probe.json、archive-checks.json及task/docs/reuse日志和exit文件。
''')
assert not subprocess.check_output(['git','-C',str(R),'status','--porcelain=v1'],text=True).strip()
print('PASS: B16-P2-01 closed; reports written; checkout clean.')
