import pathlib,json,xml.etree.ElementTree as ET,hashlib,subprocess
p=pathlib.Path(__file__).parent
rows=[]
for x in sorted(p.glob('*.xml')):
 ts=list(ET.parse(x).getroot().iter('testcase'))
 failed=[t.attrib.get('classname','')+'.'+t.attrib.get('name','') for t in ts if t.find('failure') is not None]
 skipped=[t.attrib.get('classname','')+'.'+t.attrib.get('name','') for t in ts if t.find('skipped') is not None]
 notRun=[t for t in ts if t.attrib.get('status')=='notrun']
 ids=[t.attrib.get('classname','')+'.'+t.attrib.get('name','') for t in ts]
 report=p/(x.stem+'-report.json');native=None
 if report.exists():
  d=json.loads(report.read_text(encoding='utf-8-sig'));recordIds=[t.get('testId') for t in d.get('tests',[])]
  native={'summary':d.get('summary'),'records':len(recordIds),'idSetMatchesXml':set(recordIds)==set(ids),'zeroRecords':not recordIds}
 rows.append({'suite':x.stem,'total':len(ts),'passed':len(ts)-len(failed)-len(skipped)-len(notRun),'notRun':len(notRun),'failed':len(failed),'skipped':len(skipped),'failedIds':failed,'skippedIds':skipped,'nativeReport':native})
summary={'suites':rows,'total':sum(r['total'] for r in rows),'passed':sum(r['passed'] for r in rows),'failed':sum(r['failed'] for r in rows),'skipped':sum(r['skipped'] for r in rows),'notRun':sum(r['notRun'] for r in rows),'missingNativeReports':[r['suite'] for r in rows if r['nativeReport'] is None]}
(p/'test-summary.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2),encoding='utf-8')
exeRoot=pathlib.Path('build/RobWorkStudio/bin/Release')
binaryFiles=sorted(set(exeRoot.glob('sdurws_ird_*.exe')) | set(exeRoot.glob('sdurws_ird_*.dll')) | set(pathlib.Path('build/RobWorkStudio/libs/Release').glob('sdurws_ird_*.dll')))
binaries=[{'path':f.as_posix(),'name':f.name,'sha256':hashlib.sha256(f.read_bytes()).hexdigest(),'bytes':f.stat().st_size} for f in binaryFiles]
(p/'binary-hashes.json').write_text(json.dumps(binaries,indent=2),encoding='utf-8')
print(json.dumps({k:v for k,v in summary.items() if k!='suites'},ensure_ascii=False))


