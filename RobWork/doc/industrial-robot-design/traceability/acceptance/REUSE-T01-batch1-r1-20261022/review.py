from pathlib import Path
import subprocess,json,hashlib,xml.etree.ElementTree as ET
root=Path(r'C:/Users/zgl18/AppData/Local/Temp/ird-accept-t01-batch1-20261022-211635')
repo=root/'repo'
sha='8da272d79e8a0b89e3b1a60e3bcb3942dd59d1ae'
base='212226cfd2e2ca2fd12619dc57c458888f42534b'
upstream='b12e8503cc1c2a1acc040f506842a02e126b9515'
def git(*args): return subprocess.check_output(['git','-C',str(repo),*args]).decode('utf-8-sig').strip()
def txt(path): return (repo/path).read_text(encoding='utf-8-sig')
def digest(s): return hashlib.sha256((s.lstrip('\ufeff').replace('\r\n','\n').rstrip('\n')+'\n').encode()).hexdigest()
design=Path('RobWork/doc/industrial-robot-design')
rows=[]
for unit in ['drivetrain','dynamics','kinematics','modeling','runtime','trajectory']:
    data=json.loads(txt(design/'reuse/functions'/f'{unit}.json'))
    for f in data['functions']:
        s=f['sources'][0]; lines=txt(s['path']).split('\n')
        region='\n'.join(lines[s['startLine']-1:s['endLine']])
        rows.append({'functionId':f['functionId'],'capabilityId':f['capabilityId'],'unit':unit,'path':s['path'],'qualifiedSymbol':s['qualifiedSymbol'],'startLine':s['startLine'],'endLine':s['endLine'],'regionSha256':digest(region),'expectedSha256':s['sha256'],'hashMatches':digest(region)==s['sha256'],'symbolPresent':s['symbol'] in region,'decision':f['decision'],'status':f['status'],'usageRole':f['usageRole'],'permissionRef':f['permissionRef'],'inputsMatch':all(digest(txt(i['path']))==i['sha256'] for i in f['inputs']),'qualifiedSymbolReview':'incorrect-anonymous-namespace' if f['functionId']=='runtime.rotationFromCustomEaa' else 'manual-context-matches'})
cat=json.loads(txt(design/'reuse/catalog.json'))
resources=[]
for c in cat['capabilities']:
    for kind in ['upstream','tests','examples','docs']:
        for r in c[kind]:
            current=txt(r['path'])
            version=r.get('version',cat['baselineCommit'])
            frozen=subprocess.check_output(['git','-C',str(repo),'show',version+':'+r['path']]).decode('utf-8-sig')
            resources.append({'capabilityId':c['id'],'kind':kind,'path':r['path'],'version':version,'hashMatches':digest(current)==r['sha256'],'versionMatches':digest(frozen)==r['sha256'],'symbolsPresent':all(s in current for s in r['symbols']),'reviewStatus':c['review']['status']})
xml=ET.parse(root/'gtest.xml').getroot()
native=json.loads((root/'ird-test-report.json').read_text(encoding='utf-8-sig'))
tracked=git('status','--short')
files=git('diff','--name-only',base,sha).splitlines()
findings=[{'id':'T01-B1-01','priority':'P2','path':str(design/'reuse/functions/runtime.json').replace('\\','/'),'line':89,'title':'修正 rotationFromCustomEaa 的命名空间定位','detail':'qualifiedSymbol 错记为匿名命名空间。BaseWorldTransform.cpp:165 已关闭匿名空间，207行函数及 include/sdurws/ird/runtime/BaseWorldTransform.hpp:137 的声明均位于 sdurws::ird::runtime。应登记 sdurws::ird::runtime::rotationFromCustomEaa。区段摘要正确不消除此身份错误。'}, {'id':'T01-B1-02','priority':'P2','path':str(design/'reuse/extensions/required-proposals.md').replace('\\','/'),'line':9,'title':'修正半周求解器探测的不可达归因','detail':'夹具中 RPY(Pi,0,0) 为绕Z半周，RevoluteJoint 同样绕Z且默认无限界。独立探针使用同夹具 setQ(Pi) 得 targetReachableResidualRad=0。应将“不可达半周姿态”改为“可达半周目标处的误报收敛”；误报本身由原用例证实。','evidence':['semantic-probe/semantic.cpp','semantic-test.log']}]
report={'schemaVersion':'ird-independent-batch-acceptance/1','taskId':'REUSE-T01','verdict':'fail','reviewedCommit':sha,'baseCommit':base,'upstreamBaseline':upstream,'scope':'仅首批24条pending功能候选（6单元）、3项新增能力资源、10项公共API审计探测及相关docs/task契约；不代表全20单元审计、生产复用或T02准入','independentClone':str(repo),'trackedCloneChanges':tracked,'changedFiles':files,'productUpstreamVcpkgChanges':any(p.startswith(('RobWork/RobWorkStudio/src/rwslibs/industrialrobot/','RobWork/RobWork/src/','RobWork/RobWorkSim/src/','vcpkg/')) for p in files),'matrix':{'rows':len(rows),'distinctFunctions':len({r['functionId'] for r in rows}),'units':6,'allPending':all(r['decision']==r['status']=='pending' for r in rows),'allRegionHashesMatch':all(r['hashMatches'] for r in rows),'allInputsMatch':all(r['inputsMatch'] for r in rows)},'catalog':{'newCapabilities':['MATH-EIGEN','MATH-INERTIA','MATH-POLYNOMIAL'],'allCandidate':all(c['review']['status']=='candidate' for c in cat['capabilities']),'allBoundResourceHashesAndVersionsMatch':all(r['hashMatches'] and r['versionMatches'] and r['symbolsPresent'] for r in resources)},'checks':{k:{'exitCode':int((root/(k+'.exit.txt')).read_text().strip()),'log':k+'.log'} for k in ['task','docs','reuse','configure','build','tests']},'probe':{'gtest':{k:xml.attrib.get(k) for k in ['tests','failures','disabled','errors']},'testcaseCount':len(list(xml.iter('testcase'))),'nativeSummary':native['summary'],'xml':'gtest.xml','nativeReport':'ird-test-report.json','restrictionTests':'Bullet未实现、Jacobian半周误报属于预期限制断言；RNE用例同时记录torque API投影差异；通过不代表功能正常或DYN-05验收'},'independentSemanticProbe':{'exitCode':int((root/'semantic-test.exit.txt').read_text().strip()),'result':json.loads((root/'semantic-test.log').read_text(encoding='utf-8-sig')),'scope':'附加可达性反证，单独列示，不并入原10项计数'},'bindings':{p:digest(txt(p)) for p in ['RobWork/scripts/industrialrobot/validate-task.ps1','RobWork/scripts/industrialrobot/validate-docs.ps1','RobWork/scripts/industrialrobot/validate-reuse.ps1','RobWork/scripts/industrialrobot/reuse-contract.ps1','RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01/UpstreamSuitabilityTest.cpp','RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01/CMakeLists.txt','RobWork/RobWorkStudio/src/rwslibs/industrialrobot/core/test/TestMainReport.cpp','RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/CMakeLists.txt']},'findings':findings,'notRun':['全20单元逐功能审计','完整上游测试集','生产迁移及生产断开实验','数值/性能完整验收','全部140项治理自测','DeepEvidence全库索引','远程CI/推送/主线合入'],'notes':['本批可疑实现保持pending；没有新增grant/closure；runtime继续owner，业务adapter不获构造权。','探测复用现有testkit和TestMainReport.cpp；无另造报告入口；VS2022 x64，单个绝对路径控制台可执行文件，无GUI。','临时clone首次检出遇Windows长路径；设置该clone的core.longpaths=true后完整检出，最终tracked status为空。','独立可达性探针首次配置缺Boost前缀，补用README既有CMAKE_PREFIX_PATH后构建运行成功；最终日志及退出码见semantic-*。']}
(root/'matrix-check.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf-8')
(root/'catalog-resource-check.json').write_text(json.dumps(resources,ensure_ascii=False,indent=2),encoding='utf-8')
(root/'acceptance.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({'verdict':report['verdict'],'matrix':report['matrix'],'catalog':report['catalog'],'testSummary':native['summary'],'trackedCloneChanges':tracked,'findingCount':len(findings)},ensure_ascii=False))
