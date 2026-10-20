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
summary = dict(schemaVersion='modeling-b4c3a-checkpoint/1', taskId='WP-13-T24',
    baseCommit='6cab3ff47cdd8510a9f9c97b3bfeb3a2edb2835e', completed=False,
    totals={k:sum(r[k] for r in rows) for k in ['total','passed','failed','skipped']}, suites=rows,
    notRun=['本批正式modeling-tour未重跑：上批已证实的真正冲突重建及空快照保护仍未修复',
            '完整47项真实三维终验', '全新上下文独立验收'],
    notes=['新增4项建模历史互操作用例；真实UI存储链扩展旧五token正向/恢复及逐命令undo-redo/磁盘重开，含正式快照身份对账',
           'core本批执行全部65项，含4项BuildRedLine；不得把相对上批增加的61项说成新增测试',
           '旧named-poses Restore漏双编译的实际失败见runtime-restore-red和runtime-plan-red；修复后纯位姿恢复仍无双编译',
           'GUI使用VS2022 x64/windows/绝对路径，串行225项；完整恢复工具及物性底部截图已查看',
           'named-pose-test-setup-failure、legacy-chain-test-setup-build-failure及undo-sequence-test-contract-mismatch为测试前置/契约错误，不冒充产品红证',
           '既有上批正式宿主FAIL见../modeling-completion-20261020-b4c2-checkpoint，绿套件不抵消未闭合问题',
           '仅本地WIP、不推送不合入；未使用子代理，F06/F07未修改/集成'])
assert summary['totals'] == dict(total=1495, passed=1495, failed=0, skipped=0), summary['totals']
(root/'checkpoint-summary.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
print(summary['totals'])
