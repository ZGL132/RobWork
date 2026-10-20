"""冻结 A-21 本次真实执行证据；旧证据只读，XML 与逐例报告逐套对账。"""
from pathlib import Path
import hashlib
import json
import re
import shutil
import xml.etree.ElementTree as ET

root = Path(__file__).resolve().parents[2]
source = Path(__file__).resolve().parent
dest = root / 'RobWork/doc/industrial-robot-design/traceability/builds/requirements-a21-20261020'
if dest.exists():
    raise RuntimeError('证据目录已存在，禁止覆盖不可变历史')
dest.mkdir(parents=True)
names = ['requirements', 'requirements_contract', 'requirements_gui', 'kinematics',
         'kinematics_contract', 'kinematics_gui', 'ui', 'ui_contract', 'ui_gui']
suites = []
for prefix in ['', 'standalone_']:
    for short in names:
        name = prefix + short
        xml = ET.parse(source / (name + '.xml')).getroot()
        cases = xml.findall('.//testcase')
        failures = sum(c.find('failure') is not None for c in cases)
        skipped = [c for c in cases if c.find('skipped') is not None]
        summary = json.loads((source / (name + '-report.json')).read_text(encoding='utf-8-sig'))['summary']
        assert len(cases) == int(xml.attrib['tests']) == summary['total']
        assert failures == summary['failed'] == 0
        assert len(skipped) == summary['skipped'] + summary['envUnavailable']
        assert summary['passed'] + failures + len(skipped) == len(cases)
        assert summary['notRun'] == summary['datasetInvalid'] == 0
        suites.append(dict(name=name, total=len(cases), passed=summary['passed'],
                           failed=failures, skipped=len(skipped), envUnavailable=summary['envUnavailable'],
                           skippedTests=[dict(name=c.attrib['classname'] + '.' + c.attrib['name'],
                                              reason=c.find('skipped').attrib.get('message', '')) for c in skipped]))
totals = {key: sum(s[key] for s in suites) for key in ['total', 'passed', 'failed', 'skipped', 'envUnavailable']}
tours = []
for name in ['requirements-tour', 'modeling-tour']:
    log = (source / (name + '.log')).read_text(encoding='utf-8-sig')
    passed = len(re.findall(r'\] pass ', log))
    failures = len(re.findall(r'\] FAIL', log))
    assert failures == 0 and passed > 200
    tours.append(dict(name=name, passedAssertions=passed, failedAssertions=failures,
                      screenshots=len(list((source / name).glob('*.png'))), exitCode=0))
for entry in source.iterdir():
    if entry.is_file() and entry.suffix in ['.log', '.xml', '.json', '.ps1', '.py']:
        shutil.copy2(entry, dest / entry.name)
for name in ['requirements-tour', 'modeling-tour']:
    (dest / name).mkdir()
    for entry in (source / name).glob('*.png'):
        shutil.copy2(entry, dest / name / entry.name)
result = dict(schemaVersion='requirements-a21-verification/1', taskId='WP-14-T12',
              baseCommit='277dd6aa140e4b7445718334dd5ab915c28825b3',
              scope='A-21与剪贴板重跑；不构成需求模块120项全量验收',
              environment=dict(os='Windows', toolchain='Visual Studio 2022 x64',
                               qtPlatform='windows', guiExecutables='串行、绝对路径'),
              builds=dict(integration='PASS，全量Release', standalone='PASS，9个相关目标',
                          gates='PASS，零命中', verifyTask='PASS，3条机器命令'),
              totals=totals, suites=suites, tours=tours,
              historicalFailures=['red-ui', 'red-same-region-tcp', 'ui-old-wall-grid'],
              driverErrors=['modeling-tour-driver-assertions.log'],
              closedIssues=['OPEN-01', 'OPEN-06'],
              remainingIssues=['OPEN-02', 'OPEN-03', 'OPEN-04', 'OPEN-05', 'OPEN-07', 'OPEN-08'])
(dest / 'verification-summary.json').write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print(json.dumps(dict(totals=totals, tours=tours), ensure_ascii=False, indent=2))
