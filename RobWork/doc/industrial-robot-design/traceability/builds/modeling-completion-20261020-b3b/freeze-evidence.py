"""冻结B3b证据；真实XML与IRD逐项对账，不覆盖历史批次。"""
from pathlib import Path
import hashlib,json,re,shutil,xml.etree.ElementTree as ET
root=Path.cwd()
source=Path(__file__).parent
dest=root/'RobWork/doc/industrial-robot-design/traceability/builds/modeling-completion-20261020-b3b'
assert not dest.exists(),'只增不改'
cases=ET.parse(source/'core.xml').getroot().findall('.//testcase')
assert len(cases)==4 and all(c.find('failure') is None and c.find('skipped') is None for c in cases)
(source/'core-report.json').write_text(json.dumps(dict(schemaVersion='ird-gtest-xml-derived/1',
    reportOrigin='derived-from-gtest-xml',sourceXml='core.xml',
    sourceSha256=hashlib.sha256((source/'core.xml').read_bytes()).hexdigest(),
    note='core沿用gtest_main，伴随报告由真实XML派生，不恢复原生IRD追溯信息。',
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
    allowed_skips={'modeling_gui':'ModelingPanelGuiTest.PreviewPane_MultiTypeChain_UI_T59',
        'standalone_modeling_gui':'ModelingPanelGuiTest.PreviewPane_MultiTypeChain_UI_T59',
        'ui_gui':'ParamTablePanelGuiTest.BatchPasteImpactDetail_UI_T08_ACC1'}
    for c in cases:
        if c.find('skipped') is not None:
            assert c.attrib['classname']+'.'+c.attrib['name']==allowed_skips[name],name
    assert summary['notRun']==summary['datasetInvalid']==0,name
    suites.append(dict(name=name,total=len(cases),passed=len(cases)-skipped,failed=0,skipped=skipped))
totals={k:sum(s[k] for s in suites) for k in ['total','passed','failed','skipped']}
assert totals==dict(total=1347,passed=1344,failed=0,skipped=3)
assert 'verify-task: PASS WP-13-T24' in (source/'verify-task.log').read_text(encoding='utf-8-sig')
tour=(source/'modeling-tour.log').read_text(encoding='utf-8-sig')
tour_passes=len(re.findall(r'\[ird-ui-smoke-mtour\] pass ',tour))
assert tour_passes==274 and '[ird-ui-smoke-mtour] FAIL' not in tour
result=dict(schemaVersion='modeling-completion-b3b/1',taskId='WP-13-T24',baseCommit='051f8202',
    scope='F04工程工作范围确认实现验证；正式项目重开/三维跨周全路径仍待B5',totals=totals,suites=suites,
    tour=dict(passed=tour_passes,failed=0,scope='既有综合回归，含其他域流程，不等同建模功能数'),
    environment=dict(os='Windows',toolchain='VS2022 x64',qtPlatform='windows',guiExecution='绝对路径、单exe串行'),
    builds=dict(integration='PASS，全量Release',standalone='PASS，12目标',gates='PASS，零命中',verifyTask='PASS，3条机器命令'),
    limitations=['未执行全新上下文独立验收','core伴随JSON派生自真实XML','3项真实剪贴板验证因外部占用环境跳过，不能计为通过',
        '窗口截图及编译输入不能替代完整正式项目/运行时三维跨周验收','整体47项全功能终验未完成'])
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
