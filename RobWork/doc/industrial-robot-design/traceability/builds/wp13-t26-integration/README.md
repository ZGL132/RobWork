# WP-13-T26 集成证据——建模双分支整合及联合复验

任务：WP-13-T26（建模双分支集成及真实交互联合复验；契约 tasks/foundation/WP-13-T26.json）。
日期：2026-10-20。执行：ZCode 集成会话（worktree C:/Users/zgl18/.codex/worktrees/486b/RobWork，集成分支 wp13-t26）。

## 集成范围与冻结源

| 项 | 值 |
| --- | --- |
| 集成分支 | wp13-t26（自 wp13-t24 建，先合并主线治理提交 90e9c041——DoD 条款，再合并 wp13-t25） |
| 源 A | wp13-t24@e64cdd6260a077cb3e42f3e4dbef44cb818045d7（完整草稿/局部历史/冲突/磁盘生命周期；含 wp14-t12 全部提交） |
| 源 B | wp13-t25@6cfe7ea382f8d2425531fa7fe221d50701a3e492（F06 WorkCell 导入、F01 逐关节 v/a、F07 自碰撞候选移交、F14 耦合编辑与宿主 R2） |
| 冲突处理 | 8 处冲突（6 测试文件＋证据索引＋modeling 卡）；双方文件尾新增用例逐段保留，逐文件核对记录见 merge-test-preservation.json；证据索引由 index-evidence.ps1 重建 |

## 集成新发现缺陷（先红后修，红证在案）

| 缺陷 | 红证 | 修复 |
| --- | --- | --- |
| 导入自碰撞候选跨模型泄漏：seedTemplateSession()/会话关闭不清上一导入候选，模板替换/换项目后旧连杆名对仍参与策略预填 | red-modeling-candidate-lifecycle.{xml,log}（ImportCandidatesDoNotLeakAcrossTemplateOrClose_MDL04_PM04 失败，461/1） | 候选与工作集纳入同一私有局部历史帧 DraftHistoryFrame（撤销/重做同帧恢复）；模板重种子、自定义声明链、WorkCell 导入、规范包导入、固定修订恢复、磁盘草稿恢复、外部修订刷新、关闭八处边界清旧来源；失败/取消路径保留原状态 |
| 撤销导入后草稿回退而候选残留（GUI 真实按钮路径） | red-gui-candidate-history.{xml,log}（ImportedCandidatesFollowRealDraftUndoRedo_MDL04_PM04 失败） | 同上（历史帧持工作集＋候选，undo/redo 一起走） |
| 装配清单锚点行漂移（语义冲突，git 不可见）：两支 UiPlugin.cpp 合并后 attachModule 真实行号 2707/2781，清单仍登记 2700/2774（t24 侧值） | red-ui-contract-manifest-anchor.{xml,log}（WiredEntriesDeclareConsistentHandleSymbolAndAnchor_UI_T84 失败） | production-assembly-manifest.json 两处 wiredAnchor 更新为 2707/2781（UI-T84 行级核对复绿） |

## 集成新回归（两支组合而非单支字段复测）

- ModelingIntegration.VelocityAndCouplingSurviveCompleteDraftAndRevision_PM04_MDL21：F01 逐关节 v/a＋F14 非对角 R2 耦合经完整修订帧恢复、磁盘草稿封装与应用信封 payloadCanonical 全程保持。
- ModelingIntegration.ImportCandidatesDoNotLeakAcrossTemplateOrClose_MDL04_PM04：见上表。
- ModelingUiModuleHostWiring.ImportedCandidatesFollowRealDraftUndoRedo_MDL04_PM04（GUI 真实按钮）：撤销回到模板后候选清空，重做恢复导入稿连同候选。

## 返工批（attempt 1 验收 fail 后——2026-10-20）

attempt 1 独立子会话验收（acc/WP-13-T26/1@ce7fa787）verdict=fail，两项阻断均为证据留痕面；本批返工闭环：

| 阻断 | 闭环 |
| --- | --- |
| A1 缺 ird 报告与截图（契约 acceptance 条 2 四类载体） | 补齐 11 份 `*-report.json`（green-integration-*×9＋green-standalone-*×3 中 runtime 无报告——F-944 登记）；截图两目录：host-shots/（正式宿主 layout 冒烟真实窗口 9 张＋几何报告）＋gui-shots/（modeling_gui 真实窗口用例 29 张） |
| A2 SHA256SUMS 与库内 README 哈希失配 | 根因＝行尾归一化（F-943 同族）：工作树与仓库 blob 的 CRLF/LF 不一致。本批起 SHA256SUMS 改按**仓库 blob 内容（LF 归一）**计算，校验口径 `git show HEAD:<路径>` 逐一可复现 |

返工中新发现并修复（先红后修，红证在案）：**F-945**——正式宿主 layout 冒烟（IRD_UI_PLUGIN_SMOKE=layout）探针未随 T82 滚动视口（installDockViewport 的 QScrollArea）适配，命令条布局锚恒空导致整组断言误报（cur=0）；且装配拆分断言仍停留旧 7/2 撤牌口径，未随 WP-14-T12/REQ-10 九条全恢复可达的既定决策更新。主线 90e9c041 亲测同样失败（red-layout-smoke-mainline-90e9c041.log）＝存量缺陷非集成引入。修复＝探针解包 QScrollArea 取面板本体＋断言更新 9/0；三阶段红证：red-layout-smoke-probe-broken-15fails.log（修复前 15 断言红）→ red-layout-smoke-split-stale-1fail.log（探针修复后仅拆分断言红）→ green-host-tour-layout.log（全绿 DONE）。

## 绿证（全部真实执行，构建树 build/＝集成模式、build/modeling-integration-20261020/standalone＝独立冒烟）

| 文件 | 结果 |
| --- | --- |
| green-full-build.log | 集成树全量 Release 构建 Exit=0（VS2022 x64） |
| green-standalone-configure.log | 独立冒烟树配置成功（vcpkg toolchain＋Qt 前缀，AGENTS §4.1 双模式口径） |
| green-integration-modeling.{xml,log} | 462 通过/0 失败/0 跳过 |
| green-integration-modeling-gui.{xml,log} | 91 跑：90 通过＋1 环境跳过（F-620 剪贴板被外部占用——返工批同源刷新，xml/log/report 同一运行） |
| green-integration-runtime.{xml,log} | 294 跑：293 通过＋1 环境跳过（ContractSuite.ChildProcessWorker_RT_ID_1——子进程工作器环境面） |
| green-integration-kinematics.{xml,log} | 288 跑：287 通过＋1 登记跳过（KinGuiPresentationRegisteredNotExecuted_ACC1——GUI 呈现注册不执行面） |
| green-integration-ui.{xml,log} | 339/0/0 |
| green-integration-ui-contract.{xml,log} | 63/0/0（清单锚点修复后） |
| green-integration-kinematics-contract.{xml,log} | 57/0/0 |
| green-integration-ui-gui.{xml,log} | 95/0/0（真实 Qt 窗口；探针修复后重跑确认零回归） |
| green-integration-kinematics-gui.{xml,log} | 20/0/0（真实 Qt 窗口） |
| green-standalone-modeling.{xml,log} | 360/0/0 |
| green-standalone-modeling-contract.{xml,log} | 8/0/0 |
| green-standalone-modeling-gui.{xml,log} | 60 跑：59 通过＋1 环境跳过（PreviewPane_MultiTypeChain_UI_T59——F-620 剪贴板被外部占用） |
| green-host-tour-auto.log | 正式宿主 sdurws_ird_studio 真实窗口 auto 冒烟 8 步全 ok（建项目/共享树/L1 检查器/应用模块/L3/清理/建模装配 12 命令/关闭重开），Exit=0（返工批探针修复后重跑） |
| green-host-tour-layout.log＋host-shots/ | 正式宿主 layout 冒烟全绿 DONE（净室首启默认布局＋呼出需求面板＋命令条九键/导入下拉/更多操作断言＋窄窗无重叠＋最小窗中央区保护），真实窗口截图 9 张＋几何报告（F-945 修复后） |
| green-integration-*-report.json ×9＋green-standalone-*-report.json ×3 | ird 测试报告（A1 闭环；runtime 套件不产报告＝F-944 登记） |
| gui-shots/（29 张 PNG） | modeling_gui 真实窗口用例运行期截图（返工批重跑同源） |
| green-ird-gates.log | R-1/R-2/R-3/R-4/R-5/T-1/T-2/SUB/GRAPH/LIB/SA02 全部零命中 |

## 开放面（本批不宣称完成）

- 真实三维逐项验证（真实 mesh 渲染、三维覆盖层对照）未执行，不得标 PASS——归独立验收批（B5 口径）。
- F07 候选重开恢复与来源 schema（per-pair provenance）仍属 policy 卡演化面。
- 47 项完整终验、两任务契约（WP-13-T24/T25）正式独立验收仍开放；本会话非全新上下文，不冒称独立验收通过。
- 按契约不推送远程、不合入 redesign-main；冻结 SHA 见交接说明（docs/superpowers/plans/2026-10-20-modeling-integration.md）。
