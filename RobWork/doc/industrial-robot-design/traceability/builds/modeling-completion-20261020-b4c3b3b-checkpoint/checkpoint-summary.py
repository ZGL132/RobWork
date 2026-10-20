"""逐项核对本批最终XML及ird报告；组件通过不替代正式宿主/全47项三维终验。"""
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
    assert failed == 0 and cases, row
    rows.append(row)
    if name == 'core':
        (root/'core-report.json').write_text(json.dumps(dict(source='core.xml',
            note='gtest_main不消费ird_report；此报告从真实XML派生', **row), ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    else:
        report = json.loads((root/(name+'-report.json')).read_text(encoding='utf-8-sig'))['summary']
        assert report['total'] == row['total'] and report['passed'] == row['passed'] and report['failed'] == failed
        assert sum(report.get(k, 0) for k in ['skipped','notRun','envUnavailable','datasetInvalid']) == skipped
summary = dict(schemaVersion='modeling-b4c3b3b-checkpoint/1', taskId='WP-13-T24',
    baseCommit='4e456addd70519ccf3a349aa92a1005f62eb87c7', completed=False,
    totals={k:sum(r[k] for r in rows) for k in ['total','passed','failed','skipped']}, suites=rows,
    formalHost=dict(status='NOT RUN', reason='正式窗口接线编译通过；现有tour仍绕过draft.apply，下一子批适配真实冲突三选项与手工迁移流程'),
    notRun=['全47项真实三维终验','全新上下文独立验收','正式宿主冲突至重新应用完整流程'],
    notes=['ui_gui新增4项真实冲突窗口；扩展域GUI验证旧稿对照和关闭后定位拒绝',
           'Windows GUI截图已查看；没有把Qt窗口通过当作真实mesh视觉通过',
           'conflict-dialog-red和retained-review-red保留真实产品红例',
           'availability-fixture-failure是旧测试误将新增非命令入口纳入项目命令门控；修正范围并独立断言原契约',
           '正式helper在插件/studio/gui三目标同源，不新增依赖边，冻结证据不回写',
           '仅本地WIP，不推送、不合入、不使用子代理；F06/F07未修改或集成'])
assert summary['totals'] == dict(total=1506, passed=1506, failed=0, skipped=0), summary['totals']
(root/'checkpoint-summary.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
print(summary['totals'])
