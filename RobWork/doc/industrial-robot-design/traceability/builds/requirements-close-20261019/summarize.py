"""WP-14-T12：从真实XML和宿主日志生成当轮证据索引，不升级历史终验状态。"""
from pathlib import Path
import json
import xml.etree.ElementTree as ET

root = Path(__file__).resolve().parents[2]
out = Path(__file__).resolve().parent
old = root / 'RobWork/doc/industrial-robot-design/traceability/builds/requirements-completion-20261018/batch25-functional-ledger.json'
ledger = json.loads(old.read_text(encoding='utf-8-sig'))
summary, test_index = {}, {}
for file in sorted(out.glob('*.xml')):
    if file.stem.startswith(('red-', 'ui-concurrent')):
        continue
    tree = ET.parse(file).getroot()
    records = []
    for test in tree.iter('testcase'):
        status = 'FAIL' if test.find('failure') is not None else 'SKIP' if test.find('skipped') is not None or test.get('status') == 'notrun' else 'PASS'
        name = test.get('classname', '') + '.' + test.get('name', '')
        record = {'name': name, 'status': status, 'evidence': file.name}
        records.append(record)
        if not file.stem.startswith('standalone_') and not file.stem.endswith('_probe'):
            test_index[name] = record
    summary[file.stem] = {'total': len(records), **{s: sum(x['status'] == s for x in records) for s in ('PASS', 'FAIL', 'SKIP')}, 'tests': records}

# 只关闭本轮已针对完整行方法实测的项目；其余保留待终验，组件通过另列。
closed = {
    'R02-01': ('requirements_gui.xml', '接近段方向/距离和正距离后的启停，独立字段值及一次局部撤销已实际验证。'),
    'R02-03': ('requirements_gui.xml', '撤离段方向/距离独立、正距离后启停、7笔撤销回基线已实际验证。'),
    'R02-04': ('requirements_gui.xml', '真实顺序键控件造合法链、环、重复及悬空，权威序列检查与撤销已通过。'),
    'R07-08': ('requirements_gui.xml', '独立OID、同批instanceId、只读来源类型及全部生成参数tooltip已核对。'),
    'R07-09': ('requirements_gui.xml', '手改容差后重生成保留原ID/值，批次一次撤销已核对。'),
    'U06': ('requirements_gui.xml', 'm/mm/rad/deg显示切换后SI工作集、dirty及编辑计数均不变。'),
    'U09': ('requirements-tour.log', '真实draft.apply菜单入口，取消保留草稿且HEAD不变，确认产生修订，两次应用及项目撤销真实成功。'),
    'U14': ('requirements-tour.log', '顶/前/右/轴测相机实际状态断言和真实窗口截图。'),
    'U15': ('requirements-tour.log', '放大/缩小/fit/home相机状态及零草稿零修订已核对。'),
    'U16': ('requirements-tour.log', 'W绕转及实际齐次投影矩阵在透视/正交间切换，零草稿零修订。'),
    'U17': ('requirements-tour.log', 'Wire/Outline/Solid/Transparent和8组实际SceneCamera掩码位已核对，零草稿零修订。'),
}
notes = {
    'R01-09': '六分量及旋转参考系真实FK/IK反例现通过；本行完整逐位交互仍单列终验。',
    'R03-01': '区域UI几何编辑有组件证据；任意旋转区域计算和显式区域TCP仍受A-21缺口影响。',
    'R03-02': '默认参考系数量采样有组件证据；任意旋转及区域TCP真实计算仍待A-21。',
    'R03-10': '真实覆盖评估与四态像素显示夹具分别通过；未完成真实算法四态全谱至颜色/图例的一体终验。',
    'R03-11': '真实覆盖清单选择及独立蓝芯150像素已验证；完整状态谱/切换定位仍不能只以夹具关闭。',
    'R06-05': '混合草稿的有效区域纯几何预览通过，不代表只消费有效条目的完整计算预览。',
    'R06-07': '尚无真实正式证据→未应用草稿→应用失效全链证明。',
    'U05': '本轮真实剪贴板6例FAIL；原生OpenClipboard返回ERROR_ACCESS_DENIED(5)，不推断为锁屏。',
    'U08': '四态真实像素已修复；真实算法全谱/图例一致性仍欠一体证据。',
    'U10': '正式宿主仍注入UnloadedPolicySource，修改策略按钮只有延期提示；实际策略编辑未接通。',
    'U12': '有数据1920×1152和1024×790顶部新增可达、滚底及零写入通过；未逐字段确认全部折叠组裁切和跨会话布局。',
    'U18': '8个显示掩码实际切换通过；正式宿主已知碰撞模型的高亮仍未验证。',
    'U22': '帮助部署修复及正式菜单链接在A-22核对；版本/插件清单仍见独立组件证据。',
    'J-01': '对象身份/切片/跨项目隔离缺当前完整逐项终验。',
    'J-02': '真实100点混合可达/不足与必验工况漏验阻断仍待联验，A-21亦影响本项。',
    'J-03': '正式宿主未装配trajectory；段/负载/事件到TRJ/DYN真实供数仍有产品缺口，禁止跨域私有头绕过。',
    'J-04': '选型工具回填→需求引用不变→下游失效的真实接线和证据仍待收口。',
}
items = []
for old_item in ledger['items']:
    item = {k: old_item[k] for k in ('id', 'basis', 'feature', 'method')}
    hits = [test_index[t['name']] for t in old_item.get('latestIntegratedComponentTests', []) if t['name'] in test_index]
    item['currentComponentTests'] = hits
    item['status'] = 'NOT RUN'
    item['observation'] = notes.get(item['id'], '组件实际执行结果列于本行，不把组件匹配数量升级为完整生产交互、三维或联合终验。')
    item['evidence'] = []
    if item['id'] in closed:
        evidence, observation = closed[item['id']]
        item.update(status='PASS', observation=observation, evidence=[evidence])
    if item['id'] == 'U05':
        item.update(status='FAIL', evidence=['requirements_gui.xml', 'desktop-diagnostic.log', 'requirements-tour.log'])
    if item['id'] in ('U10', 'J-03'):
        item['status'] = 'FAIL'
    items.append(item)
assert len(items) == 120 and len({i['id'] for i in items}) == 120
result = {'baselineCommit': '601ffa0ed8b4f7887576dfb9a560acffea86b757', 'conclusion': '未全部修复封闭', 'suiteResults': summary, 'items': items}
(out / 'verification-summary.json').write_text(json.dumps(result, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
lines = ['# 需求模块120项修复封闭账本（2026-10-19）', '', '**未全部封闭。** 本表保留120个原ID/功能/验证方法。PASS只指本行注明的已实测范围；未完成的整行终验为NOT RUN，已确认的产品/环境失败为FAIL。组件证据单独列出，历史记录不覆盖。', '', '|ID|功能|如何验证|本轮终验|证据与限制|', '|---|---|---|---|---|']
for item in items:
    lines.append('|'+ '|'.join(str(item[k]).replace('|','／').replace('\n',' ') for k in ('id','feature','method','status','observation')) +'|')
lines.extend(['', '## 套件实测', '', '|套件|总数|通过|失败|跳过|', '|---|---:|---:|---:|---:|'])
for name, data in summary.items():
    lines.append(f"|{name}|{data['total']}|{data['PASS']}|{data['FAIL']}|{data['SKIP']}|")
lines.extend(['', '详细的逐行匹配测试名、XML证据及状态见 verification-summary.json。正式宿主requirements-tour仍有1项真实剪贴板失败；不能把其余窗口断言通过写成整个tour通过。'])
(out / 'functional-ledger.md').write_text('\n'.join(lines)+'\n', encoding='utf-8')
print(json.dumps({name:{k:v for k,v in data.items() if k!='tests'} for name,data in summary.items()}, ensure_ascii=False, indent=2))
