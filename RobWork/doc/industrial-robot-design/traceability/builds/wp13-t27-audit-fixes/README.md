# WP-13-T27 修复批证据——主线功能审计（main-functional-audit-20261021）四项闭环

审计基线：redesign-main@5fd9a3d1（报告本地提交 e5b34289，同仓
traceability/builds/main-functional-audit-20261021-5fd9a3d1/REPORT.md）。
修复分支：wp13-t27（自 e5b34289）。日期：2026-10-21。

## 本批闭环四项（先红后修，红证在案）

| 审计项 | 缺陷 | 红证 | 修复 | 绿证 |
| --- | --- | --- | --- | --- |
| MA-05 | 优化契约 `TwoStepApplyCreatesBranchAndRevision_WP20T07_ACC2` 稳定失败——旧断言"建支修订引用集零 RobotDesign 条目"与 A-13 修订自足（完整引用集）语义冲突 | 审计轮 optimization_contract.xml（51 通过＋1 失败，单跑同败） | 断言按权威语义修正为强断言三件：恰一根引用＋原 oid＋原 cv＋基线字节原样可取（ApplierContractTest.cpp，含语义修正注记） | green-optimization-contract-52.xml：52/52（含本用例）；全量 16 套件回归绿 |
| MA-04 | 需求 Dock 滚到底内容区近乎全空白（1920×1152 与 1024×790 两轮稳定复现），tour 只断言滚动条到位 | red-req-tour-blank-tail-stale-assertions.log（cur=0/0）；审计轮 tour-9-populated-bottom 原图 | 双面修复：①ui 侧 `DockContentResizeFilter`（installDockViewport）在 LayoutRequest/Paint 拍重申"内容高==max(视口,当前最小高)"并以 maximumHeight 同拍夹紧（诊断留痕 diag-event-trace-stale-negotiation.log：最小高回落 455 后零事件到达、917↔455 拉锯——任何纯事件钩子无效，maximumHeight 为 Qt 硬约束）；②requirements 面板 currentChanged 收缩自身到当前最小高。tour step13 补真实断言"滚到底后可视交互控件数≥1"＋内外滚动几何 stdout 留痕 | green-req-tour-after-fix.log（content 889→560、vmax 570→241、可视控件 0→7/5，tour 全绿）＋req-tour-fixed-shots/ 两张实拍（树节点＋五卡全可见） |
| MA-03 | 区域覆盖后台捕获 cancelFlag 却用 NullEvaluationContext——计算不消费取消、进度被吞 | 审计轮源码定位（KinEvaluationChannel.cpp:632） | launchCoverageTask 接 CancelableEvaluationContext（批间/逐样本取消探针生效，取消按 interrupted 轨入账不伪装完整覆盖）；新增进度原子（reportProgress 单调取大）经 TaskEntry.progress 投影到任务行真实百分比（单点求解无进度源维持缺省） | 域语义零变化纯接线：kinematics 287+1、ui 339、ui_contract 63、KinEvaluationChannel 27 用例全绿；16 套件回归绿 |
| MA-09 | evidence/evidence_contract/policy/policy_contract/runtime 五目标 `--ird_report` 不产报告（gtest_main 固定入口无监听器安装点） | 审计轮 41 exe 仅 36 份报告的盘点（REPORT.md MA-09 行） | 五目标接入自有 TestMainReport.cpp（安装 testkit TestRecordListener；runtime/evidence/policy 为 L2 零 Qt——纯 gtest 入口不构造 QCoreApplication，kinematics 先例的差异豁免如实登记）；gtest_main→gtest | ma09-runtime-ird-report.json（实跑产出）；ma09-targets-build.log 五目标构建零错误 |

## 回归面（全绿）

- 集成 16 套件：modeling 462、requirements 221+1 登记跳过、runtime 293+1、
  kinematics 287+1、ui 339、ui_contract 63、kinematics_contract 57、
  evidence 205、evidence_contract 14、policy 211、policy_contract 23、
  optimization 214+1、**optimization_contract 52/52**、modeling_gui 91、
  requirements_gui 110、kinematics_gui 20。
- 独立冒烟 6 套件：modeling 360、modeling_contract 8、modeling_gui 60、
  requirements 221+1（V-22 登记）、requirements_contract 16、
  requirements_gui 110。
- 正式宿主四 tour 全绿：auto 8 步、layout 67 断言（含 9/0 拆分）、
  modeling-tour、requirements-tour（含 MA-04 新断言）。
- ird_gates 全项零命中；validate-docs PASS（14500 evidence）。

## 开放面（本批不宣称完成）

- MA-01/02（真实资源 mesh、真实碰撞设施）：RT-T15/P-MDL-3 登记的诚实
  跳过，待所有者裁决后按独立任务批落地（STL/OBJ 原生加载器在位；
  DAE/WRL/IV 需经 vcpkg 新增 Assimp 并按 DTB §3 登记依赖）。
- MA-06（F07 逐对来源 schema 与重开恢复）：属 policy 卡演化面，需契约
  演化决策。
- MA-07/08（跨模块联合终验、三维完整验收）：需同一真实项目的验收批。
- 本批不替代全新上下文独立验收；合入 redesign-main 决策归所有者。
