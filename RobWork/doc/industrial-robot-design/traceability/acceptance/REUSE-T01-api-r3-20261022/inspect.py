import pathlib, json, subprocess, hashlib, math
from fractions import Fraction
ev=pathlib.Path(__file__).parent
repo=ev/'repo'
def git(*a): return subprocess.check_output(['git','-C',str(repo),*a], text=True).strip()
assert git('rev-parse','HEAD')=='4e142e485343e8bade52a8481eefbb447b017989'
base=git('rev-parse','dfc317f234d9')
changed=git('diff','--name-only',base,'HEAD').splitlines()
assert all(p.startswith('RobWork/doc/industrial-robot-design/') for p in changed)
rows=[]
for f in (repo/'RobWork/doc/industrial-robot-design/reuse/functions').glob('*.json'):
    rows += json.loads(f.read_text(encoding='utf-8-sig'))['functions']
assert len(rows)==95
assert all(r['decision']=='pending' and r['status']=='pending' for r in rows)
archive=[]
for label,source in [('r1','ird-accept-t01-batch2-20261022-213631'),('r2','ird-accept-t01-batch2-r2-20261022-214644')]:
    original=ev.parent/source
    folder=repo/f'RobWork/doc/industrial-robot-design/traceability/acceptance/REUSE-T01-batch2-{label}-20261022'
    for archived in sorted(folder.iterdir()):
        assert archived.is_file(), archived
        src=original/archived.name
        assert src.is_file(), src
        a,b=src.read_bytes(), archived.read_bytes()
        item={'attempt':label,'file':archived.name,'originalSha256':hashlib.sha256(a).hexdigest(),'archivedSha256':hashlib.sha256(b).hexdigest(),'exactBytes':a==b,'sameText':a.replace(b'\r\n',b'\n')==b.replace(b'\r\n',b'\n')}
        archive.append(item)
(ev/'archive-review.json').write_text(json.dumps(archive,ensure_ascii=False,indent=2),encoding='utf-8')
print('archive mismatches',json.dumps([x for x in archive if not x['exactBytes']],ensure_ascii=False))
assert all(x['sameText'] for x in archive), 'archive original text mismatch'
poly=[]
for h in [Fraction(1,2),Fraction(2)]:
    for u in [0,1]:
        vals=[]
        for k in range(4):
            # Independent monomial differentiation: c_i * i!/(i-k)! * t^(i-k).
            terms=[Fraction(i+1)*Fraction(math.factorial(i),math.factorial(i-k)) * Fraction(u)**(i-k) for i in range(k,8)]
            normalized=sum(terms)
            expected=[[1,2,6,24],[36,168,756,3024]][u][k]
            assert normalized==expected,(u,k,normalized)
            # Exact rational formula, independent of upstream Horner implementation.
            temporal=sum(Fraction(i+1)*Fraction(math.factorial(i),math.factorial(i-k))*(h*u)**(i-k)/h**i for i in range(k,8))
            assert temporal==Fraction(expected)/h**k
            vals.append(float(temporal))
        poly.append({'h':float(h),'u':u,'physicalTimeDerivatives0to3':vals})
def mul(a,b): return [[sum(a[i][k]*b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]
def rx(a):
    c,s=math.cos(a),math.sin(a)
    return [[1,0,0,0],[0,c,-s,0],[0,s,c,0],[0,0,0,1]]
def rz(a):
    c,s=math.cos(a),math.sin(a)
    return [[c,-s,0,0],[s,c,0,0],[0,0,1,0],[0,0,0,1]]
def translation(x,y,z): return [[1,0,0,x],[0,1,0,y],[0,0,1,z],[0,0,0,1]]
alpha,a,d,theta=.4,.3,-.2,.7
standard=mul(mul(mul(rz(theta),translation(0,0,d)),translation(a,0,0)),rx(alpha))
c,s,ca,sa=math.cos(theta),math.sin(theta),math.cos(alpha),math.sin(alpha)
explicit=[[c,-s*ca,s*sa,a*c],[s,c*ca,-c*sa,a*s],[0,sa,ca,d],[0,0,0,1]]
craig=mul(mul(mul(rx(alpha),translation(a,0,0)),rz(theta)),translation(0,0,d))
norm=lambda x,y: math.sqrt(sum((x[i][j]-y[i][j])**2 for i in range(4) for j in range(4)))
assert norm(standard,explicit)<1e-14
assert norm(standard,craig)>.1
oracles={
 'parallelIK':{'tcpCircle':'p(q)=(cos(q),sin(q),0) m','distanceSquaredToTarget':'5-4*cos(q) >= 1','minimumDistanceMetres':1,'q0Jacobian':[0,1,0],'q0Residual':[1,0,0],'pseudoinverseStep':0,'meaning':'Unreachable target; zero tangential gradient leaves q=0 through both iterations; source returns in-bounds q after logging budget exhaustion.'},
 'standardDH':{'order':'Rz(theta)*Tz(d)*Tx(a)*Rx(alpha)','fourFactorMatrix':standard,'explicitFormulaDifferenceNorm':norm(standard,explicit),'craigOrder':'Rx(alpha)*Tx(a)*Rz(theta)*Tz(d)','standardCraigDifferenceNorm':norm(standard,craig)},
 'antiparallel':{'sourceBranch':'For z, idx=0 so axis=normalize(z cross x)=+y','upstreamRyPiDiagonal':[-1,1,-1],'productRxPiDiagonal':[1,-1,-1],'relativeRyPiInverseRxPiDiagonal':[-1,-1,1],'relativeAxis':'tool-local z','relativeAngleRad':math.pi,'bothMapZToMinusZ':True},
 'polynomial':{'definition':'p(u)=sum((i+1)*u^i,i=0..7)','oracle':'sum((i+1)*i!/(i-k)!*u^(i-k)), independently via exact Fraction arithmetic','normalizedLeft':[1,2,6,24],'normalizedRight':[36,168,756,3024],'physicalTimeDerivatives':poly}
}
out={'frozenSha':git('rev-parse','HEAD'),'baseSha':base,'changedFiles':changed,'pendingRelations':len(rows),'closedFiles':0,'archiveFileCount':len(archive),'archiveExactBytes':all(x['exactBytes'] for x in archive),'archiveSameText':all(x['sameText'] for x in archive),'archiveFiles':archive,'independentOracles':oracles,'trackedStatus':git('status','--short')}
assert out['trackedStatus']==''
(ev/'independent-review.json').write_text(json.dumps(out,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({k:v for k,v in out.items() if k not in ['changedFiles','archiveFiles']},ensure_ascii=False,indent=2))
