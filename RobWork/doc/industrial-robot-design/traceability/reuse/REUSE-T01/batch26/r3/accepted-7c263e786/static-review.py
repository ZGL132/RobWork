import pathlib,subprocess,json,hashlib,copy
out=pathlib.Path(__file__).parent; repo=out/'repo'
base='4b9977f096d0b3d58687ecc94013f435e5b0502c'; sha='7c263e78683a74b5532e6f99e7647a02b6f39ea7'
def git(*args): return subprocess.check_output(['git','-C',str(repo),*args])
cache={}
def blob(path,ref=sha):
 key=(path,ref)
 if key not in cache: cache[key]=git('show',ref+':'+path)
 return cache[key]
def norm(data): return data.decode('utf-8-sig').replace('\r\n','\n')
def digest(data): return hashlib.sha256(data).hexdigest()
prefix='RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01/batch26/r3/rejected-4b9977f09/'
original=pathlib.Path(r'C:/Users/zgl18/AppData/Local/Temp/reuse-t01-b26-r2-acceptance-20261023')
manifest=json.loads(blob(prefix+'artifact-manifest.json')); rows=[]
tree={}
for entry in git('ls-tree','-rz',sha,'--',prefix).split(b'\0'):
 if not entry: continue
 meta,path=entry.split(b'\t',1); mode,typ,oid=meta.decode().split(); tree[path.decode()[len(prefix):]]=(mode,oid)
for item in manifest+[dict(path='artifact-manifest.json')]:
 rel=item['path']; raw=blob(prefix+rel); orig=(original/rel).read_bytes(); mode,oid=tree[rel]
 rows.append(dict(path=rel,bytes=len(raw),sha256=digest(raw),originalMatch=raw==orig,checkoutMatch=raw==(repo/prefix/rel).read_bytes(),manifestMatch=('rawSha256' not in item or (digest(raw)==item['rawSha256'] and len(raw)==item['bytes'])),mode=mode,blob=oid,noGitMetadata='.git' not in pathlib.PurePosixPath(rel).parts))
archive=dict(expectedManifestEntries=327,manifestEntries=len(manifest),expectedIncludingManifest=328,gitEntries=len(tree),checked=len(rows),failed=sum(not all(r[k] for k in ['originalMatch','checkoutMatch','manifestMatch','noGitMetadata']) or r['mode']=='160000' for r in rows),exactSet=set(tree)=={r['path'] for r in rows},rows=rows)
(out/'r2-archive-raw-report.json').write_text(json.dumps(archive,ensure_ascii=False,indent=2),encoding='utf-8')
path='RobWork/doc/industrial-robot-design/reuse/functions/foundation-governance.json'
old=json.loads(blob(path,base)); new=json.loads(blob(path)); oldmap={r['functionId']:r for r in old['functions']}; affected=[r for r in new['functions'] if r!=oldmap.get(r['functionId'])]
checks=[]; deltas=[]
for row in affected:
 for source in row['sources']:
  lines=norm(blob(source['path'])).split('\n'); region='\n'.join(lines[source['startLine']-1:source['endLine']]); actual=digest((region.rstrip('\n')+'\n').encode())
  checks.append(dict(functionId=row['functionId'],kind='source',path=source['path'],start=source['startLine'],end=source['endLine'],match=actual==source['sha256'],symbolPresent=source['symbol'] in region))
 for kind in ['inputs','auditEvidence']:
  for binding in row[kind]:
   actual=digest((norm(blob(binding['path'])).rstrip('\n')+'\n').encode())
   checks.append(dict(functionId=row['functionId'],kind=kind,path=binding['path'],match=actual==binding['sha256']))
 def walk(a,b,p=''):
  if type(a)!=type(b): deltas.append(dict(functionId=row['functionId'],path=p,before=a,after=b)); return
  if isinstance(a,dict):
   for k in a.keys()|b.keys(): walk(a.get(k),b.get(k),p+'/'+k)
  elif isinstance(a,list):
   if len(a)!=len(b): deltas.append(dict(functionId=row['functionId'],path=p+'/length',before=len(a),after=len(b)))
   for i,(x,y) in enumerate(zip(a,b)): walk(x,y,p+'/'+str(i))
  elif a!=b: deltas.append(dict(functionId=row['functionId'],path=p,before=a,after=b))
 walk(oldmap[row['functionId']],row)
r3verification='RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01/batch26/r3/verification.json'
bindingSummary=dict(affectedRows=len(affected),sourceChecks=sum(c['kind']=='source' for c in checks),bindingChecks=len(checks),failed=sum(not c['match'] or not c.get('symbolPresent',True) for c in checks),allRemainPending=all(r['status']=='pending' and r['decision']=='pending' and r['usageRole']=='owner' and r['capabilityId']=='NONE' and r['targets']==[] for r in affected),r3VerificationBound=sum(any(e['path']==r3verification for e in r['auditEvidence']) for r in affected))
(out/'binding-review.json').write_text(json.dumps(dict(summary=bindingSummary,checks=checks,deltas=deltas),ensure_ascii=False,indent=2),encoding='utf-8')
changed=git('diff','--name-only',base+'..'+sha).decode().splitlines()
archives=['RobWork/doc/industrial-robot-design/traceability/acceptance/REUSE-T01-batch'+b+'-20261023/' for b in ['23','24','25']]
archiveUnchanged={p:not any(c.startswith(p) for c in changed) for p in archives}
scriptPath='RobWork/scripts/industrialrobot/merge-findings-union.ps1'; pipeline='RobWork/scripts/industrialrobot/test-pipeline-pipeline.ps1'
a=norm(blob(scriptPath,base)); b=norm(blob(scriptPath))
before='  $lines = @(& git -C $RepoRoot show ("{0}:{1}" -f $stage, $Path) 2>$null)\n  if ($LASTEXITCODE -ne 0) { return $null }'
after='  # PS5.1会把Git stderr提升为终止错误；局部按退出码判断缺stage，保持调用错误exit2。\n  $savedPreference = $ErrorActionPreference\n  try {\n    $ErrorActionPreference = \'Continue\'\n    $lines = @(& git -C $RepoRoot show ("{0}:{1}" -f $stage, $Path) 2>$null)\n    $gitExit = $LASTEXITCODE\n  } finally { $ErrorActionPreference = $savedPreference }\n  if ($gitExit -ne 0) { return $null }'
firstFail=[p for p in git('ls-tree','-r','--name-only',base,'--','RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01/batch26').decode().splitlines() if 'rejected-aa31' in p]
firstFailUnchanged=all(blob(p,base)==blob(p) for p in firstFail)
oldRpt=json.loads((original/'acceptance.json').read_text(encoding='utf-8'))
summary=dict(head=git('rev-parse','HEAD').decode().strip(),detached=git('symbolic-ref','-q','HEAD').decode().strip() if False else not (repo/'.git/HEAD').read_text().startswith('ref:'),status=git('status','--short').decode(),autocrlf=git('config','core.autocrlf').decode().strip(),longpaths=git('config','core.longpaths').decode().strip(),changedFiles=len(changed),noProductFrameworkVcpkgChanges=not any(p.startswith(('RobWork/RobWorkStudio/src/','RobWork/RobWork/src/','RobWork/RobWorkSim/src/','vcpkg/')) for p in changed),scriptsChanged=[p for p in changed if p.startswith('RobWork/scripts/')],minimalGetStagePatch=a.replace(before,after)==b,beforeSourceExact=blob('RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01/batch26/r3/before-merge-findings-union.ps1')==blob(scriptPath,base),beforePipelineExact=blob('RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01/batch26/r3/before-test-pipeline-pipeline.ps1')==blob(pipeline,base),oldArchivesUnchanged=archiveUnchanged,oldArchiveR2VerifiedCounts={'B24':173,'B25':159,'B23':136},oldArchiveR2ReportSha256=digest((original/'archive-raw-report.json').read_bytes()),firstFailFiles=len(firstFail),firstFailUnchanged=firstFailUnchanged,archiveSummary={k:v for k,v in archive.items() if k!='rows'},bindingSummary=bindingSummary)
(out/'static-summary.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(summary,ensure_ascii=False,indent=2))
