import pathlib,json,hashlib,subprocess,xml.etree.ElementTree as ET
ev=pathlib.Path(__file__).parent
repo=ev/'repo'
review=json.loads((ev/'independent-review.json').read_text(encoding='utf-8'))
checks={n:{'exitCode':int((ev/(n+'.exit.txt')).read_text(encoding='utf-8-sig').strip()),'log':n+'.log'} for n in ['configure','build','tests','task','docs','reuse','diff-check','active-diff-check','inspect']}
assert all(v['exitCode']==0 for n,v in checks.items() if n!='diff-check')
assert checks['diff-check']['exitCode']==2
assert (ev/'diff-check.log').read_text(encoding='utf-8-sig').strip()=='RobWork/doc/industrial-robot-design/traceability/acceptance/REUSE-T01-batch2-r2-20261022/inspect.py:21: new blank line at EOF.'
native=json.loads((ev/'ird-test-report.json').read_text(encoding='utf-8'))
assert native['summary']['passed']==14 and all(native['summary'][k]==0 for k in ['failed','skipped','notRun'])
xml=ET.parse(ev/'gtest.xml').getroot()
assert xml.attrib['tests']=='14' and xml.attrib['failures']=='0'
assert len(xml.findall('.//testcase'))==14 and all(t.attrib['status']=='run' for t in xml.findall('.//testcase'))
status=subprocess.check_output(['git','-C',str(repo),'status','--short'],text=True).strip()
assert status==''
bound=['RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01/UpstreamSuitabilityTest.cpp','RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01/CMakeLists.txt','RobWork/RobWork/src/rw/invkin/ParallelIKSolver.cpp','RobWork/RobWork/src/rw/math/EAA.cpp','RobWork/RobWork/src/rw/math/Transform3D.cpp','RobWork/RobWork/src/rw/math/PolynomialND.hpp','RobWork/RobWorkStudio/src/rwslibs/industrialrobot/kinematics/src/Sampling.cpp']
report={'schemaVersion':'ird-independent-api-probe-acceptance/1','taskId':'REUSE-T01','attempt':'api-r3','verdict':'pass','reviewedCommit':review['frozenSha'],'baseCommit':review['baseSha'],'scope':'仅公共API探测4项增量及14项重建重跑、第二批FAIL/PASS归档原文保留；非T01全量验收或T02准入','independentClone':str(repo),'trackedStatus':status,'checks':checks,'tests':native['summary'],'semanticReview':review['independentOracles'],'archive':{'files':82,'sameOriginalText':True,'exactBytes':78,'lineEndingOnly':4,'reportsAndLogsExactBytes':True,'details':'archive-review.json'},'pendingRelations':95,'closedFiles':0,'findings':[],'nonBlockingObservations':['全差异git diff --check=2，仅归档的batch2-r2/inspect.py:21原件已有EOF空行；保留原文，活动文件diff检查=0。不把全差异检查声称通过。','4个归档Python脚本Git检出CRLF/LF转换，规范化换行后原文一致；acceptance.md/json及原始日志全部字节相同。','MSBuild提示临时目录可能影响增量构建(MSB8029)；本轮独立fresh构建成功。'],'notRun':['Qt Widgets/GUI（探测仅模型/数值，无QApplication）','生产算法迁移/数值性能验收','完整上游测试集','140治理脚本自测','DeepEvidence全库索引深查','全20单元全功能语义审计及T01/T02准入','提交/推送/合入/远程CI'],'sourceSha256':{p:hashlib.sha256((repo/p).read_bytes()).hexdigest() for p in bound},'evidenceSha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in ev.iterdir() if p.is_file() and p.name.endswith(('.log','.xml','.json','.ps1'))}}
(ev/'acceptance.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
md=f'''# REUSE-T01 API探测增量独立验收：PASS

冻结 SHA `{review['frozenSha']}`；基线 `{review['baseSha']}`。独立临时 clone、detached HEAD，最终工作区干净；未改父工作树、产品、上游或vcpkg，未实施、提交、推送或合入。

VS2022 x64环境从零configure/build既有probe，通过现有RobWork/RobWorkSim导出targets和testkit报告入口真实运行全部14项。passed=14、failed=0、skipped=0、notRun=0；原始tests.log、gtest.xml、ird-test-report.json留存。task/docs/reuse均exit0；后者只证明治理结构，不证明算法合规。

新增四项独立语义复核成立：

- ParallelIK：单Z转动关节TCP为(cos q,sin q,0)，对(2,0,0)距离平方=5−4cos q≥1；q=0的切向Jacobian=(0,1,0)，残差=(1,0,0)，伪逆增量为0，两个迭代均不能改善。实际日志确有预算耗尽，源代码随后仍push候选；返回候选残差1 m，不能当收敛。默认未启用关节限位检查；本例q=0本身在给定界内。
- 标准DH：Rz(theta)Tz(d)Tx(a)Rx(alpha)直接四因子展开与DH公式一致，参数0.4/0.3/−0.2/0.7的Craig顺序Rx(alpha)Tx(a)Rz(theta)Tz(d)与之不同。RPY实参确为yaw/pitch/roll，因此探测四因子轴选取正确。
- 反向对齐：EAA(z,−z)分支选z×x=+y，得到Ry(π)=diag(−1,1,−1)；产品Sampling.cpp反向分支固定Rx(π)=diag(1,−1,−1)。相对旋转为diag(−1,−1,1)=工具局部Rz(π)，两者方向相同而滚转相差π。
- 多项式：从p(u)=Σ(i+1)u^i直接逐单项求导，独立精确有理数算出左端1/2/6/24、右端36/168/756/3024。系数先除h^i后对物理时间求导：h=0.5右端36/336/3024/24192；h=2右端36/84/189/378。测试未将断言两侧同除h来伪造时间缩放，覆盖非零端导数。

归档第二批FAIL/PASS共82文件原文一致：78字节完全一致，包括acceptance.md/json及原始日志；4个Python脚本仅Git CRLF/LF检出转换。全差异git diff --check=2，唯一告警为归档batch2-r2/inspect.py:21原件已有EOF空行；此为保留历史原文的非阻塞格式项，未改写原件。排除两份归档后的活动文件diff检查exit0。完整比较见archive-review.json。

累计95关系全部decision=status=pending，closedFiles=0。结论仅API探测增量，不代表T01全20单元全功能审计完成、生产复用通过或T02准入。未跑完整上游集、140治理自测、DeepEvidence、生产/性能验收与远程CI。

独立解析复算脚本inspect.py、构建运行脚本run-probe.ps1与本报告生成脚本finalize.py均在本目录；详细命令、绑定与原始结果见acceptance.json和对应日志。
'''
(ev/'acceptance.md').write_text(md,encoding='utf-8')
print('PASS: '+str(ev/'acceptance.md'))
