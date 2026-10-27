import pathlib,subprocess,json,re,sys,hashlib
out=pathlib.Path(sys.argv[1]); repo=out/'repo-copy'; scripts=repo/'RobWork/scripts/industrialrobot'; probes=out/'independent'; probes.mkdir(exist_ok=True); results=[]
old=probes/'before-build-context-pack.ps1'; old.write_bytes(subprocess.check_output(['git','show','7c263e78683a74b5532e6f99e7647a02b6f39ea7:RobWork/scripts/industrialrobot/build-context-pack.ps1'],cwd=repo))
fixed=['RobWork/doc/industrial-robot-design/REQUIREMENTS.md','RobWork/doc/industrial-robot-design/context/reading-paths.md','RobWork/doc/industrial-robot-design/context/asset-inventory.json','AGENTS.md','RobWork/doc/industrial-robot-design/AGENTS.md','RobWork/RobWorkStudio/src/rwslibs/industrialrobot/AGENTS.md']
for shell in ['pwsh','powershell']:
 fixture=probes/(shell+'-fixture'); design=fixture/'RobWork/doc/industrial-robot-design'; contract=design/'tasks/foundation/ACC-T01.json'; refs=['context/README.md#3','./context/README.md#5','context\\extra.md#x','context/extra.md#other','',None,'REQUIREMENTS.md#x']; paths=fixed+['RobWork/doc/industrial-robot-design/context/README.md','RobWork/doc/industrial-robot-design/context/extra.md']
 for path in paths:
  p=fixture/path;p.parent.mkdir(parents=True,exist_ok=True);p.write_text('fixture '+path+'\n',encoding='utf-8')
 contract.parent.mkdir(parents=True,exist_ok=True);contract.write_text(json.dumps(dict(taskId='ACC-T01',title='独立边界验收',unit='ui',status='ready',dependsOn=[],designRefs=refs),ensure_ascii=False),encoding='utf-8')
 for args in [['git','-C',fixture,'init','-q'],['git','-C',fixture,'config','core.autocrlf','false']]:subprocess.run(list(map(str,args)),check=True,capture_output=True)
 def run(name,script,args,expected,pattern):
  cmd=[shell,'-NoProfile','-File',str(script),'-RepoRoot',str(fixture/'RobWork')]+args;p=subprocess.run(cmd,cwd=repo,capture_output=True);log=(p.stdout+p.stderr).decode('utf-8',errors='replace');(probes/(shell+'-'+name+'.log')).write_text(log,encoding='utf-8');ok=p.returncode==expected and re.search(pattern,log,re.I) is not None and 'ParserError' not in log
  results.append(dict(shell=shell,case=name,exitCode=p.returncode,expected=expected,diagnostic=pattern,passed=ok,command=cmd));return log
 validator=scripts/'validate-context-pack.ps1';selected=['-TaskId','ACC-T01']
 run('all-no-directory',validator,[],0,'nothing to validate');run('selected-no-directory',validator,selected,1,'pack not found')
 run('baseline-build',old,selected,0,'build-context-pack: ACC-T01');pack=design/'context/packs/ACC-T01.md';baseline=pack.read_text(encoding='utf-8-sig')
 run('frozen-build',scripts/'build-context-pack.ps1',selected,0,'build-context-pack: ACC-T01');original=pack.read_text(encoding='utf-8-sig')
 rows=lambda t:[line for line in t.splitlines() if re.match(r'^\| .+ \| [0-9a-f]{40} \|$',line)]
 ok=rows(baseline)==rows(original) and len(rows(original))==9
 results.append(dict(shell=shell,case='exact-baseline-fingerprint-rows-with-anchors-dedup-backslash',passed=ok,rows=len(rows(original))))
 results.append(dict(shell=shell,case='navigation-current-rules',passed=all(s in original for s in ['AGENTS.md` §1','可选导航','仅自动编排读取','人工任务不读']) and '五条' not in original))
 run('fresh',validator,selected,0,'fresh=1')
 for n,row in enumerate(rows(original)):
  pack.write_text(original.replace(row,'',1),encoding='utf-8');run('removed-required-'+str(n),validator,selected,1,'missing required fingerprint')
 pack.write_text(original,encoding='utf-8');row=rows(original)[0];pack.write_text(original.replace(row,re.sub('[a-f0-9]{40}','z'*40,row)),encoding='utf-8');run('malformed-sha',validator,selected,1,'fingerprint row is malformed')
 pack.write_text(original,encoding='utf-8');p=fixture/fixed[3];saved=p.read_bytes();p.write_bytes(saved+b'changed\n');run('changed-required-rule',validator,selected,2,'STALE source');p.write_bytes(saved)
 pack.write_text(original,encoding='utf-8');p=fixture/paths[-1];saved=p.read_bytes();p.unlink();run('missing-source-kept-row',validator,selected,2,'MISSING source');p.write_bytes(saved)
 pack.write_text(original,encoding='utf-8')
report=dict(executed=len(results),passed=sum(x['passed'] for x in results),failed=sum(not x['passed'] for x in results),skipped=0,cases=results)
(probes/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');print(json.dumps({k:v for k,v in report.items() if k!='cases'}));sys.exit(bool(report['failed']))
