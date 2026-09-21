# WP-10-T11（UI-T11）执行留痕索引

执行日期：2026-09-21。分支 `wp10-t11`（base 63e3007112c28d2b0dff48119fac3fa3a6232984＝UI-T10 合入后状态）。

## 构建留痕

| 文件 | 内容 | 结论 |
| --- | --- | --- |
| integrated-build.log | 集成模式全量构建（仓库根 build/，Release，RWS_BUILD_INDUSTRIALROBOT=ON 缓存确认后构建） | exit=0，零 error（新增 TU：UiSessionController.cpp/SessionControllerModelTest.cpp/SessionContractTest.cpp 同会话增量编译零告警零错误） |
| smoke-configure.log | 冒烟模式配置（vcpkg toolchain 绝对路径＋Qt6_DIR——AGENTS §4.1；toolchain 相对路径不被解析，改绝对路径，如实登记） | exit=0 |
| smoke-build.log | 冒烟模式全量构建（新树全量编译，含 3 个新 TU） | exit=0，零 error |

## 测试留痕（gtest XML＋ird-test-report.json＋控制台）

| 目标 | 集成模式 | 冒烟模式 | 结论 |
| --- | --- | --- | --- |
| sdurws_ird_ui_test | sdurws_ird_ui_test.xml ＋ ird-test-report.json ＋ .console.log | *-smoke 同名三件 | 106/106 通过（含 UI-T11 新增 20 用例：SessionControllerModelTest 19＋MissingFactoryPortFailsFast 1） |
| sdurws_ird_ui_contract_test | sdurws_ird_ui_contract_test.xml ＋ ird-test-report-contract.json ＋ .console.log | *-smoke 同名三件 | 9/9 通过（含 UI-T11 新增 6 用例：SessionContract） |
| sdurws_ird_ui_gui_test | sdurws_ird_ui_gui_test.xml ＋ ird-test-report-gui.json ＋ .console.log | *-smoke 同名三件 | 28/28 通过（零新增——回归确认本库改动未破坏既有 GUI 面） |

GUI 执行口径（§12.2）：QT_QPA_PLATFORM=windows＋QT_QPA_PLATFORM_PLUGIN_PATH 指向 Qt 6.11.1
msvc2022_64 plugins/platforms（本机 Qt 布局，非构建约定变更）；绝对路径单实例。
如实登记：集成树 `*_report` 自定义目标直跑 GUI 可执行时因缺平台插件环境退出
（MSB8066，0xC0000409）——按 §12.2 纪律以显式环境变量手工执行后全绿，上述 XML/报告
均为该手工执行产物。

## 门禁与治理

| 文件 | 内容 | 结论 |
| --- | --- | --- |
| ird_gates-integration.log | 集成源码根 ird_gates（cmake -P 脚本模式直跑） | 91 处原始命中（与 base 同口径） |
| ird_gates-base.log | base（63e30071）detached worktree 同引擎基线运行 | 91 处原始命中 |
| ird_gates-smoke.log | 冒烟口径复跑（引擎为纯源码扫描——与集成同源码树） | 归一化后与集成命中集逐码一致 |
| gates-hit-set.txt | 本次命中集归一化清单（49 条唯一——剥离盘符绝对路径前缀＋[ird_gates] 前缀＋去重排序；口径与 base 侧完全一致） | — |
| gates-hit-set-diff.txt | 与 base 基线（49 条）归一化逐码比对 | **零变化**（DIFF EXIT=0——本任务新增产品面文件零 Qt 头包含，防线/对话框机制全模型层；无需登记提交件） |
| validate-task.log | validate-task.ps1 -TaskFile <契约全路径> | PASS |

## 说明

- 门禁零净增：新增公共头（UiSessionController.hpp）与实现 TU（UiSessionController.cpp）
  均零 Qt 头包含——UiSessionController 机制为纯模型层（§12.1 第一层分工），R-3 例外面
  零扩大；测试 TU 的 QCoreApplication include 不在门禁扫描面（与既有 13 个测试 TU 同态）。
- 会话脏标记供给口径：`sessionDirty` 的生产者＝编辑会话（IDraftController §10.5/UI-T12），
  本任务承载该事实位（对话框装配/上下文投影/放弃·保存处置清除）——生产者接线随 UI-T12
  契约头落地复核（SessionControllerModelTest 文件头如实登记）。
- 对话框 GUI 呈现面：本任务交付"机制"（数据装配＋决议解析＋状态迁移＋Draining 防线，
  §12.1 第一/二层），关闭对话框 Widget 呈现与 §12.3 用例扩展归 GUI 层消费任务——
  契约 verify 行仅含 ui_test/ui_contract_test 两目标，已全部执行留痕。
- 需求追溯：UI-SES-2~7（PM-07/PM-03——横幅含 PID/actionKind 区分/防永久阻塞四级防线/
  迟到写不重试）、UI-LCY-1（SA-17——Closed 后迟到写拒绝提示）、UI-SES-4/5（TASK-03/
  SA-17——切换持有点保活/等待覆盖在途归档）、INV-SES-1~4（§5.2）、O-31 处置（C-3/5/8
  会话端口 ui 自有承载——SessionContract.O31SessionFacesAreUiOwnedInterfaces 结构自证＋
  NoCrossUnitInclude_O31_UI_BUILD 回归零命中）、P-UI-8 处置（T_force/T_force2 默认保守值
  120 s/300 s 装配期可配——默认值冻结断言＋30 s 可配触发面双测试，不私定终值）——
  详见任务契约 acceptance 与 units/ui.md §16.7 v1.3 行。
