from pathlib import Path
import json,subprocess,shlex,hashlib
r=Path(r'C:/Users/zgl18/AppData/Local/Temp/ird-accept-t01-batch2-r2-20261022-214644'); repo=r/'repo'; d=Path('RobWork/doc/industrial-robot-design'); base='849e03a321a1ce9642f13de4e907d9df2fb214a5'
def git(*a):return subprocess.run(['git','-C',str(repo),*a],capture_output=True).stdout.decode('utf-8-sig')
def txt(p):return (repo/p).read_text(encoding='utf-8-sig')
def digest(t):return hashlib.sha256((t.lstrip('\ufeff').replace('\r\n','\n').rstrip('\n')+'\n').encode()).hexdigest()
rows=[]
for p in (repo/d/'reuse/functions').glob('*.json'):
 current=json.loads(p.read_text(encoding='utf-8-sig')); old=git('show',base+':'+p.relative_to(repo).as_posix()); oldkeys=set() if not old else {(f['functionId'],f['capabilityId']) for f in json.loads(old)['functions']}
 for f in current['functions']:
  if (f['functionId'],f['capabilityId']) in oldkeys:continue
  s=f['sources'][0]; region='\n'.join(txt(s['path']).split('\n')[s['startLine']-1:s['endLine']]); rows.append({'matrixPath':p.relative_to(repo).as_posix(),'id':f['functionId'],'cap':f['capabilityId'],'requirements':f['requirements'],'qualifiedSymbol':s['qualifiedSymbol'],'source':s,'targets':f['targets'],'decision':f['decision'],'status':f['status'],'hashMatches':digest(region)==s['sha256'],'inputsMatch':all(digest(txt(i['path']))==i['sha256'] for i in f['inputs']),'differences':f['differences'],'region':region})
(r/'new-rows.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf-8')
s=json.loads(txt(d/'traceability/reuse/REUSE-T01/batch2-search.json')); results=[]
for i,x in enumerate(s['results'],1):
 args=shlex.split(x['command']);assert args[0]=='rg'; p=subprocess.run(args,cwd=repo,capture_output=True);out=p.stdout.decode('utf-8-sig').replace('\r\n','\n');err=p.stderr.decode('utf-8-sig').replace('\r\n','\n'); expected=x['stdout'].replace('\r\n','\n')
 (r/f'search-{i:02d}.stdout.txt').write_text(out,encoding='utf-8');(r/f'search-{i:02d}.stderr.txt').write_text(err,encoding='utf-8')
 results.append({'index':i,'command':x['command'],'expectedExitCode':x['exitCode'],'actualExitCode':p.returncode,'exitMatches':p.returncode==x['exitCode'],'stdoutExactMatches':out==expected,'stdoutLinesMatch':sorted(out.splitlines())==sorted(expected.splitlines()),'stderrMatches':err==x['stderr'],'lines':len(out.splitlines())})
(r/'search-replay.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({'rows':len(rows),'units':sorted(set(x['id'].split('.')[0] for x in rows)),'allHashes':all(x['hashMatches'] and x['inputsMatch'] for x in rows),'replay':results},ensure_ascii=False))

