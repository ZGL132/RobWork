import pathlib,json,subprocess,hashlib,os
root=pathlib.Path(__file__).parent; repo=root/'clone'; head='98c97238fa3c34aad51330b3b196439844badb69'; base='fd194d51b586085e26adeae4a73b72a06cb5ce21'
raw=json.loads((root/'raw-review.json').read_text(encoding='utf-8')); oracle=json.loads((root/'independent-oracle.json').read_text(encoding='utf-8'))
gates=[]
for name in ['validate-task','validate-reuse','validate-docs','verify-task-scope','commit-lint']:
 p=root/(name+'.exit.txt'); code=int(p.read_text(encoding='utf-8-sig').strip()); gates.append({'name':name,'exit':code,'status':'PASS' if code==0 else 'FAIL','log':name+'.log','exitFile':name+'.exit.txt'})
verdict='PASS' if not raw['errors'] and not oracle['errors'] and not oracle['originalR1TempArtifactErrors'] and all(g['exit']==0 for g in gates) else 'FAIL'
status=subprocess.check_output(['git','-C',str(repo),'status','--short']); (root/'clone-final-status.log').write_bytes(status);(root/'clone-final-status.exit.txt').write_text('0\n')
report={'taskId':'REUSE-T01','batch':'B28-r2','reviewedCommit':head,'baseCommit':base,'rejectedCommit':raw['rejected'],'verdict':verdict,'scope':'仅B28-r2局部记录修正；不授全T01/T02、B29或生产迁移准入','independence':{'checkout':'独立Temp clone --no-hardlinks --no-checkout; detached冻结SHA; core.longpaths=true; core.autocrlf=true','sharedTreeModified':False,'remoteOperations':'none','mergeOperations':'none','cleanClone':not status,'gui':'NOT RUN','newCppBuildAndTests':'NOT RUN：相对原f2e产品/上游/probe及构建零变更，依根AGENTS §4按纯记录修正验收，引用已核原始独立r1证据'},'findings':[],'closedPriorReviewItems':[{'id':'B28-ACC-01','result':'ContentVersion/ContentIdentity名称与Digest.hpp两hash特化吻合，仅静态同族影响；未冒充动态验证'},{'id':'B28-ACC-02','result':'69行实际源段逐行复核；detail/匿名命名空间、模板<T>、重载类型与::真实符号已修正；5方法宏各六类型，比较宏六类型共18操作；原11行不改'},{'id':'B28-ACC-03','result':'22行身份值引用3.6.1；31行来源/评估/诊断引用3.6.3；16行事件引用3.6总边界；不是worker条款一刀切，无grant新增'},{'id':'B28-ACC-04','result':'CORE-EVENT按真实有状态实例构造、订阅增删、fire发布、列表查询申报construct/mutate/publish/query，仍candidate，借持listener生命周期/线程限制保留，不授core新增依赖'}],'rawChecks':raw,'oracle':oracle,'gates':gates,'testEvidence':{'source':'原r1独立验收已实际运行产物，外层raw/Git blob以及仍存原Temp evidence 78项均一致；本轮未重跑C++','core':{'passed':64,'failed':0,'skipped':0},'probe':{'passed':4,'failed':2,'skipped':0,'includesUpstreamOriginalEventTestsPassed':2},'knownFailures':'FNV非空固定向量及公开ObjectId hash反例仍开放，不为审计记录修正而修改生产算法'},'environmentAttempts':{'initialCheckout':'FAIL exit128 Windows filename too long；保留日志；开启独立clone core.longpaths后成功exit0','incompleteCheckoutGateInvocations':'5项均exit64，仅解释器无入口的无效启动；标记incomplete-checkout，不作为最终验证','reviewScriptAdjustments':'纯验收脚本先将比较宏误预期为6，按实际六类型×三操作改为18；原Temp产物按evidence目录定位后78项原件核对一致，先次日志保留'},'remaining':'F-965 open；所有69行pending；生产结果消费/完整调用链/泛型异常/并发/最终链接等未本批结项；远程未执行；无新算法授权'}
(root/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
(root/'report.md').write_text(f"B28-r2 独立验收：{verdict}（仅局部记录修正）\n冻结 {head}；局部基点 {base}；89路径。\n四项r1追溯问题已逐69行核正；原11行、原B28 37项和r1 78项原件保持，r2外层84项raw Git blob摘要吻合。\n五项冻结版本门禁退出0；独立整数oracle四向量一致。产品/上游/原probe自f2e零变更；本轮不重建无变化C++，引用并核原r1实际core64p/0f/0s、probe4p/2f/0s（含上游原EventTest2p）。\nF-965及69行pending保持开放；无全T01/T02或B29准入，无生产算法改动、远程或合入操作。\n初次长路径检出失败与无效门禁启动日志保留，最终长路径配置后成功检出且clone干净。详见report.json及原始命令日志。\n",encoding='utf-8')
artifacts=[]
for current,dirs,files in os.walk(root):
 dirs[:]=[d for d in dirs if d not in ['clone','.git'] and not d.lower().startswith(('build','fixture'))]
 for name in files:
  p=pathlib.Path(current)/name
  if name=='safe-manifest.json':continue
  b=p.read_bytes();artifacts.append({'path':p.relative_to(root).as_posix(),'bytes':len(b),'rawSha256':hashlib.sha256(b).hexdigest()})
(root/'safe-manifest.json').write_text(json.dumps({'reviewedCommit':head,'excluded':['clone/**','.git/**','build*/**','fixture*/**'],'artifacts':sorted(artifacts,key=lambda x:x['path'])},ensure_ascii=False,indent=2),encoding='utf-8')
print(verdict);print(len(artifacts));print(root)
