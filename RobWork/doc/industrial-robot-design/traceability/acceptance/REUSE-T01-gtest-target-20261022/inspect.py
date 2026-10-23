import pathlib,json,subprocess,hashlib,xml.etree.ElementTree as ET,re,sys
ev=pathlib.Path(__file__).parent; repo=ev/'repo'
base='ee9cc79bd68ec27b8b2f95901dddd78d6c57d5d2'; frozen='7af7e86a18e34384677c2599a4f8812ad262908f'
archive=repo/'RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01/gates/gtest-target'
scripts='RobWork/scripts/industrialrobot/'
def git(*a):return subprocess.check_output(['git','-C',str(repo),*a],text=True,encoding='utf-8-sig').strip()
assert git('rev-parse','HEAD')==frozen
hashes={k:hashlib.sha256((repo/scripts/f).read_bytes()).hexdigest().upper() for k,f in [('validatorSha256','validate-reuse.ps1'),('helperSha256','reuse-contract.ps1'),('driverSha256','test-reuse-governance.ps1')]}
history=[]
def inspect_report(folder,stem,label,expected,verifyhash=False):
    j=json.loads((folder/(stem+'reuse-test-report.json')).read_text(encoding='utf-8-sig'))
    assert [j[k] for k in ['expected','executed','passed','failed','skipped','notRun']]==expected,(label,j)
    assert j['complete']==(j['notRun']==0 and not j['driverFailure'])
    assert len(j['tests'])==j['executed'] and len({t['name'] for t in j['tests']})==j['executed']
    assert sum(t['passed'] for t in j['tests'])==j['passed']
    xml=ET.parse(folder/(stem+'reuse-governance.junit.xml')).getroot()
    assert int(xml.attrib['failures'])==j['failed'] and int(xml.attrib['skipped'])==0
    assert len(xml.findall('testcase'))==j['executed']+(0 if j['complete'] else 1)
    assert len(xml.findall('.//failure'))==j['failed']
    assert bool(xml.findall('.//error'))==(not j['complete'])
    if verifyhash: assert all(j[k].upper()==h for k,h in hashes.items()),label
    return j
for r,counts in [('r1',[142,30,7,23,0,112]),('r2',[142,142,139,3,0,0]),('r3',[142,142,141,1,0,0]),('r4',[142,142,142,0,0,0])]:
    for ps in ['ps7','ps51']:
        stem=r+'-'+ps+'-'; j=inspect_report(archive,stem,r+'-'+ps,counts,r=='r4')
        history.append({'attempt':r+'-'+ps,**{k:j[k] for k in ['expected','executed','passed','failed','skipped','notRun','complete','driverFailure']},'failedNames':[t['name'] for t in j['tests'] if not t['passed']],'r4FrozenScriptHashesMatch':True if r=='r4' else None})
        if r=='r4':
            for name in ['upstream-gtest-build-target','upstream-arbitrary-test-build-path-rejected','closure-new-file-no-independent-record','closure-new-file-omitted-final']:
                t=next(t for t in j['tests'] if t['name']==name)
                assert (archive/(stem+name+'.log')).read_bytes().decode('utf-8-sig')==t['output']
if '--history-only' in sys.argv:
    (ev/'archived-report-review.json').write_text(json.dumps({'rawFrozenScriptSha256':hashes,'history':history},ensure_ascii=False,indent=2),encoding='utf-8')
    print('Archived JSON/JUnit and r4 frozen raw source hashes verified')
    sys.exit(0)
live=[]; selected=[]
keynames=['01-clean','upstream-gtest-build-target','upstream-arbitrary-test-build-path-rejected','closure-new-file-no-independent-record','closure-new-file-omitted-final','22-final-debt-rejected','function-final-restricted-pending-rejected','31-frozen-head','32-frozen-head-ignores-dirty-tree','33-frozen-head-repeatable']
for ps in ['ps7','ps51']:
    j=inspect_report(ev/ps,'',ps,[142,142,142,0,0,0],True)
    assert int((ev/(ps+'.exit.txt')).read_text(encoding='utf-8-sig'))==0
    for t in j['tests']:
        assert (ev/ps/(t['name']+'.log')).read_bytes().decode('utf-8-sig')==t['output']
        if t['name'] in keynames:
            assert t['passed']
            if t['expected']=='PASS':assert t['exitCode']==0
            else:assert t['exitCode']!=0 and t['expected'] in t['output']
            if t['name']=='01-clean':assert 'functions=200 closedFiles=0' in t['output'] and 'DEBT=16 UNRESOLVED=1399' in t['output']
            if t['name'].startswith('closure-new-file-') and t['name'].endswith(('record','final')):assert 'new/current file lacks independent closure' in t['output']
            selected.append({'engine':ps,**t})
    live.append({'engine':ps,**{k:j[k] for k in ['expected','executed','passed','failed','skipped','notRun','complete','driverFailure']},'scriptHashesMatch':True})
scope=json.loads((ev/'scope-probes.json').read_text(encoding='utf-8-sig'))
assert len(scope)==8 and all(t['passed'] for t in scope)
validator=(repo/scripts/'validate-reuse.ps1').read_text(encoding='utf-8-sig')
driver=(repo/scripts/'test-reuse-governance.ps1').read_text(encoding='utf-8-sig')
pattern=r'^RobWork/(RobWork|RobWorkSim)/(src/.+|gtest)/CMakeLists\.txt$'
assert pattern in validator
paths=[('RobWork/RobWork/gtest/CMakeLists.txt',True),('RobWork/RobWorkSim/gtest/CMakeLists.txt',True),('RobWork/RobWork/gtest/fake/CMakeLists.txt',False),('RobWork/RobWorkSim/gtest/fake/CMakeLists.txt',False),('RobWork/RobWorkStudio/gtest/CMakeLists.txt',False),('RobWork/RobWork/gtest2/CMakeLists.txt',False),('RobWork/RobWork/gtest/CMakeLists.txt.fake',False),('RobWork/RobWork/src/rw/math/CMakeLists.txt',True)]
assert all(bool(re.fullmatch(pattern,p,re.IGNORECASE))==ok for p,ok in paths)
assert driver.count('IsolateFinalClosureMatrices')==3 # definition and exactly the two closure-case calls
assert "Case '01-clean' 'PASS' {}" in driver
assert "Case '22-final-debt-rejected' 'REUSE-MIGRATION' {} @('-RequireMigrationComplete')" in driver
assert "[IO.File]::WriteAllBytes($full,$script:restore[$full])" in driver
assert "'RobWork/RobWork/gtest' 'RobWork/RobWorkSim/gtest'" in driver
protected=['RobWork/RobWorkStudio/src','RobWork/RobWork/src','RobWork/RobWork/gtest','RobWork/RobWorkSim/src','RobWork/RobWorkSim/gtest','vcpkg','RobWork/doc/industrial-robot-design/reuse/inventory.json']
assert not git('diff','--name-only',base,frozen,'--',*protected)
assert git('rev-parse',base+':'+scripts+'reuse-contract.ps1')==git('rev-parse',frozen+':'+scripts+'reuse-contract.ps1')
assert int((ev/'active-diff-check.exit.txt').read_text(encoding='utf-8-sig'))==0
rows=[]
for f in (repo/'RobWork/doc/industrial-robot-design/reuse/functions').glob('*.json'):rows+=json.loads(f.read_text(encoding='utf-8-sig'))['functions']
assert len(rows)==200 and sum(r['status']=='pending' for r in rows)==197 and sum(r['status']=='proposal' for r in rows)==3
assert git('status','--short')==''
report={'frozenSha':frozen,'baseSha':base,'trackedStatus':'','rawFrozenScriptSha256':hashes,'archiveHistory':history,'independentRuns':live,'selectedActualTests':selected,'additionalScopeProbes':scope,'allowedPathTruthTable':[{'path':p,'allowed':ok} for p,ok in paths],'isolationReview':{'helperCallsOnlyTwoClosureCases':True,'putJsonTracksOriginalBytes':True,'finallyRestoresOriginalBytes':True,'cleanAndDebtCasesKeepRealMatrices':True,'bothGtestRootsSparseCheckedOut':True,'sharedReuseHelperUnchanged':True},'protectedScopeUnchanged':protected,'relationCounts':{'total':200,'pending':197,'proposal':3,'closedFiles':0}}
(ev/'independent-review.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({'frozenSha':frozen,'independentRuns':live,'scopeProbes':len(scope),'archiveHistory':[{'attempt':h['attempt'],'passed':h['passed'],'failed':h['failed'],'notRun':h['notRun']} for h in history]},ensure_ascii=False,indent=2))
