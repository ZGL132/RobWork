"""冻结B2b证据，逐例核对XML和IRD；原审计与旧批次不变。"""
from pathlib import Path
import hashlib,json,re,shutil,xml.etree.ElementTree as ET
root=Path.cwd()
source=root/'build/modeling-completion-20261020-b2b'
dest=root/'RobWork/doc/industrial-robot-design/traceability/builds/modeling-completion-20261020-b2b'
assert not dest.exists(),'只增不改'
suites=[]
for name in ['modeling','modeling_contract','modeling_gui','ui','ui_contract','ui_gui',
             'standalone_modeling','standalone_modeling_contract','standalone_modeling_gui','core']:
    cases=ET.parse(source/(name+'.xml')).getroot().findall('.//testcase')
    report=json.loads((source/(name+'-report.json')).read_text(encoding='utf-8-sig'))
    summary=report['summary']
    failed=sum(c.find('failure') is not None for c in cases)
    skipped=sum(c.find('skipped') is not None for c in cases)
    assert summary['total']==len(cases),name
    assert summary['failed']==failed==0,name
    assert summary['skipped']+summary['envUnavailable']==skipped==0,name
    assert summary['passed']==len(cases),name
    assert summary['notRun']==summary['datasetInvalid']==0,name
    suites.append(dict(name=name,total=len(cases),passed=summary['passed'],failed=failed,skipped=skipped,
        reportOrigin=report.get('reportOrigin','native-ird-listener')))
assert 'verify-task: PASS WP-13-T24' in (source/'verify-task.log').read_text(encoding='utf-8-sig')
tour=(source/'modeling-tour.log').read_text(encoding='utf-8-sig')
tour_passes=len(re.findall(r'\[ird-ui-smoke-mtour\] pass ',tour))
assert tour_passes==274 and '[ird-ui-smoke-mtour] FAIL' not in tour
totals={k:sum(s[k] for s in suites) for k in ['total','passed','failed','skipped']}
assert totals==dict(total=1329,passed=1329,failed=0,skipped=0)
result=dict(schemaVersion='modeling-completion-b2b/1',taskId='WP-13-T24',baseCommit='a77feaee92a0903ac77ad4cba692f058208c8b16',
    scope='F13恢复链实现验证；Home/Zero正式窗口全路径及47项终验仍未完成',
    environment=dict(os='Windows',toolchain='VS2022 x64',qtPlatform='windows',guiExecution='绝对路径、单exe串行'),
    totals=totals,suites=suites,tour=dict(passed=274,failed=0,scope='含既有其他域流程，不能等同建模功能数'),
    builds=dict(integration='PASS，全量Release',standalone='PASS，12目标',gates='PASS，零命中',verifyTask='PASS，3条机器命令'),
    implementationVerified=['F02','F03','F05','F17','F13恢复链'],
    remainingImplementation=['F01','F04','F06','F07','F08','F09','F10','F12','F14','F15','F16'],
    pendingReverification=['F11工程策略（A-23已实现）','F13/HomeZero正式窗口全路径','完整47项与真实三维矩阵'],
    limitations=['本批没有执行全新上下文独立验收','core伴随报告由真实gtest XML派生',
        '代理链截图不证明全部真实mesh、安装姿态、TCP和显示模式',
        '建模控件须滚动可达，完整窗口布局和操作矩阵仍留B5'])
dest.mkdir(parents=True)
for p in source.iterdir():
    if p.is_file() and p.suffix in ['.log','.xml','.json','.ps1','.py','.md']:
        shutil.copy2(p,dest/p.name)
for name in ['gui','modeling-tour']:
    if (source/name).exists():
        (dest/name).mkdir()
        for p in (source/name).glob('*.png'):shutil.copy2(p,dest/name/p.name)
(dest/'.gitattributes').write_text('* -text\n',encoding='utf-8')
(dest/'verification-summary.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
hashes={p.relative_to(dest).as_posix():hashlib.sha256(p.read_bytes()).hexdigest()
        for p in sorted(dest.rglob('*')) if p.is_file()}
(dest/'sha256.json').write_text(json.dumps(hashes,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(totals)
