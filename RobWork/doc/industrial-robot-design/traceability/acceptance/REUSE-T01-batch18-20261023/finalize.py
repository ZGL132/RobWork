import pathlib,sys,json,subprocess,hashlib,datetime,collections
root=pathlib.Path(sys.argv[1]);repo=root/'repo';base=repo/'RobWork/doc/industrial-robot-design';defs=json.loads((base/'traceability/reuse/REUSE-T01/batch18-definitions.json').read_text(encoding='utf-8-sig'))
notes=[
'仅snprintf %.17g；格式依赖既定C locale，不是新数值核心。',
'真实查core rad token并对缺失fail-fast；单位权威未旁路。',
'复用core派生只读来源，来源事实不升级为可信度结论。',
'码选择和core记录组装可核；中文context/cause/action仍需diagnostics正式消费审查。',
'有限Provided/非有限Invalid原串分轨，携SI单位及来源。',
'形状/唯一身份/成对限位/continuous必填先fail-fast，不吞并数据发现。',
'已发布策略、非有限位置、名称守卫后输出首违例/有符号裕量/严格近限位；Models布尔不等价。端点有限及极值溢出未运行，finalized仅本DTO。',
'无状态接口工厂，只委托标准make_unique。',
'非持久化诊断数值格式，C locale约束保留。',
'registryCode及core记录真实委托，正式中文文案仍待诊断权威核对。',
'SI值和UserProvided来源承载，不改变来源等级。',
'仅组比较型两侧，复用本地comparativeValue及core数据结构。',
'域映射到既有m/1/rad token，不自建单位换算。',
'错误分类到0/1期望界的policy域逻辑，真域窗仍由PolicyThreshold。',
'本地中文域窗文案真实存在，应继续pending并追正式diagnostics模板。',
'精确五角色词表；与会话switch存在维护重复，未宣称消除。',
'Object/Role/Group键用标准字段比较，与codec实现重复待收口。',
'委托同一比较器规整无序对端，不拼拆框架全名。',
'拷贝输入并用标准sort，codec同键谓词重复仍在。',
'同kind精确等值及Object×Role上下文交集；不支持Group异kind展开，不是完整等价关系。',
'同序及反序组合，复用单端判定，非通用集合引擎。',
'本地级别中文文案待diagnostics正式显示链核对。',
'标准sort/unique，等价由双向比较器定义，非新算法平台。',
'非有限分类先于core单位转换，再由PolicyThreshold核域窗；异常转译范围明确。',
'唯一管线按schema/词表/阈值/冻结4pi/冲突/适用范围执行，错误拒发布，identity和发布工厂真实调用。Group定义应答不能承诺会话成员可展开。',
'委托唯一管线并移动返回，不计算外部sourceVersion。',
'同管线产出诊断；临时发布值被丢弃，无外部持久化操作。',
'返回该TU词表副本；没有把另一TU switch变成机械单源。',
'枚举到五角色字面switch，与解析词表维护重复已揭示。',
'精确反查switch，未知返回nullopt。',
'按ObjectId字节序规范无序对，不以Frame名称另作真值。',
'按业务角色域矩阵机械选择，未列组合nullopt，不是几何碰撞核。',
'core真实摘要器+固定magic/form/三身份及长度串；本地u32BE可继续审原语复用，不改身份组成；全零不可能注释不采信。',
'只固化后端描述值；binary-collision+distance文案不能证明distance能力。',
'线性统计Mandatory及InScopeDefault，不另判碰撞。',
'分域线性统计同两状态。',
'线性统计两排除状态，保留事实语义。',
'策略/身份/后端/WC/设备/对象及规则防御校验；仅findDevice读取，Group无成员数据拒绝。',
'Object单点、Role场景枚举；Group不变量异常，不能猜空集。',
'规则展开去自对并用标准map保首条，冲突拒绝属于policy权威，不以框架缺省过滤代替。',
'持值/共享借持保活，作用域门控与优先级真实可核，setup和后端缓存不写WC结构；未证明全部生命周期/并发。',
'真实setup关闭静态默认、include-all；完整名直接进模式API导致F-961。evaluate不消费该setup过滤，不外推碰撞漏检。',
'构造真实ProximityStrategyRW，同评估器的会话共享后端和同锁。',
'返回构造固化描述值，无动态能力证明。',
'唯一公开make入口使用标准make_unique，未另造核心。',
'WC findDevice/findFrame/getObjects只读；hasModel/addModel同锁且属后端缓存；distance动态探测。直构共享后端未共锁、多Object同base首条与并发均待审/未运行。'
]
assert len(notes)==len(defs)==46
review=[{'functionId':d['functionId'],'source':d['source'],'independentReview':'PASS for pending registration only','conclusion':n} for d,n in zip(defs,notes)]
(root/'semantic-review.json').write_text(json.dumps(review,ensure_ascii=False,indent=2),encoding='utf-8')
checks=json.loads((root/'definition-checks.json').read_text());bindings=json.loads((root/'binding-checks.json').read_text());old=json.loads((root/'original-evidence-checks.json').read_text());sr=json.loads((root/'search-replay.json').read_text());native=json.loads((root/'native-xml-exit-checks.json').read_text());assert all(x['stdoutSortedLinesMatch'] and x['stderrMatch'] and x['exit']==x['recordedExit']==0 for x in sr)
assert all(x['match'] for x in old);assert all(x['match'] for x in bindings)
for name,want in [('configure',0),('build',0),('run',1),('verify-task',0),('audit-final',0)]:assert int((root/(name+'.exit.txt')).read_text(encoding='utf-8-sig'))==want
parent=subprocess.check_output(['git','rev-parse','HEAD^'],cwd=repo,text=True).strip()
prefix='RobWork/doc/industrial-robot-design/reuse/functions/policy.json';prev=json.loads(subprocess.check_output(['git','show','HEAD^:'+prefix],cwd=repo));cur=json.loads((repo/prefix).read_text(encoding='utf-8-sig'));before={f['functionId']:f for f in prev['functions']};after={f['functionId']:f for f in cur['functions']};added=set(after)-set(before);assert added==set(x['functionId'] for x in defs);assert all(after[k]==v for k,v in before.items())
(root/'matrix-delta.json').write_text(json.dumps({'addedCount':len(added),'existingRowsUnchanged':True,'addedIds':sorted(added)},ensure_ascii=False,indent=2),encoding='utf-8')
raw_manifest={}
for name in ['semantic-review.json','definition-checks.json','binding-checks.json','candidate-checks.json','original-evidence-checks.json','native-xml-exit-checks.json','search-replay.json','configure.log','build.log','stdout.log','gtest.xml','ird-test-report.json','verify-task.log','run-executable.ps1']:
 raw_manifest[name]=hashlib.sha256((root/name).read_bytes()).hexdigest()
accept={'schemaVersion':'ird-independent-acceptance/1','taskId':'REUSE-T01','batch':'B18','reviewedCommit':'daee4f8905587b05cd1bab88bb570b8dc240097c','parentCommit':parent,'decision':'PASS','scope':'46条policy pending登记、COLLISION/MODEL-BOUNDS候选权限、诊断边界、F-961表述与原probe独立执行；非产品合规通过','findings':[], 'definitionCount':46,'contextAndEvidenceBindings':len(bindings),'candidateHashChecks':19,'originalEvidenceBindings':{'passed':9,'failed':0},'matrixDelta':{'added':46,'existingRowsUnchanged':True},'searchReplay':{'commands':6,'exitCodes':[0]*6,'sortedLineSetsMatch':True,'note':'rg并行枚举前4组行序不同，逐行集合相同；后2组逐字一致。'},'build':{'fresh':True,'cmake':'original frozen probe-r18/CMakeLists.txt','generator':'Visual Studio 17 2022','arch':'x64','configureExit':0,'buildExit':0,'target':'ird_policy_boundary_audit'},'test':{'filter':'ReusePolicyBoundary.*','passed':1,'failed':1,'skipped':0,'notRun':0,'exitCode':1,'verdict':'EXPECTED RED REPRODUCED','nativeXmlExitAgreement':True},'F-961':{'status':'open','independentlyReproduced':True,'impact':'公开setup产物规则不匹配原完整名；未证明碰撞数值漏检'},'gates':{'verifyTaskExit':0,'machineCommands':3,'validateTask':'PASS','validateReuse':'PASS governance only','validateDocs':'PASS','DeepEvidence':'NOT RUN'},'notRun':['原policy完整套件（虽被注册）','Models::inBounds数值适用性','真实碰撞/距离数值','生命周期/并发/性能','诊断正式显示全链','GUI','全T01退出及T02迁移'],'operationalEvents':['首次检出因Windows长路径失败；仅独立clone设core.longpaths后重新检出成功。','首次exe启动因框架DLL目录未入PATH返回-1073741515，未执行用例；保留原记录。按导出目标实际build/RobWork/bin/Release供DLL并沿用已固化PythonDir，VS x64下同exe真实运行2项。','初审脚本对无末换行native报告仅LF规范化产生假摘要差异；按仓库Digest统一末LF后9/9匹配，保留初审日志不登记虚假缺陷。'],'productUpstreamChanges':False,'cloneFinalGitClean':True,'implementationTreeModified':False,'commitPushMerge':False,'artifactsRawSha256':raw_manifest}
(root/'acceptance.json').write_text(json.dumps(accept,ensure_ascii=False,indent=2),encoding='utf-8')
md=f'''# REUSE-T01 B18 独立验收

结论：**PASS（限定本批审计登记与反例执行证据）**。冻结 SHA `{accept['reviewedCommit']}`，父 SHA `{parent}`。F-961 **保持 open**。PASS 不表示测试全绿、产品复用合规、全 T01 退出或准许 T02/T05 实施。

## 语义与权限

- 46 条新增定义逐条审读，定义区段/矩阵源字段一致；506 项外部上下文与审计证据摘要、19 项候选文件/目标摘要均匹配。46 行均 pending，原有矩阵行未改，无 grant。逐条独立意见见 `semantic-review.json`，源码抽取见 `all46-source-review.txt`。
- JointLimits 的已发布策略、形状/非有限/名称守卫、ObjectId排序、首违例、有符号裕量、严格近限位、continuous豁免及来源事实可核。Models::inBounds/QConstraint::makeBounds 为普通闭区间过滤候选，不等价整段业务输出。有限端点缺显式有限校验及差/半宽极值溢出只属未实跑风险；finalized 为本评估 DTO 完成标记。
- PolicyParsing 两入口委托唯一管线；单位真实委托core，域窗委托PolicyThreshold，冻结4π委托JointThresholds，身份及发布委托原codec/工厂。标准排序/去重已用，但角色词表与codec同键排序存在维护重复，尚未消除。Group解析存在性应答不能取代会话成员展开。
- COLLISION 实用ProximitySetup/Rule与ProximityStrategyRW；传入WC仅findDevice/findFrame/getObjects读取，hasModel/addModel写后端会话缓存且持查询锁，不是WC/DWC结构发布。生产评估器共享同后端同锁；直接多会话共享后端未共享锁不能按缺省锁自动判安全。内置RW仅继承CollisionStrategy，动态DistanceStrategy探测才是距离能力依据，描述符字面串不构成能力证明。语义/权限按ARCH §3.6分别核对，无模型构造权授予。
- 诊断仍通过policyDiagCode/registryCode与core记录构造，但调用点中文context/cause/action及domainWindowText/levelText真实存在。ARCH §3.6.3明确diagnostics权威，登记保留pending，须后续追正式模板/转换/显示消费者，未将静态审读冒充全链通过，也未批准第二诊断服务或envelope。

## F-961 与实际执行

原probe CMake通过add_subdirectory编译冻结clone的真实core/testkit/policy目标，别名仅接导出RW目标；探测include原CollisionSessionTest.cpp，buildSession实际调用makeRobWorkCollisionEvaluator()->createSession。未复制实现或自列policy源集。新树位于 `{root.as_posix()}/fresh-build`。

VS2022 x64，原CMake配置exit0，Release目标构建exit0；绝对路径单exe仅过滤 `ReusePolicyBoundary.*`，QT_QPA_PLATFORM=windows。真实结果 **1 passed / 1 failed / 0 skipped / 0 notRun，exit1**；普通点号正例通过，REGEXarm同设备/同帧前缀生产会话规则反例失败于原第42行。独立及冻结原件两套native报告/XML/退出码逐例一致，原件9项摘要按仓库Digest全部匹配。原报告没有末换行，规范口径会补唯一末LF，并无摘要新鲜度缺陷。

上游convertToRegEx把前五字节REGEX去除后解释为regex，runtime合法字符集允许REGEXarm。原生产setup直接传完整名，故match原名为false。evaluate当前按scope逐对调用后端，不使用该setup过滤，因此只实证公开setup规则输出缺陷，**不证明实际碰撞漏检**。F-961范围和open状态准确，后续T05仍须最小适配与原会话回归及权限审查。

## 门禁与现场

`verify-task.ps1 -TaskFile .../REUSE-T01.json -RepoRoot <冻结clone>` exit0；其3条机器命令validate-task、validate-reuse、validate-docs均真实执行通过。reuse仅治理通过（697 functions、closedFiles=0）；docs全库索引DeepEvidence NOT RUN。六组原检索独立重放exit0、行集合全相同，前四组枚举顺序差异不当作内容漂移。

首轮长路径检出失败、首次缺DLL启动（-1073741515，未执行）均保留日志；只在clone启用longpaths，按原导出目标实际DLL路径补当前进程PATH并使用既有SDURWS_IRD_PYTHON_DIR后完成实跑，未重配主构建树、未修改产品。初审摘要算法的假差异及修正日志亦保留。

原policy完整套件虽被注册但 **NOT RUN**，不混入这两项计数。Models::inBounds数值适用性、真实碰撞/距离数值、生命周期/并发/性能、诊断显示全链、GUI与全T01/T02均 **NOT RUN/未授通过**。父提交到冻结SHA的产品/上游/vcpkg源码diff为空；独立clone最终Git干净；实施现场未改，无提交、推送、合并。
'''
(root/'acceptance.md').write_text(md,encoding='utf-8')
print('B18 ACCEPTANCE PASS; 46 pending rows; original CMake fresh VS x64 build exit0; EXPECTED RED 1 passed/1 failed exit1; F-961 open; gates PASS governance only')
