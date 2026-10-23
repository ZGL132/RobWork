"""C4独立oracle：Python Fraction按输入binary64精确有理值判定；不调用生产公式作期望。"""
from fractions import Fraction as F
from pathlib import Path
import random,sys,math,struct,subprocess,json
R=Path.cwd();E=R/'RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T14'; m=sys.float_info.max
cases=[]
for v in [m,math.nextafter(m,0),m*.75,m*.5,-m,-math.nextafter(m,0),-m*.75,-m*.5,0.,math.ulp(0.),-math.ulp(0.)]:
 for r in [m,-m,m*.75,-m*.75,m*.5,-m*.5,0.,math.ulp(0.)]:
  for rel in [0.,.5,1.,1.5,math.nextafter(2.,0.),2.,math.nextafter(2.,math.inf),3.,m]:
   for a in [0.,math.ulp(0.),m*.5,m]:cases.append((v,r,rel,a))
rng=random.Random(20261023)
for i in range(10000):
 v=math.ldexp(rng.uniform(.5,1),1024);r=-math.ldexp(rng.uniform(.5,1),1024)
 rel=rng.uniform(0,4);a=math.ldexp(rng.uniform(0,1),1023)
 cases.append((v,r,rel,a))
def bits(x):return struct.unpack('Q',struct.pack('d',x))[0]
text=''.join(' '.join(f'{bits(x):016x}' for x in c)+'\n' for c in cases)
p=subprocess.run([str(R/'.cache/foundation/c4-probe.exe')],input=text,text=True,capture_output=True,check=True)
results=list(map(int,p.stdout.split()));assert len(results)==len(cases)
mis=[];over=0
for c,actual in zip(cases,results):
 v,r,rel,a=c;expect=abs(F(v)-F(r))<=F(rel)*abs(F(r))+F(a)
 overflow=math.isinf(v-r) and math.isinf(rel*abs(r)+a);over+=overflow
 if expect!=bool(actual):mis.append(dict(input=[x.hex() for x in c],expected=expect,actual=actual,bothOverflow=overflow))
result=dict(seed=20261023,total=len(cases),bothOverflow=over,mismatches=mis,scope='有限样本精确Fraction对照，不宣称全double精确；正常路径舍入/下溢保留')
(E/'oracle-result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');print(json.dumps(result)[:3000])
