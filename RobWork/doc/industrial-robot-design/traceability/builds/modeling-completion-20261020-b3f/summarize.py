"""WP-13-T24 B3f：只聚合真实执行XML；红绿和重复轮次不重复累计。"""
import json
import re
import xml.etree.ElementTree as ET
from pathlib import Path

root = Path(__file__).resolve().parent
names = ['modeling', 'modeling_contract', 'modeling_gui', 'ui', 'ui_contract',
         'ui_gui', 'standalone_modeling', 'standalone_modeling_contract',
         'standalone_modeling_gui', 'core']
suites = []
for name in names:
    xml = ET.parse(root / (name + '.xml')).getroot()
    cases = list(xml.iter('testcase'))
    failed = sum(c.find('failure') is not None or c.find('error') is not None for c in cases)
    skipped = sum(c.find('skipped') is not None for c in cases)
    assert int(xml.attrib['tests']) == len(cases)
    row = dict(name=name, total=len(cases), passed=len(cases)-failed-skipped,
               failed=failed, skipped=skipped)
    suites.append(row)
    if name == 'core':
        data = dict(source='core.xml', note='gtest_main不消费--ird_report；本伴随报告仅派生自真实XML', **row)
        (root/'core-report.json').write_text(json.dumps(data, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
tour = (root/'modeling-tour.log').read_text(encoding='utf-8-sig')
summary = dict(schemaVersion='modeling-completion-b3f/1', taskId='WP-13-T24',
    baseCommit='c43fc49e069821cbef1e16714cdb7783bed4e0a9',
    scope='F12场景管理草稿入口；固定帧编译/真实几何碰撞/47项全功能仍未封闭',
    totals={k:sum(s[k] for s in suites) for k in ['total','passed','failed','skipped']},
    suites=suites,
    tour=dict(passed=len(re.findall(r'^\[ird-ui-smoke-mtour\] pass ',tour,re.M)), failed=len(re.findall(r'^\[ird-ui-smoke-mtour\] FAIL ',tour,re.M)),
              scope='正式宿主既有综合tour，含其他域，不等于本批场景三维验收'),
    environment=dict(os='Windows',toolchain='VS2022 x64',qtPlatform='windows',guiExecution='绝对路径、GUI单exe串行'),
    limitations=['无全新上下文独立验收','F06由wp13-t25负责，本批不实施/集成',
                 '无几何固定帧正式编译尚拒绝，F12保持开放','真实mesh挂接和碰撞仍未完成',
                 'core伴随JSON明确派生自真实XML','未推送、未自行合入'])
(root/'verification-summary.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps(summary['totals'],ensure_ascii=False))
print(json.dumps(summary['tour'],ensure_ascii=False))
