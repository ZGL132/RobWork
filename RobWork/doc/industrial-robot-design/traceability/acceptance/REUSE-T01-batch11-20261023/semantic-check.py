import pathlib,json,subprocess,re,hashlib,sys
sys.stdout.reconfigure(encoding='utf-8')
ev=pathlib.Path(__file__).parent;repo=ev/'repo';d=repo/'RobWork/doc/industrial-robot-design'; rows=json.loads((ev/'new-rows.json').read_text(encoding='utf-8')); cats=json.loads((ev/'new-catalog.json').read_text(encoding='utf-8'))
def digest(path):return hashlib.sha256((repo/path).read_text(encoding='utf-8-sig').rstrip('\n').__add__('\n').encode()).hexdigest()
catchecks=[]
for cat in cats:
 for sec in ['upstream','tests']:
  for x in cat.get(sec,[]):
   check=dict(capabilityId=cat['id'],section=sec,path=x['path'],hashMatch=digest(x['path'])==x['sha256'],symbols={s:s in (repo/x['path']).read_text(encoding='utf-8-sig') for s in x.get('symbols',[])})
   if 'targetFile' in x:check.update(target=x['target'],targetFile=x['targetFile'],targetHashMatch=digest(x['targetFile'])==x['targetSha256'])
   catchecks.append(check)
(ev/'catalog-bindings.json').write_text(json.dumps(catchecks,ensure_ascii=False,indent=2),encoding='utf-8')
print('catalog refs',len(catchecks),'hashes/symbols',all(x['hashMatch'] and all(x['symbols'].values()) and x.get('targetHashMatch',True) for x in catchecks))
sections=[];defs=[]
for r in rows:
 for s in r['sources']:
  lines=(repo/s['path']).read_text(encoding='utf-8-sig').splitlines(); a=s['startLine'];b=s['endLine'];region='\n'.join(lines[a-1:b])
  # 去注释及字符串再数大括号：辅助发现截断，不能单靠此证明真实语义作用域。
  code=re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'','',region,flags=re.S)
  bal=0;end=None;started=False
  for j,c in enumerate(code):
   if c=='{':bal+=1;started=True
   elif c=='}':
    bal-=1
    if started and bal==0:end=j;break
  ok=bal==0 and end is not None and not code[end+1:].strip()
  defs.append(dict(functionId=r['functionId'],qualifiedSymbol=s['qualifiedSymbol'],completeBalancedDefinition=ok,startLine=a,endLine=b,returnSignature=region.split('{')[0].strip(),decision=r['decision'],status=r['status']))
  sections.append(r['functionId']+'\n'+'\n'.join(f'{i}: {lines[i-1]}' for i in range(max(1,a-3),min(len(lines),b+3)+1)))
(ev/'definition-checks.json').write_text(json.dumps(defs,ensure_ascii=False,indent=2),encoding='utf-8');(ev/'source-excerpts.log').write_text('\n\n'.join(sections),encoding='utf-8')
print('defs',len(defs),'balanced',sum(x['completeBalancedDefinition'] for x in defs),'pending',sum(x['decision']=='pending' and x['status']=='pending' for x in defs))
search=json.loads((d/'traceability/reuse/REUSE-T01/batch11-search.json').read_text(encoding='utf-8-sig'))
records=[]
for i,x in enumerate(search['results'],1):
 if any(v.startswith('D:/') or v.startswith('C:/') for v in x['arguments']):
  records.append(dict(index=i,result='NOT REPLAYED',reason='外部实施环境cache非冻结源码；默认OFF另由冻结CMake核对'));continue
 args=x['arguments'];r=subprocess.run(args,cwd=repo,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
 (ev/f'search-{i}.stdout.log').write_bytes(r.stdout);(ev/f'search-{i}.stderr.log').write_bytes(r.stderr)
 records.append(dict(index=i,command=args,exitCode=r.returncode,recordedExitCode=x['exitCode'],lines=len(r.stdout.splitlines()),stdoutMatchesRecorded=r.stdout.decode('utf-8').replace('\r\n','\n')==x['stdout'].replace('\r\n','\n'),sortedHitsMatchRecorded=sorted(r.stdout.decode('utf-8').splitlines())==sorted(x['stdout'].splitlines())))
 print('search',i,r.returncode,len(r.stdout.splitlines()))
(ev/'search-replay.json').write_text(json.dumps(records,ensure_ascii=False,indent=2),encoding='utf-8')
