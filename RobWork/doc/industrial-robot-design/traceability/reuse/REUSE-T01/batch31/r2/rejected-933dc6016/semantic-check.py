import pathlib,json,subprocess,hashlib,xml.etree.ElementTree as ET
root=pathlib.Path(r'C:/Users/zgl18/AppData/Local/Temp/REUSE-T01-b31-accept-df453223');repo=root/'clone2';source=pathlib.Path(r'D:/10_Source_Repos/21_robot/RobWork');shared=pathlib.Path(r'C:/Users/zgl18/.codex/worktrees/reuse-audit/RobWork');head='933dc6016c078b7d674fea68af19c038e0388979'
def blob(p,rev=head):return subprocess.check_output(['git','-C',str(repo),'show',f'{rev}:{p}'])
def norm(b):return (b.decode('utf-8-sig').replace('\r\n','\n').rstrip('\n')+'\n').encode('utf-8')
def sha(b):return hashlib.sha256(b).hexdigest()
static=json.loads((root/'static-results.json').read_text(encoding='utf-8'));matrix=json.loads(blob('RobWork/doc/industrial-robot-design/reuse/functions/core.json'))['functions'];ids=set(static['newIds']+static['changedIds']);rows=[r for r in matrix if r['functionId'] in ids];checks=[]
for row in rows:
 for s in row['sources']:
  lines=blob(s['path']).decode('utf-8-sig').replace('\r\n','\n').split('\n');region='\n'.join(lines[s['startLine']-1:s['endLine']]);checks.append({'functionId':row['functionId'],'path':s['path'],'range':[s['startLine'],s['endLine']],'qualifiedSymbol':s.get('qualifiedSymbol'),'shaMatches':sha(norm(region.encode()))==s['sha256'],'symbolPresent':s['symbol'] in region})
 inputs=[]
 for x in row.get('inputs',[])+row.get('auditEvidence',[]):
  inputs.append({'path':x['path'],'shaMatches':sha(norm(blob(x['path'])))==x['sha256']})
 if not all(x['shaMatches'] for x in inputs):raise Exception(row['functionId']+' stale input')
cat=json.loads(blob('RobWork/doc/industrial-robot-design/reuse/catalog.json'));caps=cat.get('capabilities',cat.get('entries',[]));cap=next(c for c in caps if c['id']=='UTIL-UNITS')
up=[]
for u in cap['upstream']:
 up.append({'path':u['path'],'symbols':u['symbols'],'hashMatches':sha(norm(blob(u['path'])))==u['sha256'],'targetHashMatches':sha(norm(blob(u['targetFile'])))==u['targetSha256'],'physicalSourceMatchesFrozen':sha(norm((source/u['path']).read_bytes()))==sha(norm(blob(u['path'])))})
batch='RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01/batch31';manifest=json.loads(blob(batch+'/artifact-manifest.json'));rawChecks=[{'path':x['path'],'matches':sha((shared/batch/x['path']).read_bytes())==x['rawSha256'] and len((shared/batch/x['path']).read_bytes())==x['bytes']} for x in manifest['entries']]
search=json.loads(blob(batch+'/search.json'));searches=[{'args':s.get('args'),'exitCode':s.get('exitCode',s.get('returncode')),'stdoutLines':len(s.get('stdout','').splitlines()),'stderr':s.get('stderr','')} for s in search['searches']]
x=ET.parse(root/'run/gtest.xml').getroot();case=list(x.iter('testcase'));fail=[c.attrib['classname']+'.'+c.attrib['name'] for c in case if c.find('failure') is not None];skips=[c for c in case if c.find('skipped') is not None]
tests={'total':len(case),'passed':len(case)-len(fail)-len(skips),'failed':len(fail),'skipped':len(skips),'exitCode':int((root/'run/exit.txt').read_text()),'failedNames':fail,'original':sum(c.attrib['classname']!='ReuseUnitsAudit' for c in case),'new':sum(c.attrib['classname']=='ReuseUnitsAudit' for c in case),'suites':[{k:v for k,v in s.attrib.items() if k in ['name','tests','failures','skipped','disabled','errors']} for s in x]}
projects={}
ns={'m':'http://schemas.microsoft.com/developer/msbuild/2003'}
for name in ['ird_units_audit.vcxproj','parent-probe/ird-core/sdurws_ird_core.vcxproj']:
 path=root/'build'/name
 if not path.exists():continue
 tree=ET.parse(path);projects[name]={'compileSources':[p.attrib['Include'] for p in tree.findall('.//m:ClCompile',ns) if 'Include' in p.attrib],'includeDirectories':sorted(set(e.text for e in tree.findall('.//m:AdditionalIncludeDirectories',ns) if e.text)),'linkDependencies':sorted(set(e.text for e in tree.findall('.//m:AdditionalDependencies',ns) if e.text))}
scriptPaths=['RobWork/scripts/industrialrobot/'+s for s in ['validate-task.ps1','validate-docs.ps1','validate-reuse.ps1','reuse-contract.ps1','verify-task-scope.ps1','commit-lint.ps1']]
scriptHashes=[{'path':p,'blobRawSha256':sha(blob(p)),'executedNormalizedSha256':sha(norm((repo/p).read_bytes())),'frozenNormalizedSha256':sha(norm(blob(p)))} for p in scriptPaths]
result={'sourceSegments':checks,'allSourceSegmentsPassed':all(x['shaMatches'] and x['symbolPresent'] for x in checks),'allInputAndEvidenceHashesPassed':True,'catalogUpstream':up,'allPhysicalUpstreamSourcesMatch':all(x['physicalSourceMatchesFrozen'] for x in up),'submittedRawManifest':{'count':len(rawChecks),'allPassed':all(x['matches'] for x in rawChecks),'checks':rawChecks},'searchRecords':searches,'tests':tests,'buildProjects':projects,'scriptHashes':scriptHashes}
(root/'semantic-results.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({'segments':len(checks),'segmentsPassed':result['allSourceSegmentsPassed'],'inputHashesPassed':True,'upstreamBindingsPassed':all(x['hashMatches'] and x['targetHashMatches'] for x in up),'physicalUpstreamMatches':result['allPhysicalUpstreamSourcesMatch'],'submittedRawManifest':result['submittedRawManifest']['allPassed'],'tests':tests},ensure_ascii=False))
