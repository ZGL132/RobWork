from pathlib import Path
import json,subprocess,hashlib,xml.etree.ElementTree as ET
r=Path(r'C:/Users/zgl18/AppData/Local/Temp/ird-accept-t01-batch1-r2-20261022-212641'); repo=r/'repo'; sha='2b26f034c4ce24772239ff26697ac8ce38bb011e'; prior='8da272d79e8a0b89e3b1a60e3bcb3942dd59d1ae'; base='212226cfd2e2ca2fd12619dc57c458888f42534b'
def git(*a):return subprocess.check_output(['git','-C',str(repo),*a]).decode('utf-8-sig').strip()
def text(p):return (repo/p).read_text(encoding='utf-8-sig')
def digest(s):return hashlib.sha256((s.lstrip('\ufeff').replace('\r\n','\n').rstrip('\n')+'\n').encode()).hexdigest()
d=Path('RobWork/doc/industrial-robot-design'); rows=[]
for u in ['drivetrain','dynamics','kinematics','modeling','runtime','trajectory']:
 for f in json.loads(text(d/'reuse/functions'/f'{u}.json'))['functions']:
  s=f['sources'][0]; reg='\n'.join(text(s['path']).split('\n')[s['startLine']-1:s['endLine']]); rows.append({'functionId':f['functionId'],'capabilityId':f['capabilityId'],'decision':f['decision'],'status':f['status'],'qualifiedSymbol':s['qualifiedSymbol'],'regionHashMatches':digest(reg)==s['sha256'],'inputsMatch':all(digest(text(i['path']))==i['sha256'] for i in f['inputs'])})
cat=json.loads(text(d/'reuse/catalog.json')); resources=[]
for c in cat['capabilities']:
 for k in ['upstream','tests','docs','examples']:
  for b in c[k]:
   t=text(b['path']); v=b.get('version',cat['baselineCommit']); frozen=subprocess.check_output(['git','-C',str(repo),'show',v+':'+b['path']]).decode('utf-8-sig'); resources.append({'id':c['id'],'kind':k,'path':b['path'],'hashMatches':digest(t)==b['sha256'],'versionMatches':digest(frozen)==b['sha256'],'symbolsPresent':all(s in t for s in b['symbols'])})
xml=ET.parse(r/'gtest.xml').getroot(); native=json.loads((r/'ird-test-report.json').read_text(encoding='utf-8-sig')); semantic=json.loads((r/'semantic-test.log').read_text(encoding='utf-8-sig'))
changed=git('diff','--name-only',base,sha).splitlines(); rework=git('diff','--name-only',prior,sha).splitlines()
fixes=[{'id':'T01-B1-01','verdict':'pass','evidence':'runtime.json:89 now sdurws::ird::runtime::rotationFromCustomEaa; source namespace closes anonymous block at165 before definition207; unchanged source region hash matches.'},{'id':'T01-B1-02','verdict':'pass','evidence':'Target changed RPY(0,0,Pi)=Rx(pi). For all joint q, Rz(q)*Z=+Z, while Rx(pi)*Z=-Z; therefore unreachable. Supplement probe independently confirms reportedConverged=true and residual=pi.'},{'id':'T01-B1-03','verdict':'pass','evidence':'Target changes RPY(0.7,0.4,0.6) to RPY(0.7,0.4,1.2). yawDelta=0,pitchDelta=0,rollDelta=0.6; spatial log equals 0.6*Rz(0.7)*Ry(0.4)*X, oracleError<3e-16; yzNorm=0.4258424601930722.'}]
report={'schemaVersion':'ird-independent-batch-acceptance/1','taskId':'REUSE-T01','attempt':'batch1-r2','verdict':'pass','reviewedCommit':sha,'baseCommit':base,'reworkBaseCommit':prior,'priorAcceptance':{'verdict':'fail','path':'C:/Users/zgl18/AppData/Local/Temp/ird-accept-t01-batch1-20261022-211635/acceptance.json','preserved':True},'scope':'仅首批6单元24条pending候选关系、3个新增candidate能力、10项公共API探测与相关docs/task；非T01全20单元完成，非生产复用通过，非T02准入','independentClone':str(repo),'trackedStatus':git('status','--short'),'changedFiles':changed,'reworkFiles':rework,'forbiddenScopeChanges':any(p.startswith(('RobWork/RobWorkStudio/src/rwslibs/industrialrobot/','RobWork/RobWork/src/','RobWork/RobWorkSim/src/','vcpkg/')) for p in changed),'matrix':{'rows':len(rows),'distinctFunctions':len(set(x['functionId'] for x in rows)),'allPending':all(x['decision']==x['status']=='pending' for x in rows),'allHashesMatch':all(x['regionHashMatches'] and x['inputsMatch'] for x in rows),'qualifiedSymbolRechecked':True},'catalog':{'allCandidate':all(x['review']['status']=='candidate' for x in cat['capabilities']),'allBindingsMatch':all(x['hashMatches'] and x['versionMatches'] and x['symbolsPresent'] for x in resources)},'checks':{k:{'exitCode':int((r/(k+'.exit.txt')).read_text().strip()),'log':k+'.log'} for k in ['task','docs','reuse','configure','build','tests','semantic-configure','semantic-build','semantic-test']},'probe':{'xmlAttributes':xml.attrib,'testcaseCount':len(list(xml.iter('testcase'))),'nativeSummary':native['summary'],'xml':'gtest.xml','nativeReport':'ird-test-report.json','limitations':'Bullet未实现、Jacobian误报为预期限制断言；RNE torque接口差异仍保留；不代表功能成功、DYN-05或生产复用通过'},'semanticProbe':{'results':semantic,'source':'semantic-probe/semantic.cpp','log':'semantic-test.log','counting':'附加独立语义核对，不并入原10项计数'},'reworkVerification':fixes,'findings':[],'testkitReuse':'fresh build compiles existing industrialrobot/testkit and core/test/TestMainReport.cpp; no copied reporter','README':'Preserves probe/ initial results and documents invalid first-round RPY interpretations; probe-r2/ is separate, original FAIL unchanged.','notRun':['全20单元功能审计','完整上游集','生产迁移/断开实验','完整数值/性能','140治理自测','DeepEvidence全库索引','远程CI/推送/合入'],'bindings':{p:digest(text(p)) for p in ['RobWork/scripts/industrialrobot/validate-task.ps1','RobWork/scripts/industrialrobot/validate-docs.ps1','RobWork/scripts/industrialrobot/validate-reuse.ps1','RobWork/scripts/industrialrobot/reuse-contract.ps1','RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01/UpstreamSuitabilityTest.cpp','RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01/CMakeLists.txt','RobWork/RobWorkStudio/src/rwslibs/industrialrobot/core/test/TestMainReport.cpp']}}
assert git('rev-parse','HEAD')==sha
assert not report['trackedStatus'] and not report['forbiddenScopeChanges']
assert all(x['exitCode']==0 for x in report['checks'].values())
assert report['matrix']['allPending'] and report['matrix']['allHashesMatch'] and report['catalog']['allBindingsMatch']
assert len(list(xml.iter('testcase')))==10 and native['summary']['passed']==10 and all(native['summary'][k]==0 for k in ['failed','skipped','notRun'])
assert abs(semantic['yawDelta'])<1e-12 and abs(semantic['pitchDelta'])<1e-12 and abs(semantic['rollDelta']-.6)<1e-12 and semantic['yzNorm']>.2 and semantic['oracleError']<1e-12 and semantic['reportedConverged'] and abs(semantic['halfTurnResidualRad']-3.141592653589793)<1e-12
(r/'matrix-check.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf-8'); (r/'catalog-resource-check.json').write_text(json.dumps(resources,ensure_ascii=False,indent=2),encoding='utf-8'); (r/'acceptance.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
(r/'acceptance.md').write_text('''# REUSE-T01 首批返工独立验收 r2：PASS

冻结提交 `2b26f034c4ce24772239ff26697ac8ce38bb011e`；首轮 `8da272d79e8a0b89e3b1a60e3bcb3942dd59d1ae` 的FAIL原样保留；批次基线 `212226cfd2e2ca2fd12619dc57c458888f42534b`。

本轮仅验收首批6单元24条pending关系（23个不同functionId）、3个新增candidate能力、10项公共API探测与相应docs/task；不代表T01全20单元审计、生产复用或T02准入。

- 三项返工均通过：rotationFromCustomEaa命名空间准确；半周目标改为单Z关节无法达到的Rx(pi)；Euler反例固定yaw/pitch且仅改变roll。
- 独立数学核对：yawDelta=0，pitchDelta=0，rollDelta≈0.6；空间EAA=[0.4226797832,0.3560182700,-0.2336510054]，与0.6*Rz(yaw)*Ry(pitch)*X解析值误差<3e-16，Y/Z范数≈0.4258424602。
- 不可达性：任意Rz(q)保持+Z，而目标Rx(pi)将Z映为-Z；原成功标记仍为true，独立EAA残差π。预期限制测试通过不代表求解功能正常。
- 独立clone重跑validate-task/docs/reuse均exit=0；仍functions=24、closedFiles=0、compileWorkCell未授权pending提示。
- VS2022 x64重新configure/build/run：原10项passed=10、failed=0、skipped=0、notRun=0；真实gtest.xml和既有testkit/main生成的ird-test-report.json一致。附加语义探针单列，不并入10项计数。
- 24条区段/inputs摘要及catalog资源摘要/版本/符号均匹配。全部关系pending、能力candidate；无新增grant/closure，无产品、上游、vcpkg变动。
- README明确首轮RPY解释失效并保留原probe/；新probe-r2/独立存放。独立clone干净，未提交/推送/合入；未读取实施树未提交batch2。

完整上游集、全部20单元审计、生产迁移/断开、完整数值性能、140项治理自测、DeepEvidence及远程CI未执行。复现日志、原生报告、独立探针源码与详细绑定见本目录acceptance.json及semantic-*。
''',encoding='utf-8')
print(json.dumps({'verdict':'pass','reviewedCommit':sha,'passed':native['summary']['passed'],'semantic':semantic,'evidence':str(r)},ensure_ascii=False))
