"""从本批最终XML及ird报告重算结果；不把窗口套件绿数外推为正式宿主终验。"""
import json
import xml.etree.ElementTree as ET
from pathlib import Path

root = Path(__file__).resolve().parent
names = ['modeling', 'modeling_contract', 'ui', 'ui_contract', 'core',
         'standalone_modeling', 'standalone_modeling_contract',
         'modeling_gui', 'ui_gui', 'standalone_modeling_gui']
rows = []
for name in names:
    cases = list(ET.parse(root / (name + '.xml')).getroot().iter('testcase'))
    failed = sum(c.find('failure') is not None or c.find('error') is not None for c in cases)
    skipped = sum(c.find('skipped') is not None for c in cases)
    row = dict(name=name, total=len(cases), passed=len(cases)-failed-skipped,
               failed=failed, skipped=skipped)
    assert failed == 0, row
    rows.append(row)
    if name == 'core':
        (root/'core-report.json').write_text(json.dumps(dict(source='core.xml',
            note='gtest_main不消费ird_report；本文件仅派生自真实XML', **row), ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    else:
        report = json.loads((root/(name+'-report.json')).read_text(encoding='utf-8-sig'))['summary']
        assert report['total'] == row['total'] and report['passed'] == row['passed'] and report['failed'] == failed
        assert sum(report.get(k, 0) for k in ['skipped','notRun','envUnavailable','datasetInvalid']) == skipped
summary = dict(schemaVersion='modeling-b4c3b2-checkpoint/1', taskId='WP-13-T24',
    baseCommit='00d50d3552ee', completed=False,
    totals={k:sum(r[k] for r in rows) for k in ['total','passed','failed','skipped']}, suites=rows,
    formalHost=dict(status='FAIL', actualExitCode=1, snapshotPrerequisiteGuard='PASS',
        reason='原tour期待策略修订后旧草稿直接应用；真实Stale仍拒绝，窗口/落盘/迁移未闭环'),
    notRun=['全47项真实三维终验','全新上下文独立验收'],
    notes=['既有真实存储链新增Stale详情和反例断言，未增加测试项数，不把重复1499项说成新增',
           'stale-detail-red真实复现空详情；生产reader及onResult已接线，完整suite绿不代表宿主闭环',
           'Windows GUI三套226通过；正式宿主另列FAIL，正常退出1而非旧访问冲突，截图已查看',
           'manifest-anchor-red为现行装配清单行号漂移；只同步锚点，完整契约60项重跑通过',
           '仅本地WIP，不推送、不合入、不使用子代理；F06/F07未修改/集成'])
assert summary['totals'] == dict(total=1499, passed=1499, failed=0, skipped=0), summary['totals']
(root/'checkpoint-summary.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
print(summary['totals'])


