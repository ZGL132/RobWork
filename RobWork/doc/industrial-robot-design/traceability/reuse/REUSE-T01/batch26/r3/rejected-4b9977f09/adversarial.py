import pathlib,subprocess,json,hashlib,copy
out=pathlib.Path(__file__).parent;script=out/'repo/RobWork/scripts/industrialrobot/merge-findings-union.ps1'
target='RobWork/doc/industrial-robot-design/traceability/findings.json'
def run(cmd,cwd=None):return subprocess.run(cmd,cwd=cwd,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
def git(repo,*args):
 p=run(['git','-C',str(repo),*args]);assert p.returncode==0,(args,p.stderr);return p.stdout
def doc(entries):return dict(schemaVersion='ird-findings/1',note='RuleA',findings=[dict(id=i,title=t,when='2026-10-23T01:02:03Z') for i,t in entries])
results=[]
for shell in ['pwsh','powershell']:
 for name in ['unicode-bom-union','note-case','schema-case','same-new','new-collision','theirs-only','ours-only','both-modify','deletion','duplicate','no-delta','malformed','missing-stage']:
  root=out/'fixtures-v3'/f'{shell}-{name}';root.mkdir(parents=True)
  git(root,'init','--quiet');git(root,'config','core.autocrlf','false');(root/'empty-exclude').write_text('');git(root,'config','core.excludesfile',str(root/'empty-exclude'));(root/'.git/info/exclude').write_text('')
  docs=[doc([('F-001','原始α')]),doc([('F-001','原始α'),('F-010','我方新增')]),doc([('F-001','原始α'),('F-002','对方新增')])]
  expected=0 if name in ['unicode-bom-union','same-new','theirs-only','ours-only'] else (2 if name=='missing-stage' else 1)
  ids=['F-001','F-002','F-010']
  if name=='note-case':docs[2]['note']='Rulea'
  if name=='schema-case':docs[2]['schemaVersion']='IRD-FINDINGS/1'
  if name in ['same-new','new-collision']:
   docs[2]=copy.deepcopy(docs[1]);docs[2]['findings'].append(doc([('F-002','对方新增')])['findings'][0]);ids=['F-001','F-002','F-010']
   if name=='new-collision':docs[2]['findings'][1]['title']='异义'
  if name=='theirs-only':docs[1]=copy.deepcopy(docs[0]);docs[2]['findings'][0]['title']='对方独改';ids=['F-001','F-002']
  if name=='ours-only':docs[1]['findings'][0]['title']='我方独改'
  if name=='both-modify':docs[1]['findings'][0]['title']='我方独改';docs[2]['findings'][0]['title']='对方独改'
  if name=='deletion':docs[2]['findings']=docs[2]['findings'][1:]
  if name=='duplicate':docs[2]['findings'].append(copy.deepcopy(docs[2]['findings'][0]))
  if name=='no-delta':docs[1]=copy.deepcopy(docs[0]);docs[2]=copy.deepcopy(docs[0])
  index=[]
  for stage,d in enumerate(docs,1):
   text=json.dumps(d,ensure_ascii=False,indent=2)+'\n'
   if name=='malformed' and stage==3:text=text.replace('  ]','  ], BROKEN')
   data=text.encode('utf-8-sig' if name=='unicode-bom-union' else 'utf-8');p=root/f'stage-{stage}.json';p.write_bytes(data)
   oid=git(root,'hash-object','-w',str(p)).decode().strip();index.append(f'100644 {oid} {stage}\t{target}\n')
  work=root/target;work.parent.mkdir(parents=True);original=b'CONFLICT\r\n\x00original';work.write_bytes(original)
  if name!='missing-stage':
   p=subprocess.run(['git','-C',str(root),'update-index','--index-info'],input=''.join(index).encode(),capture_output=True);assert p.returncode==0,p.stderr
  before=git(root,'ls-files','-u');p=run([shell,'-NoProfile','-File',str(script),'-RepoRoot',str(root)]);after=git(root,'ls-files','-u');raw=work.read_bytes()
  (root/'stdout.log').write_bytes(p.stdout);(root/'stderr.log').write_bytes(p.stderr)
  passed=p.returncode==expected
  if expected:
   passed=passed and before==after and raw==original
  else:
   parsed=json.loads(raw.decode('utf-8-sig'));passed=passed and not after and [x['id'] for x in parsed['findings']]==ids and all(x['when']=='2026-10-23T01:02:03Z' for x in parsed['findings'])
   staged=git(root,'show',':0:'+target);passed=passed and staged==raw
   if name=='theirs-only':passed=passed and parsed['findings'][0]['title']=='对方独改'
   if name=='ours-only':passed=passed and parsed['findings'][0]['title']=='我方独改'
  (root/'stdout.log').write_bytes(p.stdout);(root/'stderr.log').write_bytes(p.stderr);(root/'unmerged-before.log').write_bytes(before);(root/'unmerged-after.log').write_bytes(after)
  results.append(dict(shell=shell,case=name,expected=expected,actual=p.returncode,passed=passed,skipped=False,worktreePreserved=raw==original,stagesPreserved=before==after,log=str((root/'stdout.log').relative_to(out))))
  print(json.dumps(results[-1]),flush=True)
(out/'adversarial-report.json').write_text(json.dumps(dict(passed=sum(x['passed'] for x in results),failed=sum(not x['passed'] for x in results),skipped=0,cases=results),ensure_ascii=False,indent=2),encoding='utf-8')
