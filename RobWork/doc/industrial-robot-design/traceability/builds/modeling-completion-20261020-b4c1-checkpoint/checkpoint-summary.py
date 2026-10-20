"""从非GUI最终XML重算B4c-1检查点；窗口结果不冒充本批最终验收。"""
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
summary = dict(schemaVersion='modeling-b4c1-checkpoint/1', taskId='WP-13-T24',
    baseCommit='af405e7bb4c34f21ea675efef0314add9304849e', completed=False,
    totals={k:sum(r[k] for r in rows) for k in ['total','passed','failed','skipped']},
    suites=rows,
    notRun=['最终集成modeling_gui复验','最终集成ui_gui复验','最终独立modeling_gui复验','正式宿主modeling-tour'],
    notes=['用户要求GUI等待另一工作树结束并通知，本批未启动任何GUI/正式宿主',
           '红证2项模块失败、3项应用编排失败来自未修复产品；最终仅统计七套件完整非GUI轮次，不重复累计',
           '真实ProjectStore受理旧expectedRevision后返回StaleRevision，草稿/分支tip原样保留；跨分支拒绝隔离继续后域',
           '无新增公共形状、命令token、存储或依赖边；仅追加私有Outcome准备失败态并修正状态计数',
           '双模式构建及verify-task三条通过；构建日志含既有编译警告，不称零警告',
           '最终GUI/局部历史窗口/状态文案视觉NOT RUN；未进行全新上下文独立验收',
           '仅本地WIP，不推送不合入；完整工作集apply/回执/保留对象/冲突重建和47项终验仍开放',
           'F06/F07未修改/集成；冻结后的索引/文档/格式/范围检查随下一批只增记录'])
(root/'checkpoint-summary.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
print(summary['totals'])
