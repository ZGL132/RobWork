import pathlib,json,subprocess,hashlib
root=pathlib.Path(__file__).parent; repo=root/'clone'; head='98c97238fa3c34aad51330b3b196439844badb69'; prefix='RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01/batch28/'
def raw(path):return subprocess.check_output(['git','-C',str(repo),'show',head+':'+path])
vectors=json.loads(raw(prefix+'fnv-vectors.json')); actual=[]; errors=[]
for v in vectors:
 state=int('6c62272e07bb014262b821756295c58d',16)
 for b in bytes.fromhex(v['hex']):state=((state^b)*int('1000000000000000000013b',16))%(1<<128)
 hi,lo=state>>64,state&((1<<64)-1); item={'name':v['name'],'hex':v['hex'],'hi':f'{hi:016x}','lo':f'{lo:016x}','fold':f'{hi^lo:016x}'};actual.append(item)
 if item!=v:errors.append(v['name'])
# Verify archived rejected evidence against the still-existing original independent Temp artifacts.
original=pathlib.Path('C:/Users/zgl18/AppData/Local/Temp/ird-accept-b28-68277e2f39'); mp=prefix+'r2/rejected-f2e651412/safe-manifest.json'; manifest=json.loads(raw(mp)); original_errors=[]
for e in manifest['artifacts']:
 p=original/'evidence'/e['path']; b=p.read_bytes() if p.is_file() else None
 if b is None or len(b)!=e['bytes'] or hashlib.sha256(b).hexdigest()!=e['rawSha256'] or b!=raw(prefix+'r2/rejected-f2e651412/'+e['path']):original_errors.append(e['path'])
result={'freshIndependentOracle':True,'vectors':actual,'errors':errors,'originalR1TempArtifactsChecked':len(manifest['artifacts']),'originalR1TempArtifactErrors':original_errors}
(root/'independent-oracle.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8');print(json.dumps(result,ensure_ascii=False,indent=2))

