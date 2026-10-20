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
summary = dict(schemaVersion='modeling-b4c3b1-checkpoint/1', taskId='WP-13-T24',
    baseCommit='f958648b90a2cfd2e3c5ae1f3ef981f758106868', completed=False,
    totals={k:sum(r[k] for r in rows) for k in ['total','passed','failed','skipped']}, suites=rows,
    notRun=['本批正式modeling-tour未重跑：模块完整基线重建已实现，但生产reader绑定、冲突落盘/窗口/迁移及tour空快照保护仍未完成',
            '完整47项真实三维终验', '全新上下文独立验收'],
    notes=['新增3项UI模型测试和1项真实窗口测试；真实存储链扩展固定旧修订完整读取，HEAD已前进时仍保留旧完整草稿原payload/base且项目零修订',
           'core本批执行全部65项，含4项BuildRedLine；不把重复执行的既有用例说成新增测试',
           'reanchor-red证明原实现无reader仍直接换锚；修复后无reader/读错等原子拒绝，明确无根为合法空态',
           'GUI使用VS2022 x64/windows/绝对路径，串行226项；conflict-rebuilt-current-tool.png已经人工查看，证明模型树刷新及旧未确认表单取消',
           'reader-fixture-invalid-old-root、gui-fixture-build-missing-private-symbols、reader-build-missing-include、gui-tool-selection-test-assumption是测试前置或实现编译错误，不冒充产品红证',
           'gui-old-executable-no-match两轮零匹配不计通过；本地runner增强零匹配拒绝。standalone-overlap-abort为真实跨进程临时目录冲突，PackageTest改唯一目录后双模式专项各7通过，不重复累计',
           '既有上批正式宿主FAIL见../modeling-completion-20261020-b4c2-checkpoint，绿套件不抵消未闭合问题',
           '仅本地WIP、不推送不合入；未使用子代理，F06/F07未修改/集成'])
assert summary['totals'] == dict(total=1499, passed=1499, failed=0, skipped=0), summary['totals']
(root/'checkpoint-summary.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
print(summary['totals'])

