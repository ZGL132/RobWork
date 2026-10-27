import pathlib,hashlib,json
root=pathlib.Path(__file__).resolve().parent
manifest=root/'safe-manifest.json'
paths=[p for p in root.iterdir() if p.is_file() and p!=manifest]
paths += [p for p in (root/'run').rglob('*') if p.is_file()]
allowed={'.json','.log','.txt','.md','.ps1','.py','.xml'}
assert all(p.suffix in allowed for p in paths)
entries=[]
for p in sorted(paths):
 relative=p.relative_to(root).as_posix()
 assert not any(x in {'clone','clone2','.git','build','fixture'} for x in p.relative_to(root).parts)
 b=p.read_bytes();entries.append({'path':relative,'bytes':len(b),'rawSha256':hashlib.sha256(b).hexdigest()})
manifest.write_text(json.dumps({'scope':'acceptance reports/logs/results/scripts only','excludedDirectories':['clone','clone2','.git','build','fixture'],'onlyExcludedEligibleFile':'safe-manifest.json','entries':entries},ensure_ascii=False,indent=2),encoding='utf-8')
for e in entries:
 b=(root/e['path']).read_bytes();assert len(b)==e['bytes'] and hashlib.sha256(b).hexdigest()==e['rawSha256']
print(json.dumps({'entries':len(entries),'allRawVerified':True,'manifest':str(manifest),'rawSha256':hashlib.sha256(manifest.read_bytes()).hexdigest()}))

