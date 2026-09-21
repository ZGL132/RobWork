# WP-10-T10（UI-T10）执行留痕索引

执行日期：2026-09-21。分支 `wp10-t10`（base f6968386c6748fe6e7b17e88c18f6f22c00ffccd）。

## 构建留痕

| 文件 | 内容 | 结论 |
| --- | --- | --- |
| integrated-build.log | 集成模式全量构建（仓库根 build/，Release，RWS_BUILD_INDUSTRIALROBOT=ON） | exit=0，零 error（新增 TU：AboutDialog.cpp/AboutDialogModelTest.cpp/AboutDialogGuiTest.cpp 同会话增量编译零告警零错误） |
| smoke-configure.log | 冒烟模式配置（vcpkg toolchain 绝对路径＋Qt6_DIR，AGENTS §4.1） | exit=0 |
| smoke-build.log | 冒烟模式全量构建（新树全量编译，含 3 个新 TU） | exit=0，零 error |

## 测试留痕（gtest XML＋ird-test-report.json＋控制台）

| 目标 | 集成模式 | 冒烟模式 | 结论 |
| --- | --- | --- | --- |
| sdurws_ird_ui_test | sdurws_ird_ui_test.xml ＋ ird-test-report.json ＋ .console.log | *-smoke 同名三件 | 86/86 通过（含 UI-T10 新增 9 用例） |
| sdurws_ird_ui_contract_test | sdurws_ird_ui_contract_test.xml ＋ ird-test-report-contract.json ＋ .console.log | *-smoke 同名三件 | 3/3 通过 |
| sdurws_ird_ui_gui_test | sdurws_ird_ui_gui_test.xml ＋ ird-test-report-gui.json ＋ .console.log | *-smoke 同名三件 | 28/28 通过（含 UI-T10 新增 3 用例） |

GUI 执行口径：QT_QPA_PLATFORM=windows＋QT_QPA_PLATFORM_PLUGIN_PATH 指向 Qt 6.11.1
msvc2022_64 plugins/platforms（本机 Qt 布局，非构建约定变更）；绝对路径单实例。

## 门禁与治理

| 文件 | 内容 | 结论 |
| --- | --- | --- |
| ird_gates-integration.log | 集成源码根 ird_gates（cmake -P 直跑） | 42 处唯一命中——41 处为 base 既有登记项＋本任务恰增 1 处（AboutDialog.cpp R3 头包含，F-259 同型） |
| ird_gates-base.log | base（f6968386）detached worktree 同引擎基线运行 | 41 处唯一命中（＝wp10-t09 合入后状态） |
| ird_gates-smoke.log | 冒烟口径复跑（引擎为纯源码扫描——与集成同源码树） | 命中集与集成运行逐码一致 |
| gates-hit-set.txt | 本次命中集归一化清单（42 条唯一） | — |
| gates-hit-set-diff.txt | 与 base 基线（41 条）归一化逐码比对 | 恰增 1 处（IRD-GATE-R3｜QtClassHeader｜ui/src/AboutDialog.cpp），其余零变化 |
| validate-task.log | validate-task.ps1 -TaskFile UI-T10.json | PASS |

## 说明

- ird_gates 目标级失败＝42 处命中中 41 处为 DTB §4.5 登记册既有项（机器面
  滞后状态，WP-01-T03 责任面）＋1 处本任务净增（R-3 例外本体目标既有登记
  类目）——净增命中登记提交件 traceability/wp10-t10-gate-registrations.md，
  本任务未私改门禁词表。
- 版本相关口径：NFR-DEP-05 版本数据由 L5 注入（WP-24-T01 基线产出前
  available=false 承载、版本值零虚构）；插件版本列阶段 A 恒「不适用」
  占位（§10.9 报告形状无版本字段——不虚构）。
- 需求追溯：UI-PLG-2（UX-14——清单＝白名单∩报告、版本与注入基线一致、
  帮助入口链接用户手册）、O-31 处置（版本数据 L5 注入、插件清单静态白
  名单——交付物零对端类型）、UX-02（插件标题键族，零内部 token 泄漏）、
  ERR-01（不适用占位不伪造）——详见任务契约 acceptance 与 units/ui.md
  §16.7 v1.2 行。
