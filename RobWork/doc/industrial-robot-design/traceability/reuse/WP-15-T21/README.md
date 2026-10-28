# WP-15-T21 留痕——运动学面板启动崩溃修复（2026-10-28）

## 任务背景

所有者任务「全量编译运行软件，对软件界面进行操作，验证功能」中发现 `sdurws_ird_studio` 无参启动即崩（SEH 访问违例）。崩溃日志在案：2026-10-24 两次、10-25 两次、10-28 一次（`crash-report-20261028-pre-fix.log`），修复前复现率 4/4＋历史 3/3。2026-10-17 的历史会话开发日志显示当时宿主可完整启动（host session attached＋项目打开拍），回归窗口 2026-10-18→10-24，与 WP-15-T20 引入 `worldToBase()` 消费（提交 526311d7e，2026-10-22）吻合。

## 根因（栈证据 crash-stack-pre-fix.log）

- 崩溃点：`ui::KinSessionModelView::worldToBase() + 0xD`（`ui/plugin/KinEvaluationChannel.hpp:129`）——`m_delegate` 未绑定时对空 `this` 做虚分派（first-chance AV READ target=0x0，RCX=0）。
- 调用链（原始栈扫描符号化，帧链完整）：`KinematicsPanelWidget` 构造（KinematicsPanelWidget.cpp:191）→ `refreshAll`(:723) → `refreshVisualization`(:1934) 取 `m_services.modelView->worldToBase()`，而快照空态判定 `if (m_session.lastWorkspaceRun)` 在其后（:1936）。
- 缝契约（KinEvaluationChannel.hpp:110 注释）：构造未绑定视图是合法初始态，「消费前置由面板空态判定保证——快照缺位不取模型」。启动装配期面板构造先于任何快照发布，`refreshVisualization` 违反了该前置。

## 修复

`KinematicsPanelWidget.cpp` 单文件：`worldTbase` 取值移入消费分支就地取值（`m_session.lastWorkspaceRun` 分支与 `m_services.batchRender` 非空分支——batch 在场即会话产物在场，委托必已绑定）。各会话态行为不变；构造期无快照路径不再触碰未绑定委托。与同文件既有空态判定惯例（`refreshWsSummary`:1542、`refreshWsProjectionCanvas`:1801）同口径。缝文件 `KinEvaluationChannel.hpp` 零改动（fail-fast 契约注释原样保留）。

## 验证（ QT_QPA_PLATFORM=windows，桌面锁定环境）

| 项 | 结果 |
| --- | --- |
| Release 全量构建（ALL_BUILD） | 155 目标 0 错误；修复后增量重链 0 错误 |
| 真机启动（修复前） | 4/4 必崩，dev 日志止于「插件诊断栈就绪」，装配报告止于「共享面装配完成」 |
| 真机启动（修复后） | 主窗口呈现（RobWorkStudio v26.10.28-redesign-main），装配报告：六域 ok → 共享面 → 项目树多 Dock → 事件桥接线 → Dock 宽度收束，全绿 |
| UIA 界面操作 | 六菜单枚举（结构/禁用态正确）；六个 IRD 面板开关逐一触发（运动学面板 dump 到真实 KinematicsPanelWidget＋四页签＋诚实降级状态行）；恢复默认布局；全程零崩溃 |
| kinematics_gui_test | 34/48——与 stash 未修复基线逐项一致（零回归）；14 失败全部＝`TestAnchor: 无编译产物 WorkCell（值面替身）` 夹具异常（WP-15-T20 审核批 2026-10-21 全绿 35 项之后新增用例的既有夹具问题，非本包范围） |
| ui_gui_test / dynamics_gui_test | 全绿 |
| requirements_gui_test / modeling_gui_test | 6＋1 失败均为剪贴板 COM 0x800401d0（`OleSetClipboard: Failed... OpenClipboard`）——锁定桌面环境性失败（F-456 同族口径），且与修复前 04:17 ird-test-report.json 基线完全一致；待解锁桌面复跑定性 |
| ird_gates | 本包零新增命中；全局 2 命中＝SA02 `0004-sandbox-optimization-levenbergmarquardt.patch` 登记在途项（RC-04/REUSE-T55，先于本包存在，责任归该治理批） |
| validate-reuse.ps1 | PASS（本包 Digest 口径同步后） |

## 附带登记

- `functions/kinematics.json`：`KinematicsPanelWidget.cpp` 两处 sha256 按 `reuse-contract.ps1` Digest 口径（BOM 剥除＋CRLF→LF＋尾单换行后 SHA256）重算为 `605bce8d…`；AnchorRuntimeView 端行 2355→2362（本包净 +7 行）。
- 测试运行自举生成的金样夹具副作用（pol-policy rc07 两个 json＋manifest 增段、exec-channel-protocol-fixture/、rt-codec-fixture/）不属于本包，已还原/清理，测试下次运行按需自举。
