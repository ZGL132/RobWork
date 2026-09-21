# WP-10-T09（UI-T09）执行留痕索引

执行日期：2026-09-21。分支 `wp10-t09`（base 494491949234c3f7b87e58a1ffcee72015c379c6）。

## 构建留痕

| 文件 | 内容 | 结论 |
| --- | --- | --- |
| integrated-build.log | 集成模式全量构建（仓库根 build/，Release） | exit=0，零 error |
| smoke-configure.log | 冒烟模式配置（vcpkg toolchain＋Qt6_DIR，AGENTS §4.1） | exit=0 |
| smoke-build.log | 冒烟模式全量构建（clean 后重建，含新 TU 全量编译） | exit=0，零 error |

## 测试留痕（gtest XML＋ird-test-report.json＋控制台）

| 目标 | 集成模式 | 冒烟模式 | 结论 |
| --- | --- | --- | --- |
| sdurws_ird_ui_test | sdurws_ird_ui_test.xml ＋ ird-test-report.json ＋ .console.log | *-smoke 同名三件 | 77/77 通过（含 UI-T09 新增 21 用例） |
| sdurws_ird_ui_contract_test | sdurws_ird_ui_contract_test.xml ＋ ird-test-report-contract.json ＋ .console.log | *-smoke 同名三件 | 3/3 通过 |
| sdurws_ird_ui_gui_test | sdurws_ird_ui_gui_test.xml ＋ ird-test-report-gui.json ＋ .console.log | （模型层任务，GUI 套件回归即覆盖） | 25/25 通过 |

GUI 执行口径：QT_QPA_PLATFORM=windows＋QT_QPA_PLATFORM_PLUGIN_PATH 指向 Qt 6.11.1 msvc2022_64 plugins/platforms（本机 Qt 布局，非构建约定变更）。

## 门禁与治理

| 文件 | 内容 | 结论 |
| --- | --- | --- |
| ird_gates-integration.log | 集成树 ird_gates（cmake -P 直跑，UTF-8） | 41 处命中——全部为 DTB §4.5 登记册既有项（与 wp10-t08 基线同型）；本任务新增文件零命中 |
| ird_gates-smoke.log | 冒烟树 ird_gates | 命中 41 项，与集成树一致 |
| gates-hit-set.txt | 本次命中集归一化清单（41 条唯一） | — |
| gates-hit-set-diff.txt | 与 wp10-t08 基线（gates-hit-set.txt 41 条）归一化逐码比对 | HIT-SET-IDENTICAL，零新增零消失 |
| validate-task.log | validate-task.ps1 -TaskFile UI-T09.json | PASS |

## 说明

- ird_gates 目标级失败＝41 处既有登记命中（机器面滞后状态，WP-01-T03 责任面）；
  本任务按 wp10-t08 同款口径以"逐码比对零变化"留痕，不私改门禁词表。
- 需求追溯：UI-STG-1/2（UX-12/UX-01）、UX-02（§3.5/§6.6）、§6.5 汇聚单侧冻结、
  N-11 无第二套门控、O-31/P-UI-1/P-UI-6 处置——详见任务契约 acceptance 与
  units/ui.md §16.7 v1.1 行。
