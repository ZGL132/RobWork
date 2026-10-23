from pathlib import Path
import json
r=Path(r'C:/Users/zgl18/AppData/Local/Temp/ird-accept-t01-batch1-20261022-211635')
p=r/'acceptance.json'; j=json.loads(p.read_text(encoding='utf-8'))
j['findings'].append({'id':'T01-B1-03','priority':'P2','path':'RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01/UpstreamSuitabilityTest.cpp','line':170,'title':'按产品roll语义构造Euler掩码反例','detail':'EulerMaskCannotBeSubstitutedByEaaRows 使用 RPY(0.6,0.4,0.7)→RPY(1.2,0.4,0.7)，在上游实际构造约定中是释放Z/yaw，固定Y/pitch和X/roll。断言却检查EAA的Y/Z残差，包含本来释放的Z分量，因此不能证成注释所述产品自由roll（X轴）与EAA Y/Z行掩码的差异。应按 Z/Y/X 参数顺序构造固定 yaw/pitch、改变roll 的对照（例如RPY(0.7,0.4,0.6)→RPY(0.7,0.4,1.2)），固定索引0/1，再检查EAA Y/Z残差。','evidence':['RobWork/RobWork/src/rw/math/RPY.hpp:49','RobWork/RobWork/src/rw/math/RPY.cpp:74','RobWork/RobWorkStudio/src/rwslibs/industrialrobot/modeling/src/RpyMath.hpp:47']})
j['findings'][-1]['line']=next(i for i,l in enumerate((r/'repo/RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01/UpstreamSuitabilityTest.cpp').read_text(encoding='utf-8-sig').splitlines(),1) if 'TEST(ReuseAudit, EulerMaskCannotBeSubstitutedByEaaRows)' in l)
j['checks']['task']['command']='pwsh -NoProfile -File RobWork/scripts/industrialrobot/validate-task.ps1 -TaskFile RobWork/doc/industrial-robot-design/tasks/REUSE-T01.json'
j['checks']['docs']['command']='pwsh -NoProfile -File RobWork/scripts/industrialrobot/validate-docs.ps1'
j['checks']['reuse']['command']='pwsh -NoProfile -File RobWork/scripts/industrialrobot/validate-reuse.ps1 -TaskFile RobWork/doc/industrial-robot-design/tasks/REUSE-T01.json'
j['checks']['configure']['command']='VS2022 amd64: cmake -S RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01 -B <evidence>/build -G "Visual Studio 17 2022" -A x64 -DRobWork_DIR=D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWork/cmake -DCMAKE_PREFIX_PATH=<vcpkg x64-windows>;<Qt msvc2022_64> -DRWSIM_TARGETS_DIR=D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkSim/cmake/targets'
j['checks']['build']['command']='VS2022 amd64: cmake --build <evidence>/build --config Release --target ird_reuse_audit_probe'
j['checks']['tests']['command']='VS2022 amd64: <evidence>/build/Release/ird_reuse_audit_probe.exe --gtest_output=xml:<evidence>/gtest.xml --ird_report=<evidence>/ird-test-report.json; PATH uses existing build/RobWorkSim/bin/Release, build/RobWork/bin/Release and vcpkg/installed/x64-windows/bin'
p.write_text(json.dumps(j,ensure_ascii=False,indent=2),encoding='utf-8')
text='''# REUSE-T01 首批独立验收：FAIL

冻结提交：`8da272d79e8a0b89e3b1a60e3bcb3942dd59d1ae`；比较基线：`212226cfd2e2ca2fd12619dc57c458888f42534b`。

范围仅为6单元24条pending功能—能力关系（23个不同functionId，FK同功能两条关系符合契约）、3项新增候选能力与10项公共API探测，以及本批docs/task。不是全20单元审计完成、生产复用通过或T02准入。

## 缺陷

'''
for f in j['findings']: text+=f"- **{f['id']} / {f['priority']}** `{f['path']}:{f['line']}`：{f['title']}。{f['detail']}\n\n"
text+='''## 已执行验证

- 独立临时clone，完整检出冻结提交，最终tracked status为空；未修改产品、治理、上游或vcpkg，未提交、合入、推送。
- validate-task/docs/reuse exit=0。复用仅治理PASS，functions=24、closedFiles=0；compileWorkCell仍输出未授权pending提醒。
- 24条区段摘要、输入摘要与符号短名均匹配；除上述qualifiedSymbol缺陷外，人工按命名空间边界核对其余条目。全部decision/status=pending；无新增grant/closure。
- catalog所有绑定资源路径/符号/规范化摘要与声明上游版本匹配，3个新增能力均candidate。完整机器逐项结果见matrix-check.json和catalog-resource-check.json。
- VS2022 x64环境，按README既有RobWork/Sim导出目标重新configure/build；testkit和core/test/TestMainReport.cpp实际编入；无GUI。
- 原10项全部执行：passed=10、failed=0、skipped=0、notRun=0。真实gtest.xml与原生ird-test-report.json一致。Bullet未实现、Jacobian半周误报及RNE torque差异都是限制断言，不代表生产能力成功。
- 独立可达性附加探针：同夹具setQ(pi)后targetReachableResidualRad=0，exit=0；源码、构建和运行日志在semantic-*。此附加反证不并入原10项计数。

本轮未跑全20单元审计、完整上游集、生产迁移/断开、完整数值性能、140项治理自测、DeepEvidence或远程CI。网络代理阻断继承为外部限制。返工应绑定新SHA重新独立验收，原FAIL不改写。
'''
(r/'acceptance.md').write_text(text,encoding='utf-8')
print(json.dumps({'verdict':j['verdict'],'findings':[(f['id'],f['line']) for f in j['findings']],'evidence':str(r)},ensure_ascii=False))
