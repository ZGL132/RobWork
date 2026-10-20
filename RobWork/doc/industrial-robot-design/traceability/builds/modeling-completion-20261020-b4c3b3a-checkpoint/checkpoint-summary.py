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
summary = dict(schemaVersion='modeling-b4c3b3a-checkpoint/1', taskId='WP-13-T24',
    baseCommit='bc4cda806a1b', completed=False,
    totals={k:sum(r[k] for r in rows) for k in ['total','passed','failed','skipped']}, suites=rows,
    formalHost=dict(status='NOT RUN', reason='正式冲突窗口/迁移仍未实施；上一批真实tour的Stale失败保持未封闭，本批不重复已知失败'),
    notRun=['全47项真实三维终验','全新上下文独立验收','正式冲突窗口及可见迁移闭环'],
    notes=['相较上批1499项增加3项：GUI编辑通知、控制器冲突保留、真实建模磁盘重开',
           'Windows GUI三套227通过，截图已查看；结构只读缺口真实红后修复，不声称三维已通过',
           'retained-save-red/draft-change-red/factory-branch-red/readonly-structure-red保存真实产品红例',
           'contract-fixture-failure因重复同模板是同值无通知，修正为从空域创建，不冒充产品红证',
           '真实ProjectStore拒绝来源由既有HostCompilePipeline覆盖；新链消费Stale值并验证后台保存和工厂磁盘重开',
           '仅本地WIP，不推送、不合入、不使用子代理；F06/F07未修改或集成'])
assert summary['totals'] == dict(total=1502, passed=1502, failed=0, skipped=0), summary['totals']
(root/'checkpoint-summary.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
print(summary['totals'])
