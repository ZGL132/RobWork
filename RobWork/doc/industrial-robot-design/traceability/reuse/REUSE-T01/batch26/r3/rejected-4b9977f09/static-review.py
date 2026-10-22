import pathlib,subprocess,json,hashlib
out=pathlib.Path(__file__).parent; repo=out/'repo'
sha='4b9977f096d0b3d58687ecc94013f435e5b0502c'; base='d0fcb96f62a863684848161f7b8ecd93ef0be302'
def git(*args):return subprocess.check_output(['git','-C',str(repo),*args])
def blob(path,ref=sha):return git('show',ref+':'+path)
def norm(data):return data.decode('utf-8-sig').replace('\r\n','\n')
def digest(data):return hashlib.sha256(data).hexdigest()
archive='RobWork/doc/industrial-robot-design/traceability/acceptance/'
reports=[]
for batch,temp,expected in [('24',r'C:/Users/zgl18/AppData/Local/Temp/ird-b24-accept-7d7d2502',173),('25',r'C:/Users/zgl18/AppData/Local/Temp/REUSE-T01-batch25-accept-e331f08d036d45f29b1c84a7182f2e4c',159),('23',None,136)]:
 prefix=archive+'REUSE-T01-batch'+batch+'-20261023/'
 entries=git('ls-tree','-rz',sha,'--',prefix).split(b'\0');rows=[]
 for entry in filter(None,entries):
  meta,path=entry.split(b'\t',1);path=path.decode();mode,typ,oid=meta.decode().split();relative=path[len(prefix):]
  raw=git('cat-file','blob',oid); original=(pathlib.Path(temp)/relative).read_bytes() if temp else blob(path,base)
  rows.append(dict(path=relative,mode=mode,blob=oid,bytes=len(raw),rawSha256=digest(raw),originalSha256=digest(original),match=raw==original,forbiddenMetadata='.git' in pathlib.PurePosixPath(relative).parts))
 summary=dict(batch='B'+batch,original=temp or ('Git '+base),expected=expected,checked=len(rows),passed=sum(x['match'] for x in rows),failed=sum(not x['match'] or x['forbiddenMetadata'] or x['mode']=='160000' for x in rows),rows=rows)
 assert len(rows)==expected
 reports.append(summary)
(out/'archive-raw-report.json').write_text(json.dumps(reports,ensure_ascii=False,indent=2),encoding='utf-8')
path='RobWork/doc/industrial-robot-design/reuse/functions/foundation-governance.json'
new=json.loads(blob(path));old=json.loads(blob(path,base));oldmap={x['functionId']:x for x in old['functions']}
rows=[r for r in new['functions'] if r['functionId'] not in oldmap];checks=[]
for row in rows:
 for s in row['sources']:
  lines=norm(blob(s['path'])).split('\n');region='\n'.join(lines[s['startLine']-1:s['endLine']]); actual=digest((region.rstrip('\n')+'\n').encode())
  checks.append(dict(functionId=row['functionId'],kind='source',path=s['path'],start=s['startLine'],end=s['endLine'],match=actual==s['sha256'],symbolPresent=s['symbol'] in region))
 for kind in ['inputs','auditEvidence']:
  for b in row[kind]:
   data=norm(blob(b['path']));actual=digest((data.rstrip('\n')+'\n').encode())
   checks.append(dict(functionId=row['functionId'],kind=kind,path=b['path'],match=actual==b['sha256']))
deltas=[dict(functionId=r['functionId'],changedKeys=[k for k in set(r)|set(oldmap[r['functionId']]) if r.get(k)!=oldmap[r['functionId']].get(k)]) for r in new['functions'] if r['functionId'] in oldmap and r!=oldmap[r['functionId']]]
assertions=dict(newRows=len(rows),oldRowDeltas=deltas,pendingOwnerOnly=all(r['status']=='pending' and r['decision']=='pending' and r['usageRole']=='owner' and r['capabilityId']=='NONE' and r['targets']==[] for r in rows),bindingChecks=len(checks),bindingFailed=sum(not x['match'] or x.get('symbolPresent',True)==False for x in checks))
(out/'binding-review.json').write_text(json.dumps(dict(summary=assertions,checks=checks),ensure_ascii=False,indent=2),encoding='utf-8')
changed=git('diff','--name-only',base+'..'+sha).decode().splitlines();bad=[p for p in changed if not p.startswith(('RobWork/doc/industrial-robot-design/','RobWork/scripts/industrialrobot/'))]
tree=git('ls-tree','-rz',sha).split(b'\0');gitlinks=[x.decode() for x in tree if x.startswith(b'160000')]
script=blob('RobWork/scripts/industrialrobot/merge-findings-union.ps1');prior=blob('RobWork/scripts/industrialrobot/merge-findings-union.ps1',base)
minimal=norm(prior).replace('if ("$($oursJson.schemaVersion)" -ne', '# 顶层契约与条目正文一样逐字比较；大小写差异也必须交所有者裁决。\nif ("$($oursJson.schemaVersion)" -cne').replace('if ("$($oursJson.note)" -ne','if ("$($oursJson.note)" -cne')==norm(script)
static=dict(head=git('rev-parse','HEAD').decode().strip(),status=git('status','--short').decode(),autocrlf=git('config','core.autocrlf').decode().strip(),changedFiles=len(changed),outOfScope=bad,gitlinks=gitlinks,bom=script[:3].hex(),minimalMergePatch=minimal,archiveSummaries=[{k:v for k,v in r.items() if k!='rows'} for r in reports],bindingSummary=assertions)
(out/'static-summary.json').write_text(json.dumps(static,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(static,ensure_ascii=False,indent=2))
