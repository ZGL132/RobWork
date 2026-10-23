import pathlib,subprocess,json,hashlib,xml.etree.ElementTree as E,collections
R=pathlib.Path(__file__).parent;repo=R/'repo-isolated';base=repo/'RobWork/doc/industrial-robot-design';norm=lambda b:b.decode('utf-8-sig').replace('\r\n','\n').rstrip('\n')+'\n';dig=lambda b:hashlib.sha256(norm(b).encode()).hexdigest()
checks=[]
for batch,origin in [('17',pathlib.Path('C:/Users/zgl18/AppData/Local/Temp/REUSE-T01-B17-independent-7eb99134')),('18',pathlib.Path('C:/Users/zgl18/AppData/Local/Temp/ird-accept-b18-20261023-020423-1105365130'))]:
 prefix=f'RobWork/doc/industrial-robot-design/traceability/acceptance/REUSE-T01-batch{batch}-20261023/'
 files=subprocess.check_output(['git','ls-tree','-r','--name-only','HEAD','--',prefix],cwd=repo,text=True).splitlines()
 for f in files:
  rel=f[len(prefix):];src=origin/rel;b=(repo/f).read_bytes();blob=subprocess.check_output(['git','cat-file','blob','HEAD:'+f],cwd=repo)
  checks.append({'batch':batch,'path':f,'bytes':len(b),'workingCopyMatchesGitBlob':b==blob,'originalExists':src.is_file(),'originalRawMatches':src.read_bytes()==blob if src.is_file() else None,'sha256':hashlib.sha256(b).hexdigest()})
(R/'archive-original-byte-checks.json').write_text(json.dumps(checks,indent=2),encoding='utf-8');print('ARCHIVE',json.dumps({'counts':dict(collections.Counter(x['batch'] for x in checks)),'matchingOriginal':sum(x['originalRawMatches'] is True for x in checks),'missingOriginal':[x['path'] for x in checks if not x['originalExists']],'failures':[x for x in checks if x['originalRawMatches'] is False or not x['workingCopyMatchesGitBlob']]},ensure_ascii=False))
cat=json.loads((base/'reuse/catalog.json').read_text(encoding='utf-8-sig'));caps=cat.get('capabilities',cat.get('catalog',[])); cc=[]
for c in caps:
 if c['id'] in ['UTIL-FILE','MATH-RANDOM']:
  for u in c['upstream']:
   for path,sha in [(u['path'],u['sha256']),(u['targetFile'],u['targetSha256'])]:cc.append({'capabilityId':c['id'],'path':path,'match':dig((repo/path).read_bytes())==sha})
(R/'candidate-checks.json').write_text(json.dumps(cc,indent=2),encoding='utf-8');print('CANDIDATES',len(cc),'FAIL',sum(not x['match'] for x in cc))
nchecks=[]
for label,p in [('independent',R),('original-final',base/'traceability/reuse/REUSE-T01/probe-r20'),('original-initial',base/'traceability/reuse/REUSE-T01/probe-r20/initial-four')]:
 report=json.loads((p/'ird-test-report.json').read_text(encoding='utf-8-sig'));xml=E.parse(p/'gtest.xml').getroot(); tests=xml.findall('.//testcase'); native={t['testId']:t['outcome'] for t in report['tests']}; outcomes={t.attrib['classname']+'.'+t.attrib['name']:('failed' if t.find('failure') is not None else 'skipped' if t.find('skipped') is not None else 'passed') for t in tests}; actual=collections.Counter(outcomes.values()); nchecks.append({'label':label,'summary':report['summary'],'xmlTests':int(xml.attrib['tests']),'xmlFailures':int(xml.attrib['failures']),'actualCounts':dict(actual),'testcasesMatch':native==outcomes,'allOnlyFilteredNewTests':all(n.startswith('ReuseProjectBoundary.') for n in native),'nativeSummaryMatch':all(report['summary'][k]==actual.get(k,0) for k in ['passed','failed','skipped']),'runExit':int((p/('run.exit.txt' if label=='independent' else 'exit.txt')).read_text(encoding='utf-8-sig'))})
(R/'native-xml-exit-checks.json').write_text(json.dumps(nchecks,indent=2),encoding='utf-8');print('NATIVE',json.dumps(nchecks))
env=json.loads((base/'traceability/reuse/REUSE-T01/probe-r20/environment-artifacts.json').read_text(encoding='utf-8-sig'));ec=[]
for b in env['inputs']+[env[x] for x in ['xml','report','stdout']]:ec.append({'path':b['path'],'match':dig((repo/b['path']).read_bytes())==b['sha256']})
(R/'original-evidence-checks.json').write_text(json.dumps(ec,indent=2),encoding='utf-8');print('ORIGINAL EVIDENCE',len(ec),'FAIL',sum(not x['match'] for x in ec))
