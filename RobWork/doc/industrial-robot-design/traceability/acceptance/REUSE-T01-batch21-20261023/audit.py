import hashlib,json,pathlib,subprocess,zipfile,difflib
ROOT=pathlib.Path(__file__).parent
REPO=ROOT/'repo'
IMPL=pathlib.Path('C:/Users/zgl18/.codex/worktrees/reuse-audit/RobWork')
SHA='cff60ddd4a05fdae399932d8b6a3ccb5b1cdada7'
BASE='d52fd06aafcb025a7d494d59c6d44bc9c16f3643'
D='RobWork/doc/industrial-robot-design/'
B=D+'traceability/reuse/REUSE-T01/batch21/'
def j(p): return json.loads((REPO/p).read_text(encoding='utf-8-sig'))
def h(b): return hashlib.sha256(b).hexdigest()
def digest(t): return h((t.lstrip('\ufeff').replace('\r\n','\n').rstrip('\n')+'\n').encode())
def blob(p,ref=SHA): return subprocess.check_output(['git','-C',str(REPO),'show',ref+':'+p])
def save(p,v): (ROOT/p).write_text(json.dumps(v,ensure_ascii=False,indent=2),encoding='utf-8')
defs=j(B+'definitions.json'); matrix=j(D+'reuse/functions/foundation-governance.json')['functions']; ids={x['functionId'] for x in defs}; rows=[x for x in matrix if x['functionId'] in ids]
checks=[]; sourceview=[]
for row in rows:
    f=row['functionId']; d=next(x['source'] for x in defs if x['functionId']==f)
    check={'functionId':f,'definitionMatches':d in row['sources'],'pending':row['decision']=='pending' and row['status']=='pending','noFakeTargets':row['targets']==[], 'ownerNone':row['usageRole']=='owner' and row['capabilityId']=='NONE','executionMatches':row['execution']['kind']=='governance-script' and row['execution']['entry'] in row['execution']['command'],'sources':[],'inputs':[],'auditEvidence':[],'differences':row['differences']}
    for s in row['sources']:
        txt=blob(s['path']).decode('utf-8-sig').replace('\r\n','\n'); lines=txt.split('\n'); region='\n'.join(lines[s['startLine']-1:s['endLine']])
        check['sources'].append({'path':s['path'],'hashMatches':digest(region)==s['sha256'],'symbolLiteral':s['symbol'] in region,'fullInputBound':any(x['path']==s['path'] for x in row['inputs'])})
        sourceview.append(f+'\n'+row['differences']+'\n'+'\n'.join(f'{i+1}: {v}' for i,v in enumerate(lines) if s['startLine']-1 <= i < s['endLine']))
    for typ in ['inputs','auditEvidence']:
        for v in row[typ]: check[typ].append({'path':v['path'],'hashMatches':digest(blob(v['path']).decode('utf-8-sig'))==v['sha256']})
    checks.append(check)
save('binding-checks.json',checks); (ROOT/'source-review.txt').write_text('\n\n'.join(sourceview),encoding='utf-8')
search=[]
for i,s in enumerate(j(B+'search.json'),1):
    p=subprocess.run(s['argv'],cwd=REPO,capture_output=True)
    out=p.stdout.decode('utf-8',errors='replace');err=p.stderr.decode('utf-8',errors='replace')
    (ROOT/f'search-{i}.log').write_bytes(p.stdout+p.stderr)
    # Git Windows checkout inserts CRLF; rg path separator output is unchanged.
    search.append({'argv':s['argv'],'exitCode':p.returncode,'expectedExitCode':s['exitCode'],'exitMatches':p.returncode==s['exitCode'],'outputMatchesAfterCRLF':out.replace('\r\n','\n')==s['stdout'].replace('\r\n','\n'),'stderr':err})
save('search-replay.json',search)
archives=[]
orig19=IMPL/'.cache/accepted-b19'
originals={str(p.relative_to(orig19)).replace('\\','/'):p.read_bytes() for p in orig19.rglob('*') if p.is_file()}
with zipfile.ZipFile(IMPL/'.cache/accepted-b20-evidence.zip') as z: originals20={n:z.read(n) for n in z.namelist() if not n.endswith('/')}
for batch,items in [(19,originals),(20,originals20)]:
    prefix=D+f'traceability/acceptance/REUSE-T01-batch{batch}-20261023/'
    tracked=subprocess.check_output(['git','-C',str(REPO),'ls-tree','-r','--name-only',SHA,'--',prefix]).decode().splitlines()
    for rel,b in items.items():
        p=prefix+rel; actual=blob(p)
        archives.append({'batch':batch,'path':rel,'originalBytes':len(b),'gitBlobBytes':len(actual),'originalSha256':h(b),'gitBlobSha256':h(actual),'rawEqual':b==actual})
    assert set(tracked)=={prefix+k for k in items},(batch,len(tracked),len(items))
save('archive-raw-byte-checks.json',archives)
paths=subprocess.check_output(['git','-C',str(REPO),'diff','--name-only',BASE,SHA]).decode().splitlines()
scripts=[p for p in paths if p.startswith('RobWork/scripts/')]
protected=[p for p in paths if p.startswith(('RobWork/RobWork/','RobWork/RobWorkSim/','RobWork/RobWorkStudio/','vcpkg/'))]
script=scripts[0]; before=blob(script,BASE); after=blob(script)
normbefore=before.decode('utf-8-sig').replace('\r\n','\n'); normafter=after.decode('utf-8-sig').replace('\r\n','\n')
(ROOT/'script-semantic-diff.txt').write_text(''.join(difflib.unified_diff(normbefore.splitlines(True),normafter.splitlines(True),fromfile=BASE,tofile=SHA)),encoding='utf-8')
savedbefore=blob(B+'lite-before.ps1')
save('scope-and-script-checks.json',{'pathsChanged':len(paths),'scriptsChanged':scripts,'protectedChanged':protected,'beforeRawSha256':h(before),'savedBeforeRawSha256':h(savedbefore),'beforeRawEqual':before==savedbefore,'beforeNormalizedEqual':normbefore==savedbefore.decode('utf-8-sig').replace('\r\n','\n'),'afterRawSha256':h(after),'archiveCounts':{str(k):sum(x['batch']==k for x in archives) for k in [19,20]},'archiveRawEqual':all(x['rawEqual'] for x in archives),'functionCount':len(rows),'definitionCount':len(defs)})
print(json.dumps({'functions':len(rows),'search':len(search),'archiveCounts':{str(k):sum(x['batch']==k for x in archives) for k in [19,20]},'archiveRawEqual':all(x['rawEqual'] for x in archives),'protectedChanged':protected},ensure_ascii=False))
