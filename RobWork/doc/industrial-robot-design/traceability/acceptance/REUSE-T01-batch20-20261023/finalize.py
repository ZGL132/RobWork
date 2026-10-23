import pathlib,json,subprocess,hashlib,xml.etree.ElementTree as E,shutil
R=pathlib.Path(__file__).parent; repo=R/'repo-isolated'; defs=json.loads((repo/'RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01/batch20-definitions.json').read_text(encoding='utf-8-sig'))
notes=[
'SDK112注入实际误归WriteRejected；70硬编码与原F2相互掩盖，F-963仍open，不扩展到同族未跑路径。',
'调用core规范身份后核67长度与cv-前缀再取64hex；与ObjectStore重复职责保留pending，未新增摘要内核。',
'filesystem错误亦归nullopt、ifstream打开失败AccessDenied，读取中途bad未检查；IOUtil长度/宽路径/错误差异真实存在。',
'读回长度及codec::contentVersionOf真实委托core摘要；未将JSON可解析充作完整性。',
'仅可空IDiagnosticsSink委托，不构造第二诊断服务。',
'本地稳定码字面/中文context-cause-action真实存在；正式diagnostics模板权威须继续追，pending不豁免。',
'同类core记录组装，路径detail入cause；未把文案当业务专属结项。',
'OID与CV规范文本字典序供std::set/map严格弱序；源全限定名含MetadataRefLess，不受简短functionId影响。',
'必需非owning三部件fail-fast，bus/sink可空，注入方保活，无模型创建。',
'projectDir/.staging只做项目格式路径组合，不是框架名称解析。',
'projectDir/objects由project格式所有者派生。',
'projectDir/revisions由project格式所有者派生。',
'规范RevisionId目录，不自行重建身份格式。',
'OID规范文本与CVhex派生，与ObjectStore重复薄逻辑需评估收口，不授权通用门面。',
'atomic序号+thread_local mt19937_64两次低32位组16hex；非计算可重复随机流，不承诺绝无碰撞。全局Random不直接等价线程隔离。',
'计划字段校验、原索引分配/derive/校验、codec摘要、原AtomicFile七段编排。守卫武装晚于三步索引注册；HEAD切换后撤守卫，事件/清理不回滚；自校验仅比HEAD字段。未证明全部故障。',
'IFileOps只增publish+ObjectStore已有内容核对共享判定，竞态最多2轮；已发布对象不删。',
'既有bus顺序提交、std::exception最多重试一次，开发诊断；无新消息队列。',
'局部索引装载/闭包/对象核查，目录残留分类排序；清单仅parse且未对HEAD摘要实算，三个相关反例重现。目录错误等尚未跑。',
'readAllBytes→parseHeadRecord，缺失StoreCorrupt，不会核清单。',
'可空sink开发通道，非正式码/文案替代。',
'局部析构只在armed时摘本次索引；范围/全限定局部类型真实，不能遮蔽武装前异常。',
'INVALID_HANDLE_VALUE失败清句柄并取GetLastError；成功清错误码。',
'BOOL转换只失败取GetLastError；成功0。',
'CreateFileW独占CREATE_ALWAYS和WRITE_THROUGH，原暂存路径；不能用普通读工具替掉持久写。',
'Win32同步WriteFile/DWORD长度，空块成功、短写失败；短写GetLastError残留只静态待核，未实跑。',
'直接FlushFileBuffers，薄失败透传；持久写不同于发布可见性。',
'直接CloseHandle，薄失败透传。',
'MoveFileExW flags0不覆盖；同卷调用约束，不附加断电保证。',
'REPLACE_EXISTING|WRITE_THROUGH既有切换；不证明断电后必新值。',
'标准filesystem create_directories/error_code，不重写遍历器。',
'标准remove_all/error_code，只临时清理，非对象GC。',
'原64KiB常量分块，open/write/flush/显式close失败透传；提前返回Guard清句柄，部分暂存保留。',
'委托IFileOps::publishNew；业务CAS/共享校验留上层。',
'委托replaceExisting；上层决定唯一HEAD提交点。',
'默认持静态无状态Win32FileOps非owning，初始化标准线程安全，不证明所有上层并发。',
'注入拒null，保活归调用方，没有新文件后端。',
'提前返回真实close；正常close前dismiss；析构closeResult不会传播覆盖主要错误，局部嵌套范围无遗漏。',
'命令槽后acquireInFlight在try前；S1/S2/executePlan委托；仅StoreError值化，其他异常透传，排队关闭并发尚未跑。',
'writer锁内复制权威metadata，锁外保原branches顺序投影；不是WorkCell元数据重建。',
'锁内取权威tip后沿不可变parent取原索引；unknown非法参数、悬挂损坏；maxCount0空但仍校验分支。reserve和并发未验证。',
'存在/open/bad失败nullopt，空字节合法；与其他读helper错误语义有差异，IOUtil不能直接等价。',
'使用SDK磁盘满/拒绝访问；未独列quota，不能无差别替换其余映射。',
'任务/manifest/写权校验后原codec摘要、writer锁内终局与幂等、条目size/hash、原子manifest后终结。幂等先于条目检查；writerGuard覆盖事件/票据释放，锁外注释不是证明。',
'sessions锁内置态摘登记并移票据，锁外reset只指sessions锁；调用者可仍持writer锁。',
'core事件只携TaskIdentity，两次bus尝试；noexcept内事件make在try前，异常保证待审，不宣称死锁/崩溃实证。',
'scan后搬运核projectId/schema/format、装载原索引元数据/adoptHeadSeq；无HEAD清单摘要，公开open真实接受错误摘要。'
]
assert len(notes)==len(defs)==47
review=[{'functionId':d['functionId'],'source':d['source'],'review':'PASS for pending audit registration only','conclusion':n} for d,n in zip(defs,notes)]
(R/'semantic-review.json').write_text(json.dumps(review,ensure_ascii=False,indent=2),encoding='utf-8')
build=R/'build'; proj=build/'ird_project_boundary_audit.vcxproj'; compile=E.parse(proj).getroot();ns={'m':'http://schemas.microsoft.com/developer/msbuild/2003'}
wiring={'generator':'Visual Studio 17 2022','arch':'x64','freshBuild':True,'sources':[x.attrib['Include'] for x in compile.findall('.//m:ClCompile',ns) if 'Include' in x.attrib],'projects':[x.attrib['Include'] for x in compile.findall('.//m:ProjectReference',ns) if 'Include' in x.attrib],'usesOriginalTestInclude': 'IRD_PROJECT_EXISTING_TEST' in proj.read_text() and str(repo).replace('\\','/') in proj.read_text().replace('\\','/'),'sourceDirectory':str(repo/'RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01/probe-r20')}
assert len(wiring['sources'])==2 and wiring['usesOriginalTestInclude']
(R/'build-wiring.json').write_text(json.dumps(wiring,indent=2),encoding='utf-8')
for source,name in [(build/'CMakeCache.txt','generated-CMakeCache.txt'),(proj,'generated-probe.vcxproj'),(build/'ird-project/sdurws_ird_project.vcxproj','generated-project.vcxproj'),(build/'ird-core/sdurws_ird_core.vcxproj','generated-core.vcxproj'),(build/'ird-testkit/sdurws_ird_testkit.vcxproj','generated-testkit.vcxproj')]:shutil.copyfile(source,R/name)
status=subprocess.check_output(['git','status','--porcelain=v1'],cwd=repo,text=True);(R/'git-status-end.log').write_text(status,encoding='utf-8');assert not status
changes=subprocess.check_output(['git','diff','--name-only','HEAD^','HEAD','--','RobWork/RobWorkStudio/src/rwslibs/industrialrobot','RobWork/RobWork/src','RobWork/RobWorkSim/src','vcpkg'],cwd=repo,text=True);(R/'product-upstream-diff.log').write_text(changes);assert not changes
sha=subprocess.check_output(['git','rev-parse','HEAD'],cwd=repo,text=True).strip();parent=subprocess.check_output(['git','rev-parse','HEAD^'],cwd=repo,text=True).strip();assert sha=='eddb82a8e29f91660b048a69f4ec90c3d0a0e63a' and parent=='daee4f8905587b05cd1bab88bb570b8dc240097c'
for name,want in [('configure',0),('build',0),('run',1),('verify-task',0)]: assert int((R/(name+'.exit.txt')).read_text(encoding='utf-8-sig'))==want
meta=json.loads((R/'metadata-audit.json').read_text());assert meta['added']==47 and not meta['removedOrChanged'] and meta['allAddedPending'] and not meta['bindingFailures'] and not meta['definitionFailures'];arc=json.loads((R/'archive-original-byte-checks.json').read_text());assert len(arc)==161 and all(x['originalRawMatches'] and x['workingCopyMatchesGitBlob'] for x in arc)
native=json.loads((R/'native-xml-exit-checks.json').read_text());assert all(x['testcasesMatch'] and x['nativeSummaryMatch'] and x['runExit']==1 for x in native)
for file in ['candidate-checks.json','original-evidence-checks.json']:assert all(x['match'] for x in json.loads((R/file).read_text()))
accept={'schemaVersion':'ird-independent-acceptance/1','taskId':'REUSE-T01','batch':'B20','reviewedCommit':sha,'parentCommit':parent,'decision':'PASS','scope':'仅47条project pending审计登记、UTIL-FILE边界、F-962/F-963与6项红证复现，以及161归档原件保存；非产品合规通过','blockingAuditFindings':[],'definitionCount':47,'contextAndEvidenceBindings':517,'candidateHashChecks':6,'originalEvidenceBindings':9,'matrixDelta':{'added':47,'existing72RowsUnchanged':True,'allAddedPending':True},'searchReplay':{'commands':6,'sortedLineSetsMatch':True,'exitCodes':[x['exitCode'] for x in meta['searches']]},'archivePreservation':{'B17':101,'B18':60,'total':161,'rawByteMatchesOriginalAndFrozenGitBlob':True,'priorScopeUnchanged':True},'build':{'fresh':True,'generator':'Visual Studio 17 2022','arch':'x64','configureExit':0,'buildExit':0,'target':'ird_project_boundary_audit','originalCoreProjectTestkitTargets':True,'originalTxEngineTestFixture':True},'test':{'filter':'ReuseProjectBoundary.*','passed':2,'failed':4,'skipped':0,'notRun':0,'exitCode':1,'verdict':'EXPECTED RED REPRODUCED','nativeXmlExitAgreement':True},'findings':{'F-962':{'status':'open','independentlyReproduced':['wrong HEAD manifest digest scan accepts','parseable manifest timestamp byte change scan accepts','public ProjectStoreFactory::open accepts wrong HEAD digest']},'F-963':{'status':'open','independentlyReproduced':'original IFileOps SDK ERROR_DISK_FULL=112 injection returns WriteRejected, committed old bytes unchanged; not real exhausted disk'}},'gates':{'verifyTaskExit':0,'machineCommands':3,'validateTask':'PASS','validateReuse':'PASS governance only','validateDocs':'PASS','DeepEvidence':'NOT RUN','functions':744,'inventory':1401,'DEBT':16,'UNRESOLVED':1399,'closedFiles':0},'notRun':['原project完整套件（虽被注册）','其他ObjectStore/StoreClone错误映射路径','真实磁盘耗尽','断电/进程kill/崩溃矩阵','命令排队/关闭及归档事件票据并发','性能','GUI','全T01退出/T02迁移/算法合规'], 'remainingStaticRisks':['readAllBytes中途读取bad/目录错误归类','幻影守卫武装前注册异常','短写GetLastError及同卷/持久性细节','本地中文诊断正式模板消费','archive writerGuard覆盖事件/票据释放及noexcept事件make保证'],'productUpstreamChanges':False,'cloneFinalGitClean':True,'implementationTreeModified':False,'commitPushMerge':False,'operationalEvents':['首次clone默认硬链接失败Improper link；改用--no-hardlinks全新目录成功；未复制build/vcpkg/.cache。'],'evidenceRoot':R.as_posix()}
(R/'acceptance.json').write_text(json.dumps(accept,ensure_ascii=False,indent=2),encoding='utf-8')
md=f'''# REUSE-T01 B20 独立验收

结论：**PASS，仅本批 pending 审计登记和证据保存/反例复现**。冻结 SHA `{sha}`，父提交 `{parent}`。F-962/F-963 **保持 open**；本结论不代表产品合规、测试全绿、全 T01 退出或 T02/算法替换准入。

- 全新隔离 clone，core.longpaths=true、core.autocrlf=false。47 条新增关系均 pending，旧 72 行原样；47 精确源区段、517 项区段/上下文/证据绑定、6 项候选文件/目标绑定、9 项原 probe 证据摘要均匹配。六组原检索独立重放退出码及行集合一致。逐定义意见见 semantic-review.json，完整上下文摘录见 all47-source-review.txt。
- B17 101 份及 B18 60 份，共 161 份归档原件逐字节匹配各原始 Temp 文件及冻结 Git blob；B17 manifest 所列100文件的原始大小/哈希亦全匹配。原局部 PASS 的范围和 open 缺陷没有扩大或改写。
- UTIL-FILE仍为candidate。IOUtil::readFile实际是narrow string/ifstream/int长度/vector<char>/rw异常；遍历不保证闭包语义或排序，不提供Windows宽路径持久写、同卷不覆盖发布、唯一HEAD提交或崩溃恢复协议。未以无同名命中断言上游绝无能力，未授权新事务核心。
- TxEngine实际复用AtomicFile、ObjectStore、RevisionIndex、codec::contentVersionOf→core::ContentDigester与既有bus；提交点前后失败不同，守卫武装前注册异常并未被掩盖。项目修订/写协议保持project owner，不转移至runtime；纯路径组合不是模型名称真值。CommandService只委托既有S1/S2/executePlan；QueryPort从权威metadata/索引读取历史。Archive finalize复用原编码/摘要/原子manifest，幂等先于条目检查，writerGuard仍覆盖事件与票据释放，注释“锁外”没有被采信为锁序证明。诊断中文文案与正式模板权威留pending。

VS2022 x64开发环境，原probe CMake fresh配置 **exit0**，Release目标构建 **exit0**。实际源接线为clone内原core/project/testkit目标、原TxEngineTest.cpp夹具和TestMainReport.cpp；未复制生产源集。绝对路径单exe，仅过滤 `ReuseProjectBoundary.*`，原注册套件 **NOT RUN**。模型测试无GUI。

独立真实执行：**2 passed / 4 failed / 0 skipped / 0 notRun，exit1**。独立与冻结最终原件、首次4项探索的native JSON/XML/退出码逐例一致。6项失败语义如下：

1. 健康scan与健康公开open两正例通过。
2. HEAD摘要改为语法合法错误值仍scan true；清单合法时间字节变化而原HEAD摘要不变仍scan true；补合法project.json后的公开ProjectStoreFactory::open也接受错误摘要并报告完整。F-962三反例已复现，不外推全部篡改/崩溃情形。
3. 原IFileOps注入真实SDK ERROR_DISK_FULL=112，commit拒绝且原HEAD/对象/修订字节保持；唯一失败为实际WriteRejected(7)而非DiskFull(8)。原映射/原F2均用70，F-963已复现。不是实际磁盘耗尽实验；ObjectStore/StoreClone同族仅静态候选。

冻结clone运行verify-task exit0，三条机器命令validate-task/reuse/docs全执行。reuse仅治理PASS：744 functions、inventory=1401、DEBT=16、UNRESOLVED=1399、closedFiles=0；DeepEvidence NOT RUN。全project、真实磁盘满、断电/进程kill、关闭/归档并发、性能、GUI、T01退出/T02均未执行或未授权。静态风险不冒充运行缺陷。

检出最终Git干净，父提交至冻结的产品/上游/vcpkg源码diff为空；未修改实施现场，无提交、推送、合并或实施代理。首次硬链接clone失败记录保留，后用--no-hardlinks新目录成功。证据均在 `{R.as_posix()}`，归档包排除repo、repo-isolated及build。
'''
(R/'acceptance.md').write_text(md,encoding='utf-8')
print('B20 SCOPED ACCEPTANCE PASS; expected red 2 passed/4 failed; 161 raw originals match; frozen clean; report emitted')
