"""冻结A-23真实执行证据；XML/逐例报告对账，拒绝覆盖历史目录。"""
from pathlib import Path
import hashlib
import json
import re
import shutil
import xml.etree.ElementTree as ET

root = Path.cwd()
source = root / 'build/requirements-a23-20261020'
dest = root / 'RobWork/doc/industrial-robot-design/traceability/builds/requirements-a23-20261020'
assert source.is_dir()
assert not dest.exists(), '禁止覆盖冻结证据'
names = ['modeling', 'modeling_contract', 'modeling_gui', 'ui', 'ui_contract', 'ui_gui',
         'requirements', 'requirements_contract', 'requirements_gui',
         'kinematics', 'kinematics_contract', 'kinematics_gui']
names += ['standalone_' + n for n in names]
names += ['policy', 'project', 'core']
suites = []
for name in names:
    xml = ET.parse(source / (name + '.xml')).getroot()
    cases = xml.findall('.//testcase')
    failed = sum(c.find('failure') is not None for c in cases)
    skipped = [c for c in cases if c.find('skipped') is not None]
    envelope = json.loads((source / (name + '-report.json')).read_text(encoding='utf-8-sig'))
    report = envelope['summary']
    assert len(cases) == int(xml.attrib['tests']) == report['total'], name
    assert failed == report['failed'] == 0, name
    assert len(skipped) == report['skipped'] + report['envUnavailable'], name
    assert report['passed'] + failed + len(skipped) == len(cases), name
    assert report['notRun'] == report['datasetInvalid'] == 0, name
    suites.append(dict(name=name, reportOrigin=envelope.get('reportOrigin', 'native-ird-listener'),
                       total=len(cases), passed=report['passed'], failed=failed,
                       skipped=len(skipped), envUnavailable=report['envUnavailable'],
                       skippedTests=[dict(name=c.attrib['classname'] + '.' + c.attrib['name'],
                                          reason=c.find('skipped').attrib.get('message', '')) for c in skipped]))
tours = []
for name in ['modeling-tour', 'requirements-tour']:
    log = (source / (name + '.log')).read_text(encoding='utf-8-sig')
    passed = len(re.findall(r'\] pass ', log))
    failed = len(re.findall(r'\] FAIL', log))
    assert failed == 0 and passed > 200, name
    tours.append(dict(name=name, passedAssertions=passed, failedAssertions=failed,
                      screenshots=len(list((source / name).glob('*.png'))), exitCode=0))
assert 'verify-task: PASS WP-14-T12' in (source / 'verify-task.log').read_text(encoding='utf-8-sig')
totals = {key: sum(s[key] for s in suites) for key in ['total', 'passed', 'failed', 'skipped', 'envUnavailable']}
dest.mkdir(parents=True)
for entry in source.iterdir():
    if entry.is_file() and entry.suffix in ['.log', '.xml', '.json', '.ps1', '.py', '.png', '.md']:
        shutil.copy2(entry, dest / entry.name)
for name in ['modeling-tour', 'requirements-tour']:
    (dest / name).mkdir()
    for entry in (source / name).glob('*.png'):
        shutil.copy2(entry, dest / name / entry.name)
result = dict(schemaVersion='requirements-a23-verification/1', taskId='WP-14-T12',
              baseCommit='b3ae9582e679b6dbbde80bb526e75e2384b1f0ff',
              scope='A-23工程策略闭环；不构成原120项全功能终验',
              environment=dict(os='Windows', toolchain='Visual Studio 2022 x64', qtPlatform='windows',
                               guiExecutables='绝对路径、串行'),
              builds=dict(integration='PASS，全量Release', standalone='PASS，12个相关目标',
                          gates='PASS，零命中', verifyTask='PASS，3条机器命令'),
              totals=totals, suites=suites, tours=tours,
              closedInThisBatch=['OPEN-02'],
              remainingIssues=['OPEN-03', 'OPEN-04', 'OPEN-05', 'OPEN-07', 'OPEN-08'],
              limitations=['无真实碰撞设施时只给数据不足，不代表完成真实碰撞检测',
                           '独立UI不含集成专属HostEngineeringPolicy/KinEvaluationChannel用例',
                           '只读/分支/跨项目隔离由真实ProjectStore契约覆盖，未以此代替原120项终验'])
(dest / 'verification-summary.json').write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
hashes = {str(p.relative_to(dest)).replace('\\', '/'): hashlib.sha256(p.read_bytes()).hexdigest()
          for p in sorted(dest.rglob('*')) if p.is_file()}
(dest / 'sha256.json').write_text(json.dumps(hashes, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print(json.dumps(dict(totals=totals, tours=tours), ensure_ascii=False, indent=2))
