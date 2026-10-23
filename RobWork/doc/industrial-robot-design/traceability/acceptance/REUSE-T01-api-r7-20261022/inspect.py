import pathlib,json,subprocess,hashlib,math,sys,struct,xml.etree.ElementTree as ET
from fractions import Fraction
ev=pathlib.Path(__file__).parent; repo=ev/'repo'
prefix='RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01/'
probe=repo/prefix
def git(*a): return subprocess.check_output(['git','-C',str(repo),*a],text=True,encoding='utf-8-sig').strip()
def norm(p): return p.read_text(encoding='utf-8-sig').replace('\r\n','\n').rstrip('\n')+'\n'
def digest(p): return hashlib.sha256(norm(p).encode()).hexdigest()
sha=git('rev-parse','HEAD'); base=git('rev-parse','HEAD^')
assert sha=='9fd7256cf61a7768622810ed547617a66013949b'
changed=git('diff','--name-only',base,sha).splitlines()
assert all(p.startswith('RobWork/doc/industrial-robot-design/') for p in changed)
protected=[prefix+'UpstreamSuitabilityTest.cpp']+git('ls-tree','-r','--name-only',base,'--',prefix+'probe-r6/').splitlines()
oldproof=[]
for p in protected:
    old=git('rev-parse',base+':'+p); new=git('rev-parse',sha+':'+p); assert old==new
    oldproof.append({'path':p,'gitBlob':old,'unchanged':True})
assert git('rev-parse','877f6afee38446ab43c5e6a6fd9b09593925aedb:'+prefix+'UpstreamSuitabilityTest.cpp')==git('rev-parse',sha+':'+prefix+'UpstreamSuitabilityTest.cpp')
assert git('diff','--name-only','877f6afee38446ab43c5e6a6fd9b09593925aedb',sha,'--',prefix+'probe-r6/')==''
expected=['BinaryArchiveScalarUsesHostRepresentation','BinaryArchiveStringDiffersFromCanonicalLengthAndWhitespace','LinearInterpolatorExtrapolatesAndCanOverflowFiniteEndpoints']
counts=[]
for label,folder in [('frozen-r7',probe/'probe-r7'),('independent-r7',ev)]:
    xml=ET.parse(folder/'gtest.xml').getroot(); cases=xml.findall('.//testcase')
    native=json.loads((folder/'ird-test-report.json').read_text(encoding='utf-8-sig')); s=native['summary']
    assert (s['total'],s['passed'],s['failed'],s['skipped'],s['notRun'])==(3,3,0,0,0)
    assert int(xml.attrib['tests'])==3 and int(xml.attrib['failures'])==0
    assert [t.attrib['name'] for t in cases]==expected and all(t.attrib['status']=='run' for t in cases)
    assert all(t.attrib['file'].replace('\\','/').endswith('/'+prefix+'ToolSuitabilityTest.cpp') for t in cases)
    assert 'Google Test filter = ReuseAudit.BinaryArchive*:ReuseAudit.LinearInterpolatorExtrapolates*' in (folder/'tests.log').read_text(encoding='utf-8-sig')
    counts.append({'label':label,'summary':s,'allActuallyRan':True,'xmlFileBinding':'ToolSuitabilityTest.cpp'})
rows=[]; bindings=[]; associated=[]
for f in (repo/'RobWork/doc/industrial-robot-design/reuse/functions').glob('*.json'):
    fs=json.loads(f.read_text(encoding='utf-8-sig'))['functions']; rows+=fs
    for r in fs:
        relevant=[b for b in r.get('auditEvidence',[]) if b['path'].startswith(prefix+'probe-r7/') or b['path']==prefix+'ToolSuitabilityTest.cpp']
        if relevant:
            assert r['decision']==r['status']=='pending'
            associated.append({'functionId':r['functionId'],'capabilityId':r['capabilityId'],'differences':r['differences'],'status':'pending'})
            for b in relevant:
                actual=digest(repo/b['path']); assert actual==b['sha256'], (r['functionId'],b)
                bindings.append({'functionId':r['functionId'],'path':b['path'],'sha256':actual,'matches':True})
assert len(rows)==146
assert sum(r['status']=='pending' for r in rows)==143 and sum(r['status']=='proposal' for r in rows)==3
env=json.loads((probe/'probe-r7/environment-artifacts.json').read_text(encoding='utf-8-sig'))
for k in ['source','build']: assert digest(repo/env[k]['path'])==env[k]['sha256']
art=[]
for a in env['artifacts']:
    raw=pathlib.Path(a['path']).read_bytes(); assert hashlib.sha256(raw).hexdigest()==a['sha256'] and len(raw)==a['bytes']; art.append(dict(a,matches=True))
catalog=json.loads((repo/'RobWork/doc/industrial-robot-design/reuse/catalog.json').read_text(encoding='utf-8-sig'))
cap=next(c for c in catalog['capabilities'] if c['id']=='UTIL-BINARCHIVE'); assert cap['review']['status']=='candidate'
for b in cap['upstream']:
    assert digest(repo/b['path'])==b['sha256'] and digest(repo/b['targetFile'])==b['targetSha256']
cmake=norm(probe/'CMakeLists.txt')
assert 'find_package(RobWork REQUIRED CONFIG COMPONENTS common invkin models trajectory)' in cmake
assert 'RW::sdurw_common' in cmake and 'ToolSuitabilityTest.cpp' in cmake
assert 'BINArchive' in norm(repo/'RobWork/RobWork/src/rw/common/CMakeLists.txt')
# Independent scalar and byte truth. Little endian is asserted only for this x64 environment.
assert sys.byteorder=='little'; native=struct.pack('@I',0x01020304); assert native==bytes([4,3,2,1])
assert str(len('x y'))+'x y'=='3x y'
assert Fraction(1)+(Fraction(3)-1)*2==5
limit=sys.float_info.max
assert math.isfinite(limit) and math.isinf(-limit-limit) and math.isinf(((-limit-limit)*.5)+limit)
assert .5*limit+.5*(-limit)==0
oracles={'scalarHostRepresentation':{'nativeByteOrder':sys.byteorder,'value':'0x01020304','bytesHex':native.hex(),'source':'BINArchive.hpp scalar writeValue uses ostream.write(address,sizeof(T)); not canonical endian'},'formattedString':{'original':'x y','serializedText':'3x y','lengthPrefix':'decimal formatted 3, no u32 bytes','read':'Formatted uint32 consumes 3; formatted char reads x, then skips space and reads y, then EOF leaves resized char zero','expectedDecodedBytesHex':'787900','testAssertions':'length=3, first x/second y, string differs; no inference of general malformed-input behavior'},'linear':{'ordinary':'x(t)=1+(3-1)*t, t=2 gives 5 outside [0,1]','durationZero':'constructor throws for duration<=0','finiteExtreme':'end-start=-DBL_MAX-DBL_MAX overflows to -infinity before duration division; midpoint result nonfinite','weightedMidpoint':0,'scope':'Listed double values only, not full replay or floating-point equivalence'}}
out={'frozenSha':sha,'baseSha':base,'trackedStatus':git('status','--short'),'changedFiles':changed,'protectedFilesUnchanged':oldproof,'unchangedFromPriorR6FrozenSha':True,'testReports':counts,'auditEvidenceBindings':bindings,'associatedFunctions':associated,'environmentArtifacts':art,'environmentSourceAndBuildNormalizedHashesMatch':True,'catalogCandidateAndBindingsValid':True,'counts':{'relations':146,'pending':143,'proposal':3,'closedFiles':0},'independentOracles':oracles}
assert out['trackedStatus']==''
(ev/'independent-review.json').write_text(json.dumps(out,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({'sha':sha,'base':base,'protectedFiles':len(protected),'reportSummaries':counts,'associatedFunctions':len(associated),'auditBindings':len(bindings),'counts':out['counts'],'trackedStatus':out['trackedStatus']},ensure_ascii=False,indent=2))
