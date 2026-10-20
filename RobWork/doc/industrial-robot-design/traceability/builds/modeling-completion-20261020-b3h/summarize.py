"""B3h从最终真实XML计数，核对原生报告；不把红绿重复轮次算成覆盖数量。"""
import json
import re
import xml.etree.ElementTree as ET
from pathlib import Path

root = Path(__file__).resolve().parent
names = ['modeling', 'modeling_contract', 'modeling_gui', 'ui', 'ui_contract',
         'ui_gui', 'standalone_modeling', 'standalone_modeling_contract',
         'standalone_modeling_gui', 'core']
suites = []
skips = []
for name in names:
    xml = ET.parse(root / (name + '.xml')).getroot()
    cases = list(xml.iter('testcase'))
    failed = sum(c.find('failure') is not None or c.find('error') is not None for c in cases)
    skipped = sum(c.find('skipped') is not None for c in cases)
    assert int(xml.attrib['tests']) == len(cases)
    row = dict(name=name, total=len(cases), passed=len(cases)-failed-skipped,
               failed=failed, skipped=skipped)
    assert failed == 0, row
    suites.append(row)
    for c in cases:
        s = c.find('skipped')
        if s is not None:
            skips.append(dict(suite=name, test=c.attrib['classname']+'.'+c.attrib['name'],
                              reason=s.attrib.get('message', s.text or '')))
    if name == 'core':
        report = dict(source='core.xml', note='GTest::gtest_main不消费ird_report；仅派生自真实XML', **row)
        (root/'core-report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    else:
        report = json.loads((root/(name+'-report.json')).read_text(encoding='utf-8-sig'))['summary']
        assert report['total'] == row['total'] and report['passed'] == row['passed'] and report['failed'] == failed
        assert sum(report.get(k, 0) for k in ['skipped','notRun','envUnavailable','datasetInvalid']) == skipped

tour = (root/'modeling-tour.log').read_text(encoding='utf-8-sig')
summary = dict(schemaVersion='modeling-completion-b3h/1', taskId='WP-13-T24', baseCommit='2b653e924275',
    scope='工具完整草稿管理；项目应用/磁盘重开/真实mesh与47项仍开放',
    totals={k:sum(s[k] for s in suites) for k in ['total','passed','failed','skipped']},
    suites=suites, skips=skips,
    tour=dict(passed=len(re.findall(r'^\[ird-ui-smoke-mtour\] pass ', tour, re.M)),
              failed=len(re.findall(r'^\[ird-ui-smoke-mtour\] FAIL ', tour, re.M)),
              scope='既有正式宿主综合流程，含其他域；未新增工具三维终验'),
    environment=dict(toolchain='VS2022 x64', qtPlatform='windows', guiExecution='串行、绝对exe'),
    limitations=['core伴随报告由真实XML派生','独立模式未编入真实模块装配用例',
                 'scroll-red是测试程序化滚动不足；已改滚动条到底及真实鼠标可达性检查',
                 'compile-field-error是测试字段误写，非产品红证',
                 '工具/场景完整工作集应用与磁盘恢复发现根对象单槽缺口，后续B4修复',
                 'F06归另一工作树，未实施/集成','无全新上下文独立验收',
                 '真实mesh/碰撞/完整47项仍开放','未推送、未自行合入'])
assert summary['tour']['failed'] == 0 and summary['tour']['passed'] > 0
(root/'verification-summary.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
print(summary['totals'])
print(summary['tour'])
