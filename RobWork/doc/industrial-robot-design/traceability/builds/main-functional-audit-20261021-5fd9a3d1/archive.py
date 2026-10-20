"""封存本轮证据；不复制构建二进制，不改历史证据或findings原有字节。"""
from pathlib import Path
import hashlib
import json
import shutil

src=Path(__file__).resolve().parent
root=src.parents[1]
doc=root/'RobWork/doc/industrial-robot-design'
target=doc/'traceability/builds/main-functional-audit-20261021-5fd9a3d1'
if target.exists():
    raise RuntimeError('证据目标已存在，禁止覆盖封存历史')
target.mkdir()
for path in src.iterdir():
    if path.is_file() and path.suffix in ('.md','.json','.xml','.log','.ps1','.py'):
        if path.name not in ('test-summary-progress.json','report-generation.log'):
            shutil.copy2(path,target/path.name)
for name in ('requirements-tour','modeling-tour','layout','contact-sheets','gui-modeling_gui',
             'gui-standalone_modeling_gui','gui-ui_gui','optimization-repro','visual-repro'):
    if (src/name).exists():
        shutil.copytree(src/name,target/name)
(target/'README.md').write_text('''# 合入后主线需求/建模复验入口

产品冻结基线：redesign-main@5fd9a3d192ea1f67484bed7aec089ec10719c51a。

结论：**不通过**。入口[REPORT.md](REPORT.md)，逐功能见[需求120项](REQUIREMENTS-MATRIX.md)、[建模47项](MODELING-MATRIX.md)。

本轮双模式重新构建，41套件5401条：5393通过、1失败、7跳过；完整JSON见verification-summary.json。重复专项不累计。
正式宿主235＋361＋67断言、auto 8步通过，主流程71张PNG；视觉复跑31张原图仍出现滚动到底空白，测试却通过。完整问题与未执行边界见报告，不以这些计数替代全功能验收。

run.ps1/batch.ps1是本轮实际入口；硬编码输出仍指向原workspace的build/main-functional-audit-20261021。再次运行应先更换输出目录，避免覆盖已完成的工作现场。独立树与可执行文件未入证据库，二进制摘要已记录。

41份主XML和36份原生ird报告如实保存；缺失的5份不伪造，原因/目标见MA-09。9页联系表仅用于审阅索引，原PNG保留。

SHA256SUMS采用文本换行规范化LF后字节哈希（.md/.json/.xml/.log/.ps1/.py/.txt），PNG/JPG按原始字节；自身不入哈希。日志含失败与跳过，不清理成全绿。findings中的本轮新增条目在证据外登记；文档验证/索引命令日志保留在原build目录，避免自引用改写已封存证据。
''',encoding='utf-8')
records=[]
text_ext={'.md','.json','.xml','.log','.ps1','.py','.txt'}
for path in sorted(target.rglob('*')):
    if not path.is_file():
        continue
    data=path.read_bytes()
    if path.suffix in text_ext and b'\0' not in data:
        data=data.replace(b'\r\n',b'\n').replace(b'\r',b'\n')
    records.append(hashlib.sha256(data).hexdigest()+'  '+path.relative_to(target).as_posix())
(target/'SHA256SUMS').write_text('\n'.join(records)+'\n',encoding='utf-8')

finding_path=doc/'traceability/findings.json'
original=finding_path.read_bytes()
existing=json.loads(original.decode('utf-8-sig'))
new=[
 ('F-949','minor','优化采用契约旧断言把修订中保留RobotDesign引用等同对象复制：主线完整回归52条中该用例失败，独立单跑稳定复现；与A-13新修订保留完整基线OID/CV语义冲突，需修正断言及单元卡并验证无新增设计对象，不能回退完整引用集修复','optimization/project契约所有者；MA-05'),
 ('F-950','minor','正式需求tour两种窗口尺寸滚动到底均取得近乎全空白Dock截图，独立复跑稳定重现但235项断言仍全绿：step13只检查滚动条value==maximum，未验证最后业务字段/按钮可见可点击；尚待区分布局与抓图绘制时机，不推导数据丢失','ui/requirements界面与验收所有者；MA-04'),
 ('F-951','minor','区域覆盖后台捕获任务cancelFlag却仍使用NullEvaluationContext（KinEvaluationChannel.cpp:632），取消恒false且进度被丢弃；单点/批量取消及迟到结果隔离通过不能证明coverage真实早停；源码接线已确认，大型采样取消延迟尚未实测','ui区域覆盖执行通道所有者；MA-03'),
 ('F-952','suggestion','主线扩展复验41个exe均有XML/stdout，显式传--ird_report仍仅产36份报告：evidence/evidence_contract/policy/policy_contract/runtime缺原生ird报告；需补接报告入口或登记明确豁免，不能以伪造转换报告补齐三件套','testkit及相关目标所有者；MA-09'),
]
ids={f['id'] for f in existing['findings']}
if any(id in ids for id,_,_,_ in new):
    raise RuntimeError('finding编号已有占用；禁止覆盖')
blocks=[]
for id,severity,title,owner in new:
    item=dict(id=id,source='2026-10-21 合入后主线5fd9a3d1需求/建模复核；traceability/builds/main-functional-audit-20261021-5fd9a3d1/REPORT.md',
              severity=severity,status='open',registeredAt='2026-10-21',title=title,owner=owner)
    blocks.append('\n'.join('    '+line for line in json.dumps(item,ensure_ascii=False,indent=2).splitlines()).replace('\n','\r\n').encode('utf-8'))
suffix=b'\r\n  ]\r\n}\r\n'
assert original.endswith(suffix)
prefix=original[:-len(suffix)]
updated=prefix+b',\r\n'+b',\r\n'.join(blocks)+suffix
json.loads(updated.decode('utf-8-sig'))
assert updated.startswith(prefix)
finding_path.write_bytes(updated)
print(json.dumps(dict(target=str(target),files=len(records)+1,newFindings=[x[0] for x in new]),ensure_ascii=False))
