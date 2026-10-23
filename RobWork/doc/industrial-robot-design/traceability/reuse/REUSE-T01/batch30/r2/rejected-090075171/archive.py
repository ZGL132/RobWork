import pathlib,json,hashlib,zipfile
root=pathlib.Path(__file__).resolve().parent
manifest=json.loads((root/'archive-manifest.json').read_text(encoding='utf-8'))
files={p.relative_to(root).as_posix():p for p in root.rglob('*') if p.is_file()}
listed={e['path'] for e in manifest['entries']}
assert set(files)-listed=={'archive-manifest.json'}
for e in manifest['entries']:
 raw=files[e['path']].read_bytes()
 assert len(raw)==e['bytes'] and hashlib.sha256(raw).hexdigest()==e['rawSha256'],e['path']
assert not any(part.lower() in {'.git','clone','build','fixture','fixtures'} for rel in files for part in pathlib.PurePosixPath(rel).parts)
archive=root.parent/'B30-independent-acceptance.zip'
with zipfile.ZipFile(archive,'w',compression=zipfile.ZIP_DEFLATED) as z:
 for rel,p in sorted(files.items()):z.write(p,rel)
with zipfile.ZipFile(archive) as z:
 assert set(z.namelist())==set(files)
 assert z.testzip() is None
 for rel in files:assert z.read(rel)==files[rel].read_bytes()
print(json.dumps({'verdict':'FAIL','finding':'B30-F01','manifestFiles':len(listed),'zipFiles':len(files),'zip':str(archive),'zipBytes':archive.stat().st_size,'zipRawSha256':hashlib.sha256(archive.read_bytes()).hexdigest(),'manifestRawSha256':hashlib.sha256((root/'archive-manifest.json').read_bytes()).hexdigest()},ensure_ascii=False,indent=2))
