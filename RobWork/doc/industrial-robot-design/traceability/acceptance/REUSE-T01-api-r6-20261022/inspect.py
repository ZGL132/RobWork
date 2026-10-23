import pathlib,json,subprocess,hashlib,math,sys,xml.etree.ElementTree as ET
from fractions import Fraction
ev=pathlib.Path(__file__).parent; repo=ev/'repo'
probe=repo/'RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01'
def git(*a): return subprocess.check_output(['git','-C',str(repo),*a],text=True).strip()
def normalized(p): return p.read_text(encoding='utf-8-sig').replace('\r\n','\n').rstrip('\n')+'\n'
def digest(p): return hashlib.sha256(normalized(p).encode()).hexdigest()
sha=git('rev-parse','HEAD'); base='c7e1469b40ca7c1dbba55a3b6bd8a443fc6086c5'
assert sha=='877f6afee38446ab43c5e6a6fd9b09593925aedb'
changes=git('diff','--name-only',base,'HEAD').splitlines()
assert all(p.startswith('RobWork/doc/industrial-robot-design/') for p in changes)
rows=[]; audit=[]
for f in (repo/'RobWork/doc/industrial-robot-design/reuse/functions').glob('*.json'):
    fs=json.loads(f.read_text(encoding='utf-8-sig'))['functions']; rows+=fs
    for r in fs:
        for b in r.get('auditEvidence',[]):
            if b['path'].startswith('RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01/probe-r6/') or b['path'].endswith('UpstreamSuitabilityTest.cpp'):
                actual=digest(repo/b['path']); assert actual==b['sha256'],(r['functionId'],b)
                audit.append({'functionId':r['functionId'],'path':b['path'],'sha256':actual,'matches':True})
assert len(audit)==11 and len(rows)==121
assert all(r['decision']==r['status']=='pending' for r in rows)
env=json.loads((probe/'probe-r6/environment-artifacts.json').read_text(encoding='utf-8-sig'))
artifacts=[]
for a in env['artifacts']:
    p=pathlib.Path(a['path']); raw=p.read_bytes(); actual=hashlib.sha256(raw).hexdigest()
    assert actual==a['sha256'] and len(raw)==a['bytes'], a
    artifacts.append(dict(a,matches=True))
for k in ['source','build']:
    assert digest(repo/env[k]['path'])==env[k]['sha256']
assert normalized(probe/'probe-r6/hypothesis-source.txt')==normalized(probe/'UpstreamSuitabilityTest.cpp')
assert normalized(probe/'probe-r4/hypothesis-source.txt').count('TEST(ReuseAudit,')==18
assert 'EXPECT_NEAR(1.8, EAA<>(inverse(original) * recovered).angle(), 1e-12);' in normalized(probe/'probe-r4/hypothesis-source.txt')
snapshots=[]
for name,total,passed,failed in [('r4',18,17,1),('r5',18,18,0),('r6',19,19,0)]:
    folder=probe/('probe-'+name); xml=ET.parse(folder/'gtest.xml').getroot()
    native=json.loads((folder/'ird-test-report.json').read_text(encoding='utf-8-sig'))
    s=native['summary']; cases=xml.findall('.//testcase')
    assert int(xml.attrib['tests'])==total and len(cases)==total and int(xml.attrib['failures'])==failed
    assert (s['total'],s['passed'],s['failed'],s['skipped'],s['notRun'])==(total,passed,failed,0,0)
    assert all(t.attrib['status']=='run' for t in cases)
    if failed:
        f=xml.findall('.//testcase[failure]'); assert len(f)==1 and f[0].attrib['name']=='RpyExactPositiveGimbalHasRoundTripLimitation'
        assert '4.1992870233496388e-17' in (folder/'tests.log').read_text(encoding='utf-8-sig')
    assert int((folder/'build.exit.txt').read_text(encoding='utf-8-sig'))==0
    assert int((folder/'tests.exit.txt').read_text(encoding='utf-8-sig'))==(1 if failed else 0)
    snapshots.append({'attempt':name,'xmlTotal':total,'native':s,'allActuallyRan':True})
# Source under test's original 14 cases are unchanged except two extra includes.
old=subprocess.check_output(['git','-C',str(repo),'show',base+':RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01/UpstreamSuitabilityTest.cpp'],text=True,encoding='utf-8-sig')
new=normalized(probe/'UpstreamSuitabilityTest.cpp')
prefix=new[:new.index('// REQ-10/REQ-11')].replace('#include <limits>\n','').replace('#include <Eigen/Cholesky>\n','').rstrip()
assert prefix==old.rstrip()
def mul(a,b): return [[sum(a[i][k]*b[k][j] for k in range(3)) for j in range(3)] for i in range(3)]
def rz(a):
    c,s=math.cos(a),math.sin(a); return [[c,-s,0],[s,c,0],[0,0,1]]
def ry(a):
    c,s=math.cos(a),math.sin(a); return [[c,0,s],[0,1,0],[-s,0,c]]
def rx(a):
    c,s=math.cos(a),math.sin(a); return [[1,0,0],[0,c,-s],[0,s,c]]
def rot(y,p,r): return mul(mul(rz(y),ry(p)),rx(r))
def difference(a,b): return max(abs(a[i][j]-b[i][j]) for i in range(3) for j in range(3))
rpy=[]
for p in [-math.pi/2+1e-8,-.4,.4,math.pi/2-1e-8,-math.pi/2,math.pi/2]:
    original=rot(.7,p,-.2)
    if p==math.pi/2: fields=[0,p,-.9]
    elif p==-math.pi/2: fields=[0,p,.5]
    else: fields=[.7,p,-.2]
    error=difference(original,rot(*fields)); assert error<1e-12
    rpy.append({'originalYawPitchRoll':[.7,p,-.2],'equivalentYawPitchRoll':fields,'maxMatrixDifference':error,'sourceR12':original[0][1]})
assert abs(rpy[-1]['sourceR12']+math.sin(.9))<1e-15
tiny=Fraction.from_float(math.ulp(0.0)); assert tiny>0 and tiny==Fraction(1,2**1074)
boundaries=[]
for off in [math.nextafter(1.,0.),1.,math.nextafter(1.,2.)]:
    low=1-Fraction.from_float(off); high=1+Fraction.from_float(off)
    boundaries.append({'off':off,'minimumEigenvalueExact':str(low),'maximumEigenvalueExact':str(high),'strictlyPositiveDefinite':low>0})
decimal=Fraction.from_float(1e-14)
assert decimal>0 and decimal*decimal-decimal*decimal==0
oracles={'rpy':rpy,'positiveGimbal':'r12=R(0,1)=sin(roll-yaw)=-sin(0.9); yaw=0,roll=-0.9 is same rotation, not an upstream bug','negativeGimbal':'yaw=0,roll=roll+yaw=0.5 at pitch=-pi/2 is same rotation','mixedScaleDiagonal':{'minimumExact':str(tiny),'middle':1,'maximum':1e300,'strictSPD':True,'spectralResult':'actual API returns minimum zero, contradicting exact positive diagonal oracle'},'unitBlockBoundary':boundaries,'rankOneDecimalBlock':{'storedEntryExact':str(decimal),'spectrumExact':['0',str(2*decimal),'1'],'nullVector':[1,-1,0],'strictSPD':False,'reason':'First two columns are exactly equal binary64 values, so determinant zero independent of decimal rounding'}}
out={'frozenSha':sha,'baseSha':base,'trackedStatus':git('status','--short'),'changedFiles':changes,'pendingRelations':len(rows),'closedFiles':0,'auditBindings':audit,'environmentRawArtifacts':artifacts,'environmentSourceAndBuildNormalizedHashesMatch':True,'probeR6SourceSnapshotMatchesFrozenSource':True,'historicalSnapshots':snapshots,'independentOracles':oracles,'prior14CasesUnchanged':True}
assert out['trackedStatus']==''
(ev/'independent-review.json').write_text(json.dumps(out,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({k:v for k,v in out.items() if k not in ['auditBindings','changedFiles','independentOracles']},ensure_ascii=False,indent=2))
