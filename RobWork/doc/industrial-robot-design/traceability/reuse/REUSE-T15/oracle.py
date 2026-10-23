"""GAP-CORE-FNV-01：任意精度整数oracle，独立于Boost及旧自持乘法。"""
from pathlib import Path
import random,subprocess,json,hashlib,sys
R=Path.cwd();E=R/'RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T15'
def fnv(b):
 h=0x6c62272e07bb014262b821756295c58d
 for x in b:h=((h^x)*0x1000000000000000000013b)%2**128
 return h
rng=random.Random(20261023)
cases=[bytes(range(n)) for n in [0,1,2,15,16,17,31,32,33,63,64,65,255]]+[b'\xff'*16,b'\0'*32,b'a',b'foobar']
cases += [rng.randbytes(rng.randrange(0,4097)) for _ in range(1000)]
p=subprocess.run([str(R/(sys.argv[1] if len(sys.argv)>1 else '.cache/foundation/boost-probe.exe'))],input=''.join(x.hex()+'\n' for x in cases),text=True,capture_output=True,check=True)
out=[(int(a,16)<<64)|int(b,16) for a,b in (l.split() for l in p.stdout.splitlines())]
assert len(out)==len(cases); bad=[i for i,(b,h) in enumerate(zip(cases,out)) if fnv(b)!=h]
(E/(sys.argv[2] if len(sys.argv)>2 else 'boost-oracle.json')).write_text(json.dumps(dict(seed=20261023,count=len(cases),failed=bad,corpusSha256=hashlib.sha256(b''.join(cases)).hexdigest(),known=[dict(input=b.hex(),hi=f'{fnv(b)>>64:016x}',lo=f'{fnv(b)%(2**64):016x}') for b in cases[:17]]),indent=2)+'\n',encoding='utf-8')
assert not bad
print('Boost fixed uint128 exact oracle:',len(out),'passed, 0 skipped, 0 failed')
