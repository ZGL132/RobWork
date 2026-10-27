import pathlib,subprocess,json,hashlib,sys
out=pathlib.Path(sys.argv[1]);repo=out/'repo-copy';record=json.loads((repo/'RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01/batch27/search.json').read_text(encoding='utf-8-sig'));results=[]
for n,c in enumerate(record['commands']):
 p=subprocess.run(c['command'],cwd=repo,capture_output=True);(out/('search-'+str(n)+'.log')).write_bytes(p.stdout+p.stderr);results.append(dict(command=c['command'],expectedExitCode=c['exitCode'],exitCode=p.returncode,passed=p.returncode==c['exitCode']))
for s in record['readSources']:
 text=(repo/s['path']).read_text(encoding='utf-8-sig').replace('\r\n','\n').rstrip('\n')+'\n';actual=hashlib.sha256(text.encode()).hexdigest();results.append(dict(path=s['path'],expected=s['sha256'],actual=actual,passed=actual==s['sha256']))
report=dict(executed=len(results),passed=sum(x['passed'] for x in results),failed=sum(not x['passed'] for x in results),cases=results);(out/'search-replay.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items() if k!='cases'}))
