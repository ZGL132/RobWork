# WP-10-T12（UI-T12）执行留痕索引

执行日期：2026-09-21。分支 `wp10-t12`（base `e071a0a7317fc6c2be0b56ee8dbf78b3896ba858`＝UI-T11 合入后状态）。

## 构建留痕

| 模式 | 命令口径 | 结论 |
| --- | --- | --- |
| 集成模式 | 仓库根 `build/`（Release，`RWS_BUILD_INDUSTRIALROBOT:BOOL=ON` 缓存已 grep 确认）；`cmake --build build --config Release --target sdurws_ird_ui_test`／`sdurws_ird_ui_contract_test`／`sdurws_ird_ui_gui_test` | 三目标零 error（契约 verify 两行原样执行；GUI 目标为回归确认面） |
| 冒烟模式 | `cmake -S RobWork/RobWorkStudio/src/rwslibs/industrialrobot -B build-smoke-wp10-t12`＋vcpkg toolchain 绝对路径＋`Qt6_DIR`（AGENTS §4.1；toolchain 相对路径不被解析，首次配置实测改绝对路径）＋`--target sdurws_ird_ui_test` 等三目标 | 三目标零 error |

## 测试留痕（gtest XML＋ird-test-report.json＋控制台日志，双模式同口径）

| 目标 | 集成模式 | 冒烟模式 | 结论 |
| --- | --- | --- | --- |
| sdurws_ird_ui_test | `sdurws_ird_ui_test.xml`＋`ird-test-report.json`＋`.console.log` | `*-smoke` 同名三件 | **119/119 通过**（含 UI-T12 新增 13 用例：DraftControllerModelTest——保存/应用分离、未保存标记合并、跨模块汇总、定时保存周期与双触发防御、落盘线程纪律与 UI-DRAFT-AUTOSAVE-FAILED、在途串行与代次守卫、只读/未绑定拒绝、.bak 恢复与横幅、损坏清单与显式 discard、Stale 冲突呈现与保留、重建基线、局部撤销栈边界、会话生命周期与生产者接线闭环） |
| sdurws_ird_ui_contract_test | `sdurws_ird_ui_contract_test.xml`＋`ird-test-report-contract.json`＋`.console.log` | `*-smoke` 同名三件 | **12/12 通过**（含 UI-T12 新增 3 用例：DraftContractTest——DraftService 语义锚闭环、Stale 数据传递与 O-31 虚派发、真后台落盘线程同步完成） |
| sdurws_ird_ui_gui_test | `sdurws_ird_ui_gui_test.xml`＋`ird-test-report-gui.json`＋`.console.log` | `*-smoke` 同名三件 | **27/28 通过，1 环境失败**（详见下节——零本任务改动面用例失败） |

### GUI 环境失败如实登记（testkit §7.5——不记为通过）

- 失败用例：`ParamTablePanelGuiTest.BatchPasteImpactDetail_UI_T08_ACC1`（UI-T08 既有交付面，本任务未触碰）。
- 失败原因：宿主环境剪贴板被其他进程占用——`OleSetClipboard: COM error 0x800401D0（OpenClipboard 被拒）`，粘贴 0 行导致影响明细断言不满足；三轮执行（初次＋两次间隔重跑，`batchpaste-rerun*.console.log`）错误一致，属环境资源竞争而非代码缺陷。
- 与本任务改动无关的佐证：失败点在剪贴板系统调用（网络/资源面），本任务交付物（DraftController/IDraftController/端口投影）与该用例零耦合；其模型层等价断言（批量粘贴影响明细）在 `FormEditModelTest` 内全数通过并随本任务回归（119 用例内）。

## 门禁留痕（ird_gates——WP-01-T01 引擎，双模式＋基线逐码比对）

| 文件 | 内容 | 结论 |
| --- | --- | --- |
| ird_gates-integration.log | 集成树 `--target ird_gates`（引擎自测七类植入违例全部按预期检出） | 与 wp10-t11 基线逐码比对零变化 |
| ird_gates-smoke.log | 冒烟树同目标 | 与集成模式命中集一致（gates-hit-set.txt vs gates-hit-set-smoke.txt diff 为空） |
| gates-hit-set.txt／gates-hit-set-smoke.txt | 命中码计数（IRD-GATE-LIB×8／R-1×1／R-3×33／R-4×3／R-5×1／SUB×39／T-1×5／T-2×1） | 两模式一致 |
| gates-hit-set-diff.txt | 与 wp10-t11 基线（`ird_gates-integration.log`，＝本任务 base 同源状态）的比对结论 | **零变化**——新增产品面文件（IDraftController.hpp／DraftController.cpp）零 Qt 头包含（R-3 例外面零扩大）、零表外边（O-31）；全部命中均为 DTB §4.5 登记册既有项（目标失败＝机器面滞后既有状态，与 wp10-t11 同型） |

## O-31 处置执行面证据（acceptance 3）

- `NoCrossUnitInclude_O31_UI_BUILD` 常驻守卫（BuildRedLineTest，随 119 用例运行）对新增产品面文件扫描零命中。
- 产品面零对 project/evidence/execution/policy/runtime 的链接或 include：C-5 写半区经 ui 自有端口 `IUiDraftStorePort`（UiPorts.hpp，本任务首消费冻结）＋值投影（`DraftDocumentProjection`/`DraftSaveOutcome`/`DraftLoadOutcome`/`DraftDiscardOutcome`/`StaleRevisionDetailProjection`——UiProjections.hpp）承载；L5 适配归装配层。
- RV-10/§8.7 两栈边界语义零变化：控制器结构上无命令提交通道（"不得自动重试提交"由构造保证，具名用例 `LocalUndoStackBoundToDraftNotRevisions`／`StaleRevisionRetainsDraftAndPresentsConflict` 钉住）。

## 其他留痕

| 文件 | 内容 |
| --- | --- |
| validate-task.log | `validate-task.ps1`：PASS (1 tasks) |
| batchpaste-rerun*.console.log／*.xml | GUI 剪贴板用例两次间隔重跑记录（环境失败复核链） |
