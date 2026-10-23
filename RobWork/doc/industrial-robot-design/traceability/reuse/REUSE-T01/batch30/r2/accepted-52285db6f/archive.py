import pathlib,hashlib,json,zipfile
OUT=pathlib.Path(__file__).resolve().parent
MANIFEST=OUT/'archive-manifest.json'
entries=[]
for path in sorted(OUT.rglob('*')):
 if not path.is_file() or path==MANIFEST: continue
 relative=path.relative_to(OUT)
 if any(p.lower() in ['clone','build','.git'] for p in relative.parts): raise RuntimeError('unsafe archive member '+str(relative))
 data=path.read_bytes()
 entries.append({'path':relative.as_posix(),'bytes':len(data),'rawSha256':hashlib.sha256(data).hexdigest()})
manifest={'schemaVersion':'ird-safe-archive/1','reviewedCommit':'52285db6f0d276742531ecb7e7bcf73db283e563','root':str(OUT),'excludedOnly':'archive-manifest.json','entries':entries}
MANIFEST.write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
for entry in entries:
 data=(OUT/entry['path']).read_bytes()
 if len(data)!=entry['bytes'] or hashlib.sha256(data).hexdigest()!=entry['rawSha256']: raise RuntimeError(entry['path'])
archive=OUT.parent/'B30-r2-independent-acceptance.zip'
with zipfile.ZipFile(archive,'w',compression=zipfile.ZIP_DEFLATED) as z:
 for entry in entries: z.write(OUT/entry['path'],entry['path'])
 z.write(MANIFEST,MANIFEST.name)
with zipfile.ZipFile(archive,'r') as z:
 if z.testzip() is not None: raise RuntimeError('zip CRC failure')
 if set(z.namelist())!={e['path'] for e in entries}|{MANIFEST.name}: raise RuntimeError('zip inventory mismatch')
 for entry in entries:
  data=z.read(entry['path'])
  if len(data)!=entry['bytes'] or hashlib.sha256(data).hexdigest()!=entry['rawSha256']: raise RuntimeError('zip bytes mismatch '+entry['path'])
print(json.dumps({'manifest':str(MANIFEST),'entries':len(entries),'manifestRawSha256':hashlib.sha256(MANIFEST.read_bytes()).hexdigest(),'archive':str(archive),'archiveBytes':archive.stat().st_size,'archiveRawSha256':hashlib.sha256(archive.read_bytes()).hexdigest(),'zipIntegrity':'PASS'},ensure_ascii=False))
