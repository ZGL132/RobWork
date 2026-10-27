import pathlib,json,hashlib,subprocess,xml.etree.ElementTree as ET
ev=pathlib.Path(__file__).parent; repo=ev/'repo'
r=json.loads((ev/'independent-review.json').read_text(encoding='utf-8'))
checks={n:{'exitCode':int((ev/(n+'.exit.txt')).read_text(encoding='utf-8-sig').strip()),'log':n+'.log'} for n in ['configure','build','tests','task','reuse','docs','diff-check','inspect']}
assert all(v['exitCode']==0 for v in checks.values())
s=json.loads((ev/'ird-test-report.json').read_text(encoding='utf-8'))['summary']
assert (s['total'],s['passed'],s['failed'],s['skipped'],s['notRun'])==(19,19,0,0,0)
xml=ET.parse(ev/'gtest.xml').getroot(); assert xml.attrib['tests']=='19' and xml.attrib['failures']=='0'
assert all(c.attrib['status']=='run' for c in xml.findall('.//testcase'))
assert not subprocess.check_output(['git','-C',str(repo),'status','--short'],text=True).strip()
report={'schemaVersion':'ird-independent-api-probe-acceptance/1','taskId':'REUSE-T01','attempt':'api-r6','verdict':'pass','reviewedCommit':r['frozenSha'],'baseCommit':r['baseSha'],'scope':'仅5项新增公共API探测、既有19项独立重建实跑、r4/r5/r6证据计数及新增auditEvidence/环境摘要；不含第四批其余引用修正及全T01/生产准入','independentClone':str(repo),'trackedStatus':'','checks':checks,'tests':s,'historicalSnapshots':r['historicalSnapshots'],'semanticOracles':r['independentOracles'],'auditEvidence':{'bindings':len(r['auditBindings']),'allNormalizedHashesMatch':True,'details':'independent-review.json'},'environmentArtifacts':{'allRawHashesAndBytesMatch':True,'artifacts':r['environmentRawArtifacts'],'sourceAndCmakeNormalizedHashesMatch':True,'r6SourceSnapshotMatchesFrozenSource':True,'limitation':'仅记录现有安装DLL/头文件身份；不证明上游DLL由冻结源可复现构建。历史r5无源码快照，不把XML行号/构建日志当完整源码来源证明。'},'pendingRelations':r['pendingRelations'],'closedFiles':0,'prior14CasesUnchanged':True,'findings':[],'observations':['MSBuild MSB8029临时目录增量构建告警；本次独立fresh构建通过。','RPY r4是假设错误，不是上游RPY缺陷；失败源及报告仍保留。','新增auditEvidence由本轮独立逐条核摘要，不宣称校验器已经实现其语义验证。'],'notRun':['重跑历史r4错误假设或r5旧源码（核保留原件计数，本次实跑冻结r6全部19项）','完整上游测试/生产迁移/全工况数值性能验收','全20单元全功能审计或T01/T02准入','第四批其他引用修正的独立验收','Qt Widgets/GUI（本探测无QApplication）','140治理自测/DeepEvidence/远程CI','提交/推送/合入'],'evidenceSha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in ev.iterdir() if p.is_file() and p.name.endswith(('.log','.xml','.json','.ps1'))}}
(ev/'acceptance.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
md=f'''# REUSE-T01 API新5项独立验收：PASS

冻结 SHA `{r['frozenSha']}`；基线 `{r['baseSha']}`。独立临时clone、detached HEAD；最终git status为空，未改父树、产品、上游或vcpkg，未提交/推送/合入。

VS2022 x64从零configure/build既有probe，以既有RobWork/RobWorkSim导出targets及testkit报告入口实跑19项：passed=19、failed=0、skipped=0、notRun=0。原始configure/build/tests日志、gtest.xml及ird-test-report.json保留。task/reuse/docs与diff-check均exit0。已有14项测试正文未变；仅增头文件及5项测试。

独立语义复核：

- RPY普通±0.4、近奇异±(π/2−1e−8)及精确±π/2旋转往返断言有效。独立Rz·Ry·Rx连乘确认，正奇异时yaw=0、roll=−0.9与原yaw=0.7/roll=−0.2表示相同旋转；负奇异等效roll=0.5。源码r12明确是R(0,1)，不是R(1,2)。产品roll=0的规范化选择与上游yaw=0字段不同，不能以字段不等认定旋转不等。本轮不认定上游RPY缺陷。
- 混合尺度对角阵diag(denorm_min,1,1e300)的精确数学谱即三个正对角元素，denorm_min=2^−1074>0。上游谱接口返回min=0的实测有效，说明直接以该谱值判严格SPD受限；不等于上游完全没有可用候选。
- 单位块[[1,off],[off,1]]谱为1±off。用binary64精确有理数独算，off=nextafter(1,0)最小谱=2^−53>0，off=1为0，off=nextafter(1,2)为−2^−52。既有Eigen LLT对三者分别成功/拒绝/拒绝，且对混合尺度对角输入成功。
- 1e−14虽不是精确十进制，但四个块元素是同一个binary64值，前两列完全相等，(1,−1,0)是精确零向量；谱为0、2c、1，严格SPD应拒绝。LLT实测NumericalIssue正确。以上只覆盖列明输入，不宣称完整合法性/不对称/平衡/极值/消费者回归已通过，也不授权保留自持LDLT。

r4保留18实跑、17passed/1failed的假设源码和失败日志/XML/native报告；唯一失败是错误预期1.8rad，而实测约4.199e−17rad。r5保留18passed，r6保留19passed，XML与native报告的total/passed/failed/skipped/notRun一致，全部status=run。r6源码快照与冻结源码一致；历史r5无独立源码快照，不将行号或build.log冒充完整构建来源证明。本轮没有改写任何历史证据，也没有重跑旧错误假设。

5条功能记录的11条新增auditEvidence规范化摘要全部匹配；environment-artifacts列示数学DLL和Eigen LLT.h的原始SHA256及字节数全部与本机文件相符，源码/CMake规范化摘要匹配。记录明确仅是已安装二进制/头文件身份，不证明上游DLL由冻结源可复现构建；该限制已在README及环境文件注明。auditEvidence由本轮独立核对，不声称治理校验器已实现此字段语义审计。

累计121条关系仍decision=status=pending，closedFiles=0。本PASS仅覆盖API新5项及相关证据，不覆盖第四批其他引用修正、全T01功能审计、生产复用或T02准入。未执行完整上游集、全工况数值性能、140治理自测、DeepEvidence和远程CI；无GUI测试需求。

可复现命令见run-probe.ps1，独立解析/摘要核查见inspect.py与independent-review.json，生成步骤见finalize.py；详细结果见acceptance.json。
'''
(ev/'acceptance.md').write_text(md,encoding='utf-8')
print('PASS: '+str(ev/'acceptance.md'))
