import pathlib,json,subprocess,hashlib,re,sys
sys.stdout.reconfigure(encoding='utf-8')
ev=pathlib.Path(__file__).parent; repo=ev/'repo'; d=repo/'RobWork/doc/industrial-robot-design'
def read(p): return p.read_text(encoding='utf-8-sig')
def digest(p): return hashlib.sha256((read(p).rstrip('\n')+'\n').encode()).hexdigest()
def save(n,x): (ev/n).write_text(json.dumps(x,ensure_ascii=False,indent=2),encoding='utf-8')
rows=json.loads(read(ev/'new-rows.json')); defs=[]; excerpts=[]
for r in rows:
 for s in r['sources']:
  lines=read(repo/s['path']).splitlines(); section='\n'.join(lines[s['startLine']-1:s['endLine']]);
  defs.append(dict(functionId=r['functionId'],source=s['path'],symbol=s['qualifiedSymbol'],start=s['startLine'],end=s['endLine'],first=section.splitlines()[0],last=section.splitlines()[-1],completeBoundary=section.rstrip().endswith('}'),grouped=r['functionId'] in ['testkit.JsonLite.eof','workflow.CloseSupervisor.running']))
  excerpts.append('\n'+r['functionId']+' '+s['qualifiedSymbol']+'\n'+'\n'.join(f'{i+1}: {lines[i]}' for i in range(max(0,s['startLine']-3),min(len(lines),s['endLine']+2))))
save('definition-checks.json',defs); (ev/'source-excerpts.log').write_text('\n'.join(excerpts),encoding='utf-8')
search=json.loads(read(d/'traceability/reuse/REUSE-T01/batch12-search.json')); replay=[]
for i,x in enumerate(search['results'],1):
 p=subprocess.run(x['arguments'],cwd=repo,capture_output=True,encoding='utf-8',errors='replace')
 (ev/f'search-{i}.stdout.log').write_text(p.stdout,encoding='utf-8'); (ev/f'search-{i}.stderr.log').write_text(p.stderr,encoding='utf-8')
 replay.append(dict(index=i,arguments=x['arguments'],exitCode=p.returncode,recordedExitCode=x['exitCode'],exitMatches=p.returncode==x['exitCode'],stdoutSetMatches=sorted(p.stdout.splitlines())==sorted(x['stdout'].splitlines()),stderrMatches=p.stderr==x['stderr'],hits=len(p.stdout.splitlines())))
save('search-replay.json',replay)
corrected=search['results'][0]['arguments'].copy(); corrected[3]=r'CreateProcess|JobObject|fork\s*\(|popen|spawn|processrunner|QProcess'
p=subprocess.run(corrected,cwd=repo,capture_output=True,encoding='utf-8',errors='replace')
(ev/'process-search-corrected.stdout.log').write_text(p.stdout,encoding='utf-8'); (ev/'process-search-corrected.stderr.log').write_text(p.stderr,encoding='utf-8')
save('process-search-corrected.json',dict(arguments=corrected,exitCode=p.returncode,hits=len(p.stdout.splitlines()),sameHitSet=sorted(p.stdout.splitlines())==sorted(search['results'][0]['stdout'].splitlines()),note='原记录fork正则双反斜杠，独立补查正确空白/左括号转义；仅核实补查命中，不据同名未命中宣告缺口。'))
cat=next(c for c in json.loads(read(d/'reuse/catalog.json'))['capabilities'] if c['id']=='UTIL-THREAD'); cb=[]
for u in cat['upstream']:
 t=read(repo/u['path']); cm=read(repo/u['targetFile']); cb.append(dict(path=u['path'],sourceHashMatches=digest(repo/u['path'])==u['sha256'],targetHashMatches=digest(repo/u['targetFile'])==u['targetSha256'],symbols={s:bool(re.search(r'\b'+re.escape(s)+r'\s*\(',t)) for s in u['symbols']},target=u['target'],targetDeclared=u['target'] in cm,headerListed=pathlib.Path(u['path']).name in cm))
save('catalog-bindings.json',cb)
archives=[]
roots={'REUSE-T01-batch10-r2-20261023':pathlib.Path('C:/Users/zgl18/AppData/Local/Temp/REUSE-T01-batch10-r2-4fd24994'),'REUSE-T01-batch11-20261023':pathlib.Path('C:/Users/zgl18/AppData/Local/Temp/ird-accept-b11-20261023-003003-759712800'),'REUSE-T01-planning-api-20261023':pathlib.Path('C:/Users/zgl18/AppData/Local/Temp/ird-accept-planning-f8bd92c90dd245928f3f01895191d033')}
for name,original in roots.items():
 archive=d/'traceability/acceptance'/name
 for f in sorted(archive.iterdir()):
  if not f.is_file(): continue
  orig=original/f.name
  a=f.read_bytes(); b=orig.read_bytes() if orig.exists() else None
  archives.append(dict(archive=name,file=f.name,original=str(orig),exists=orig.is_file(),rawMatches=a==b,newlineMatches=b is not None and a.replace(b'\r\n',b'\n')==b.replace(b'\r\n',b'\n')))
 # Any original root-level artifacts omitted by archive must be explicit.
 for f in sorted(original.iterdir()) if original.exists() else []:
  if f.is_file() and not (archive/f.name).exists(): archives.append(dict(archive=name,file=f.name,original=str(f),omitted=True))
save('archive-checks.json',archives)
print('DEFS',len(defs),'complete boundaries',sum(x['completeBoundary'] for x in defs))
print('SEARCH',len(replay),'matching',sum(x['exitMatches'] and x['stdoutSetMatches'] and x['stderrMatches'] for x in replay))
print('CATALOG',len(cb),'matching',sum(x['sourceHashMatches'] and x['targetHashMatches'] and all(x['symbols'].values()) and x['targetDeclared'] and x['headerListed'] for x in cb))
for name in roots:
 a=[x for x in archives if x['archive']==name]; print('ARCHIVE',name,'files',len(a),'raw',sum(x.get('rawMatches',False) for x in a),'newline',sum(x.get('newlineMatches',False) and not x.get('rawMatches',False) for x in a),'missing_or_mismatch',sum(not x.get('newlineMatches',False) for x in a))
