import pathlib,json,hashlib,subprocess
ev=pathlib.Path(__file__).parent; repo=ev/'repo'
r=json.loads((ev/'independent-review.json').read_text(encoding='utf-8'))
checks={n:{'exitCode':int((ev/(n+'.exit.txt')).read_text(encoding='utf-8-sig').strip()),'log':n+'.log'} for n in ['configure','build','tests','task','reuse','docs','diff-check','active-diff-check','inspect']}
assert all(v['exitCode']==0 for n,v in checks.items() if n!='diff-check')
assert checks['diff-check']['exitCode']==2
diff=(ev/'diff-check.log').read_text(encoding='utf-8-sig')
diagnostics=[x for x in diff.splitlines() if ': trailing whitespace.' in x]
assert diagnostics and all(x.startswith('RobWork/doc/industrial-robot-design/traceability/acceptance/REUSE-T01-proposals-20261022/diff.log:') for x in diagnostics)
assert not subprocess.check_output(['git','-C',str(repo),'status','--short'],text=True).strip()
summary=r['testReports'][1]['summary']
report={'schemaVersion':'ird-independent-api-probe-acceptance/1','taskId':'REUSE-T01','attempt':'api-r7','verdict':'pass','reviewedCommit':r['frozenSha'],'baseCommit':r['baseSha'],'scope':'只读验收新增3项ToolSuitabilityTest及common CONFIG接线、probe-r7原件/环境/矩阵绑定；非第五批完整审计、全T01退出或生产迁移','independentClone':str(repo),'trackedStatus':'','checks':checks,'tests':summary,'testFilter':'ReuseAudit.BinaryArchive*:ReuseAudit.LinearInterpolatorExtrapolates*','notRunOriginalTests':19,'protectedFilesUnchanged':r['protectedFilesUnchanged'],'unchangedFromPriorR6FrozenSha':True,'sourceXmlEnvironmentBinding':{'allMatch':True,'associatedFunctions':len(r['associatedFunctions']),'auditBindings':len(r['auditEvidenceBindings']),'details':'independent-review.json'},'semanticOracles':r['independentOracles'],'catalogAndMatrixReview':{'UTIL-BINARCHIVE':'candidate；主机序/格式化string与冻结协议不等价，继续查既有依赖；不授权通用codec新核心。','TRAJ-LINEAR':'标量可用但外推/溢出/舍入/业务守卫需适配；Replay采样维持pending。','related21FunctionsPending':True,'scope':'仅相关候选/工具探测结论，不替代第五批其余条目的语义验收。'},'environmentArtifacts':r['environmentArtifacts'],'environmentLimitation':'既有common DLL raw摘要只标识环境，不证明冻结源码可复现构建来源；模板/头来源通过冻结源码及CMake绑定留存，探测本身fresh构建。','relationCounts':r['counts'],'findings':[],'observations':['全差异git diff --check=2，所有告警仅来自新归档历史验收diff.log原文尾空白；未改写历史证据。排除该归档后的活动文件检查=0。','MSBuild MSB8029临时目录增量构建告警；本次fresh构建成功。','本次只实际运行3项；原19项编译进入目标但被filter排除，不写成22全跑。'],'notRun':['原19项测试重跑','BINArchive一般输入/畸形输入/跨平台/大端机器验证','完整上游测试、生产接线或协议迁移、回放全路径/数值性能验收','HOST-LINES GUI或其他第五批功能验收','全T01审计退出/T02准入','140治理自测/DeepEvidence/远程CI','提交/推送/合入'],'evidenceSha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in ev.iterdir() if p.is_file() and p.name.endswith(('.log','.xml','.json','.ps1'))}}
(ev/'acceptance.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
md=f'''# REUSE-T01 新3项工具API独立验收：PASS

冻结 SHA `{r['frozenSha']}`；直接基线 `{r['baseSha']}`。独立临时clone、detached HEAD，最终git status为空。仅在Temp产出报告和日志；未改父树、产品、上游、vcpkg，未提交/推送。

VS2022 x64从零configure/build既有目标；CMake沿用RobWork CONFIG组件并新增common及RW::sdurw_common，使用既有GTest/testkit报告入口，没有手拼lib或新依赖。绝对路径执行一个可执行文件，filter严格为`ReuseAudit.BinaryArchive*:ReuseAudit.LinearInterpolatorExtrapolates*`。实际passed=3、failed=0、skipped=0、notRun=0；原19项本轮未执行，不称22全跑。gtest.xml、原生ird-test-report.json、tests.log和build/configure日志均留存。task/reuse/docs exit0。

三项语义及独立oracle有效：

- uint32 0x01020304经BINArchive标量writeValue按地址/sizeof直接写出；本x64小端实测04 03 02 01，独立memcpy及显式字节oracle一致，不等于跨平台canonical或大端协议。源码确无端序转换。
- string写入以格式化`<<`输出十进制长度及字符，`x y`编码为`3x y`。读取格式化uint32得3，resize初始化3字符；格式化char提取x、跳空白得y，第三次遇EOF。因此长度仍3但首两字符xy，与原串不同。测试不依赖未初始化内存，不把此个例泛化成所有字符串/畸形输入行为；不能用一般archive roundtrip承诺冻结u32长度+原始UTF8协议等价。
- LinearInterpolator标量为(start+(end−start)/h*t)，普通1→3、h=1、t=2得5，源代码x(t)没有区间守卫；h=0抛出异常。有限DBL_MAX→−DBL_MAX在构造时end−start先变−∞，中点非有限；权重式0.5*max+0.5*(−max)=0。独立标量oracle和源码一致，只说明列明输入差异，不能证明完整回放器迁移或所有舍入等价。

原UpstreamSuitabilityTest.cpp和probe-r6所有文件相对直接基线及先前冻结877f6af...均Git blob不变。冻结probe-r7与本次独立XML/native均实际3项、零失败/跳过/未运行，XML文件路径绑定ToolSuitabilityTest.cpp。21条相关功能记录的84条auditEvidence规范化摘要均匹配，源码/CMake绑定有效，已安装sdurw_common.dll原始SHA256及字节数与环境记录一致。DLL摘要只标识环境，不证明其由冻结源可复现构建；原环境文件已注明该限制。

UTIL-BINARCHIVE仍candidate，矩阵明确主机序/格式化长度差异及既有依赖继续检索，不将候选排除当新核心批准。回放条目保留越界nullopt、帧原值、五量混合、极值/舍入及只显示边界，仍pending。相关结论无过度声称；宿主GUI及第五批其余功能不在本验收范围。

全量diff-check=2，仅归档REUSE-T01-proposals-20261022/diff.log的历史原文尾空白；保留原件，不宣称全差异检查通过。排除该归档后的活动文件diff-check=0。累计146条关系=143 pending/3 proposal，closedFiles=0；未批准提案实施、生产迁移或全T01退出。

未运行原19项、完整上游测试、一般/畸形输入与跨平台验证、生产/性能验收、GUI、140治理自测、DeepEvidence和远程CI。复现脚本run-probe.ps1、独立复核inspect.py、详细independent-review.json及acceptance.json均在本目录。
'''
(ev/'acceptance.md').write_text(md,encoding='utf-8')
print('PASS: '+str(ev/'acceptance.md'))
