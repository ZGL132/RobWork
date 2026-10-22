import pathlib, subprocess, json
OUT=pathlib.Path(__file__).resolve().parent
REPO=OUT.parent/'clone'
HEAD='52285db6f0d276742531ecb7e7bcf73db283e563'
BASE='933dc6016'
D='RobWork/doc/industrial-robot-design/'
B=D+'traceability/reuse/REUSE-T01/batch30/'
def git(*args): return subprocess.check_output(['git','-C',str(REPO),*args])
def obj(path,rev=HEAD): return json.loads(git('show',rev+':'+path))
old={r['functionId']:r for r in obj(D+'reuse/functions/core.json',BASE)['functions']}
new={r['functionId']:r for r in obj(D+'reuse/functions/core.json')['functions']}
changes=[]; errors=[]
for fid in old:
 if old[fid]==new[fid]: continue
 a=old[fid]; b=new[fid]
 changedkeys=[k for k in a if a[k]!=b[k]]
 additions={k:[e for e in b[k] if e not in a[k]] for k in ['inputs','auditEvidence']}
 searchvalid={**a['search'],'command':'batch30/r2/search.json'}==b['search']
 newevidence={e['path']:e for e in b['auditEvidence']}
 removed=[e for e in a['auditEvidence'] if e['path']!=D+'reuse/core-digest-audit.md' and e not in b['auditEvidence']]
 expectedInputPaths=[]
 if fid in ['core.audit-b30.Digest.hash.ContentVersion','core.audit-b30.Digest.hash.ContentIdentity']:
  expectedInputPaths=['RobWork/RobWorkStudio/src/rwslibs/industrialrobot/core/src/Identity.cpp']
 ok=set(a)==set(b) and set(changedkeys)<=set(['inputs','search','auditEvidence']) and searchvalid and not removed and [e['path'] for e in additions['inputs']]==expectedInputPaths
 if set(e['path'] for e in additions['auditEvidence'])!={D+'reuse/core-digest-audit.md',B+'r2/verification.json',B+'r2/search.json'}: ok=False
 value={'functionId':fid,'changedKeys':changedkeys,'addedInputs':additions['inputs'],'removedUnrelatedEvidence':removed,'ok':ok}
 changes.append(value)
 if not ok: errors.append(value)
coverage=[]
for label,root,filename,exclusions in [('original37',B,'artifact-manifest.json',['r2/']),('rejected57',B+'r2/rejected-090075171/','archive-manifest.json',[]),('r2original66',B+'r2/','artifact-manifest.json',[])]:
 listed={e['path'] for e in obj(root+filename)['entries']}
 actual={p[len(root):] for p in git('ls-tree','-r','--name-only',HEAD,'--',root).decode().splitlines() if p!=root+filename and not any(p[len(root):].startswith(x) for x in exclusions)}
 row={'label':label,'missingFromManifest':sorted(actual-listed),'nonexistentManifestEntries':sorted(listed-actual),'selfReferenced':filename in listed,'count':len(actual)}
 coverage.append(row)
 if actual!=listed or row['selfReferenced']: errors.append(row)
result={'rowDeltaCount':len(changes),'rowDeltas':changes,'coverage':coverage,'errors':errors}
(OUT/'delta-and-coverage.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({'rowDeltaCount':len(changes),'coverage':coverage,'errors':errors},ensure_ascii=False,indent=2))
raise SystemExit(1 if errors else 0)
