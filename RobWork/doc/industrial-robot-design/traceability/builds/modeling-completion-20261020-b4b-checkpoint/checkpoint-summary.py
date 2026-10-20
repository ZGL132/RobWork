"""从非GUI最终XML重算B4b检查点；窗口结果不冒充本批最终验收。"""
import json
import xml.etree.ElementTree as ET
from pathlib import Path
root = Path(__file__).resolve().parent
names = ['modeling', 'modeling_contract', 'ui', 'ui_contract', 'core',
         'standalone_modeling', 'standalone_modeling_contract']
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
summary = dict(schemaVersion='modeling-b4b-checkpoint/1', taskId='WP-13-T24',
    baseCommit='e2615b825d76da8168cc8c5186ae768cb7b2f9f3', completed=False,
    totals={k:sum(r[k] for r in rows) for k in ['total','passed','failed','skipped']},
    suites=rows,
    notRun=['最终集成modeling_gui复验','最终集成ui_gui复验','最终独立modeling_gui复验','正式宿主modeling-tour'],
    invalid=['modeling-gui-concurrent-invalid.*','ui-gui-concurrent-invalid.*'],
    notes=['用户要求桌面GUI待另一工作树结束并通知后再执行，期间转其他非GUI工作',
           '较早GUI专项已通过并查看截图，但不代替最新完整窗口复验',
           '早期失败和通过日志完整保留；只统计最终非GUI轮次，不重复累计',
           '双模式构建、verify-task三条已通过，冻结后的索引与文档校验随下一批只增记录',
           '本地WIP检查点，不推送、不合入、不标B4b或47项全部完成',
           'F06由另一智能体负责，未修改/集成'])
(root/'checkpoint-summary.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
print(summary['totals'])
