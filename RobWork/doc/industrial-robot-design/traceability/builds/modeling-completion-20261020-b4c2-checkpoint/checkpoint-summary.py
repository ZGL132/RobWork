"""从非GUI最终XML重算B4c-2检查点；窗口结果不冒充本批最终验收。"""
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
summary = dict(schemaVersion='modeling-b4c2-checkpoint/1', taskId='WP-13-T24',
    baseCommit='1b7ac6013637962936cdddca57b1d1616e981351', completed=False,
    totals={k:sum(r[k] for r in rows) for k in ['total','passed','failed','skipped']},
    suites=rows,
    formalHost=dict(status='FAIL', exitCode=-1073741819,
        reason='策略修订推进后旧草稿正确Stale；尚缺真正重建，旧tour误期待可直接提交；后续空快照解引用崩溃',
        stdout='modeling-tour.log', screenshots='modeling-tour'),
    notRun=['全新上下文独立验收','完整47项真实三维终验'],
    notes=['用户已通知GUI恢复；串行执行三套真实窗口测试，正式宿主失败如实单列，不计入套件绿数',
           '红证按目录区分原产品缺口、API旧路径脚手架及测试前置错误；最终只统计七套件完整轮次，不重复累计',
           '完整工作集首次/修改/移除/重新新增、正式身份回执、固定修订读取、撤销重做及磁盘重开走真实ProjectStore和HostModelCompilePort',
           '增加完整工作集命令token、assembly成功回执方法；旧五命令/v2帧/项目对象格式和依赖边不变',
           '回读异常保留真实Committed修订及未消费草稿，不清控制器脏标记；后续域继续',
           '双模式构建和verify-task结论见实际日志；既有编译警告不称零警告',
           '三GUI套件225通过，正式宿主旧流程FAIL；完整新增工作集正式窗口提交/状态文案及47项三维仍未验齐',
           '旧五命令与多历史单例部件互操作、冲突真正重建已登记B4c-3，尚未由本批闭合',
           '仅本地WIP，不推送不合入；F06/F07未修改/集成；冻结后格式/范围日志随后续只增保存'])
(root/'checkpoint-summary.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
print(summary['totals'])
