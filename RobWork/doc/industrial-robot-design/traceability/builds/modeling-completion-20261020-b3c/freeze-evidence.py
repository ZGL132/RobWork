"""WP-13-T24 B3c：核对真实报告后只增冻结证据，不覆盖历史批次。"""
from pathlib import Path
import hashlib,json,re,shutil,xml.etree.ElementTree as ET
root=Path.cwd()
source=Path(__file__).parent
dest=root/'RobWork/doc/industrial-robot-design/traceability/builds/modeling-completion-20261020-b3c'
assert not dest.exists(),'冻结目录只能创建一次'
cases=ET.parse(source/'core.xml').getroot().findall('.//testcase')
assert len(cases)==4 and all(c.find('failure') is None and c.find('skipped') is None for c in cases)
(source/'core-report.json').write_text(json.dumps(dict(schemaVersion='ird-gtest-xml-derived/1',
    reportOrigin='derived-from-gtest-xml',sourceXml='core.xml',
    sourceSha256=hashlib.sha256((source/'core.xml').read_bytes()).hexdigest(),
    note='core采用gtest_main，伴随JSON由真实XML派生，不冒称原生IRD报告。',
    summary=dict(total=4,passed=4,failed=0,skipped=0,notRun=0,envUnavailable=0,datasetInvalid=0)),ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
suites=[]
for name in ['modeling','modeling_contract','modeling_gui','ui','ui_contract','ui_gui',
             'standalone_modeling','standalone_modeling_contract','standalone_modeling_gui','core']:
    cases=ET.parse(source/(name+'.xml')).getroot().findall('.//testcase')
    summary=json.loads((source/(name+'-report.json')).read_text(encoding='utf-8-sig'))['summary']
    failed=sum(c.find('failure') is not None for c in cases)
    skipped=sum(c.find('skipped') is not None for c in cases)
    assert summary['total']==len(cases)>0,name
    assert summary['passed']==len(cases)-skipped,name
    assert summary['failed']==failed==0,name
    assert summary['skipped']+summary['envUnavailable']==skipped,name
    for case in cases:
        if case.find('skipped') is not None:
            assert name in ['modeling_gui','standalone_modeling_gui'],name
            assert case.attrib['classname']+'.'+case.attrib['name']=='ModelingPanelGuiTest.PreviewPane_MultiTypeChain_UI_T59'
    assert summary['notRun']==summary['datasetInvalid']==0,name
    suites.append(dict(name=name,total=len(cases),passed=len(cases)-skipped,failed=0,skipped=skipped))
totals={key:sum(s[key] for s in suites) for key in ['total','passed','failed','skipped']}
assert totals==dict(total=1364,passed=1364,failed=0,skipped=0),totals
assert 'verify-task: PASS WP-13-T24' in (source/'verify-task.log').read_text(encoding='utf-8-sig')
tour=(source/'modeling-tour.log').read_text(encoding='utf-8-sig')
tour_passes=len(re.findall(r'\[ird-ui-smoke-mtour\] pass ',tour))
assert tour_passes>=274 and '[ird-ui-smoke-mtour] FAIL' not in tour
assert 'pass b4a shared-selection-and-tree-agree' in tour
result=dict(schemaVersion='modeling-completion-b3c/1',taskId='WP-13-T24',baseCommit='d4b27df07aa0',
    scope='F09完整段元估算录入与局部历史实现验证；整体47项及完整三维终验仍待B5',
    totals=totals,suites=suites,
    tour=dict(passed=tour_passes,failed=0,scope='正式宿主综合流程，含其他域，不等同建模功能数量'),
    environment=dict(os='Windows',toolchain='VS2022 x64',qtPlatform='windows',guiExecution='绝对路径、单exe串行'),
    builds=dict(integration='PASS，全量Release',standalone='PASS，12目标',gates='PASS，零命中',verifyTask='PASS，三条机器命令'),
    limitations=['未执行全新上下文独立验收','core伴随报告派生自真实XML',
        '剪贴板预览本轮集成/独立均通过；不覆盖B4a历史环境跳过记录',
        '保留法兰估算入口及错误提示红证，修复后重跑验证；定向轮次不重复累计',
        '独立构建测试默认数学构造与模块集成边界链接失败日志保留；两项完整模块用例仅集成编入',
        '截图及共享选中不能替代B5全部真实mesh、安装姿态、TCP、导航和碰撞验收',
        '未推送、未自行合入主线'])
dest.mkdir(parents=True)
for p in source.iterdir():
    if p.is_file() and p.suffix in ['.log','.xml','.json','.ps1','.py','.md']:
        shutil.copy2(p,dest/p.name)
for name in ['gui','modeling-tour']:
    if (source/name).exists():
        (dest/name).mkdir()
        for p in (source/name).glob('*.png'):
            shutil.copy2(p,dest/name/p.name)
(dest/'.gitattributes').write_text('* -text\n',encoding='utf-8')
(dest/'verification-summary.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
hashes={p.relative_to(dest).as_posix():hashlib.sha256(p.read_bytes()).hexdigest()
        for p in sorted(dest.rglob('*')) if p.is_file()}
(dest/'sha256.json').write_text(json.dumps(hashes,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps(dict(totals=totals,tourPassed=tour_passes),ensure_ascii=False))
