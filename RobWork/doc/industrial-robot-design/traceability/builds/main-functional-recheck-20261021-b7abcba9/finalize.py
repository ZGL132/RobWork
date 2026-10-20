import pathlib,json,re,shutil,hashlib,subprocess
out=pathlib.Path(__file__).parent
summary=json.loads((out/'test-summary.json').read_text(encoding='utf-8'))
tours={}
for name,prefix in [('requirements-tour','ird-ui-smoke-tour'),('modeling-tour','ird-ui-smoke-mtour'),('layout','ird-ui-smoke-layout')]:
 t=(out/(name+'.log')).read_text(encoding='utf-8-sig',errors='replace')
 tours[name]={'assertionsPassed':len(re.findall(r'^\['+prefix+r'\] pass ',t,re.M)),'png':len(list((out/name).glob('*.png'))),'done':bool(re.search(r'^\['+prefix+r'\] DONE',t,re.M))}
(out/'tour-summary.json').write_text(json.dumps(tours,ensure_ascii=False,indent=2),encoding='utf-8')
native=[s for s in summary['suites'] if s['nativeReport'] is not None]
execution=f'''|检查|本轮结果|证据|
|---|---|---|
|集成重新配置、全量Release构建|PASS，退出0，RWS_BUILD_INDUSTRIALROBOT=ON|configure.log、full-build.log|
|35套集成回归|{summary['total']}条：{summary['passed']}通过、{summary['failed']}失败、{summary['skipped']}跳过|[test-summary.json](test-summary.json)、35份原始XML/日志|
|需求主/契约/GUI|347通过、1登记跳过；其中真实GUI110通过|requirements*.xml|
|建模主/契约/GUI|570通过、0失败、0跳过；其中真实GUI91通过|modeling*.xml|
|运动学真实GUI|35通过、0失败、0跳过|kinematics_gui.xml|
|正式宿主需求tour|{tours['requirements-tour']['assertionsPassed']}项断言通过，{tours['requirements-tour']['png']}张PNG；底部空白复现|requirements-tour.log、requirements-tour/|
|正式宿主建模tour|{tours['modeling-tour']['assertionsPassed']}项断言通过，{tours['modeling-tour']['png']}张PNG|modeling-tour.log、modeling-tour/|
|正式宿主layout/auto|layout {tours['layout']['assertionsPassed']}项断言通过、{tours['layout']['png']}张PNG；auto8步ok|layout.log、auto.log|
|ird_gates|PASS，产品扫描零命中，自测预期检出|gates.log|
|原生报告核对|35套XML，{len(native)}份原生报告；已生成报告逐例ID集合均与XML一致，缺5份|test-summary.json|

全部GUI在VS2022 x64环境、QT_QPA_PLATFORM=windows下以绝对路径串行运行。共71张主流程原图的9页联系表已逐页审阅，另放大空白、临时目标、旋转区域原图。重复场景/历史回归不累计。未运行的功能终验不计为通过。'''
t=(out/'report-template.md').read_text(encoding='utf-8-sig')
t=t.replace('__EXECUTION__',execution).replace('__VISUAL__','本轮正式宿主235项断言通过，但两个尺寸滚到底的原始截图再次大面积空白（requirements-tour/tour-9-populated-bottom-{0,1}.png）').replace('__REPORTS__','本轮真实运行35套，仍只有30份原生报告；缺 evidence/evidence_contract/policy/policy_contract/runtime 五份')
t=t.replace('冻结 `main@49a9e819` 不变。','冻结 `main@49a9e819` 不变。本轮为所有者主线复核，不自签全新上下文独立验收。')
t=t.replace('## 对原结论的校正','补充视觉观察：`modeling-tour/mtour-target-3d-preview.png` 中临时目标文字呈明显多重轮廓，辨识质量须专项确认；本轮仅记录原图现象，未将其归因为坐标算法错误，亦未将这类覆盖层标为完整视觉通过。\n\n## 对原结论的校正')
t+='\n## 直接证据\n\n- [两种尺寸的底部空白原图一](requirements-tour/tour-9-populated-bottom-0.png)、[原图二](requirements-tour/tour-9-populated-bottom-1.png)\n- [截图索引](screenshot-index.json)、[逐套测试计数/失败与跳过](test-summary.json)、[正式窗口计数](tour-summary.json)\n- [已提交源码定位与T27空报告取证](source-check.txt)、[基线/祖先关系](baseline.json)、[被测程序哈希](binary-hashes.json)\n- SHA256SUMS：文本按项目白名单归一LF后哈希，图片按原始字节；不改历史证据。\n'
(out/'REPORT.md').write_text(t,encoding='utf-8')
dest=pathlib.Path('RobWork/doc/industrial-robot-design/traceability/builds/main-functional-recheck-20261021-b7abcba9')
if dest.exists():raise RuntimeError('新证据目录已存在，禁止覆盖')
dest.mkdir(parents=True)
exclude={'report-template.md'}
for f in out.iterdir():
 if f.name=='standalone' or f.name in exclude:continue
 if f.is_dir():shutil.copytree(f,dest/f.name)
 else:shutil.copy2(f,dest/f.name)
exts={x.strip() for x in pathlib.Path('RobWork/scripts/industrialrobot/config/text-extensions.txt').read_text(encoding='utf-8-sig').splitlines() if x.strip() and not x.startswith('#')}
hashes=[]
for f in sorted(dest.rglob('*')):
 if not f.is_file():continue
 b=f.read_bytes()
 if f.suffix.lower() in exts and b'\x00' not in b:b=b.replace(b'\r\n',b'\n').replace(b'\r',b'\n')
 hashes.append(hashlib.sha256(b).hexdigest()+'  '+f.relative_to(dest).as_posix())
(dest/'SHA256SUMS').write_text('\n'.join(hashes)+'\n',encoding='utf-8')
print(json.dumps({'path':str(dest),'filesHashed':len(hashes),'tours':tours},ensure_ascii=False))
