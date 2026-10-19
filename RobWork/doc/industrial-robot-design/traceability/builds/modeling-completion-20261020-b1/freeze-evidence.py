"""冻结WP-13-T24/B1真实证据；拒绝覆盖旧审计，逐例报告与XML对账。"""
from pathlib import Path
import hashlib
import json
import shutil
import xml.etree.ElementTree as ET

root=Path.cwd()
source=root/'build/modeling-completion-20261020'
dest=root/'RobWork/doc/industrial-robot-design/traceability/builds/modeling-completion-20261020-b1'
assert not dest.exists(), '证据只增不改'
names=['modeling','modeling_contract','modeling_gui','ui','ui_contract','ui_gui',
       'standalone_modeling','standalone_modeling_contract','standalone_modeling_gui','core']
suites=[]
for name in names:
    xml=ET.parse(source/(name+'.xml')).getroot()
    cases=xml.findall('.//testcase')
    envelope=json.loads((source/(name+'-report.json')).read_text(encoding='utf-8-sig'))
    summary=envelope['summary']
    failed=sum(c.find('failure') is not None for c in cases)
    skipped=[c for c in cases if c.find('skipped') is not None]
    assert len(cases)==int(xml.attrib['tests'])==summary['total'],name
    assert failed==summary['failed']==0,name
    assert len(skipped)==summary['skipped']+summary['envUnavailable'],name
    assert summary['passed']+failed+len(skipped)==len(cases),name
    assert summary['notRun']==summary['datasetInvalid']==0,name
    suites.append(dict(name=name,total=len(cases),passed=summary['passed'],failed=failed,
        skipped=len(skipped),reportOrigin=envelope.get('reportOrigin','native-ird-listener'),
        skippedTests=[c.attrib['classname']+'.'+c.attrib['name'] for c in skipped]))
verify=(source/'verify-task.log').read_text(encoding='utf-8-sig')
assert 'verify-task: PASS WP-13-T24' in verify
totals={key:sum(s[key] for s in suites) for key in ['total','passed','failed','skipped']}
result=dict(schemaVersion='modeling-completion-b1/1',taskId='WP-13-T24',
    baseCommit='84f1cfc7645814ccf01742815129273e783eaedf',
    scope='B1：F02导入报告、F03展开后选链、F05生产Xacro；不构成47项全功能终验',
    environment=dict(os='Windows',toolchain='Visual Studio 2022 x64',qtPlatform='windows',
                     guiExecutables='绝对路径、单进程串行'),
    builds=dict(integration='PASS，全量Release',standalone='PASS，12个相关目标',
                gates='PASS，零命中',verifyTask='PASS，3条机器命令'),
    totals=totals,suites=suites,
    implementationVerified=['F02','F03','F05'],
    remainingImplementation=['F01','F04','F06','F07','F08','F09','F10','F12','F13','F14','F15','F16','F17'],
    pendingReverification=['F11（A-23已实现，建模侧复验待完成）','47项完整用户路径与真实三维'],
    limitations=['core伴随报告由真实gtest XML派生，不冒充原生IRD listener',
        '独立模式不编入集成专属命令流/宿主接线用例，差异在suite计数中保留',
        '三张图为真实生产Qt模态窗口内容；不证明正式宿主端到端文件导入或三维结果',
        '不禁用既有io允许的内部DTD；坏XML、路径逃逸和预算拒绝另有真实测试'])
dest.mkdir(parents=True)
for entry in source.iterdir():
    if entry.is_file() and entry.suffix in ['.log','.xml','.json','.ps1','.py','.md']:
        shutil.copy2(entry,dest/entry.name)
shutil.copytree(source/'gui',dest/'gui')
(dest/'.gitattributes').write_text('* -text\n',encoding='utf-8')
(dest/'verification-summary.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
hashes={p.relative_to(dest).as_posix():hashlib.sha256(p.read_bytes()).hexdigest()
        for p in sorted(dest.rglob('*')) if p.is_file()}
(dest/'sha256.json').write_text(json.dumps(hashes,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps(totals,ensure_ascii=False))
