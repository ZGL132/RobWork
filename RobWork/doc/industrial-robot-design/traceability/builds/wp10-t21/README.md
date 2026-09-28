# WP-10-T21（≙UI-T21）验证留痕

任务：工业项目树和选择服务（方案 B.1 迁移链，SA-18 D3/D4；契约 tasks/foundation/UI-T21.json）
分支/基线：ui-t21（base＝redesign-main@a1f91eeaeec751f60979fe762093e56937c69205）；流水线 tick#408 领取
日期：2026-09-28

## 构建结论（双模式）

| 模式 | 命令面 | 结论 |
| --- | --- | --- |
| 集成模式 | `cmake --build build --config Release --target sdurws_ird_ui sdurws_ird_ui_plugin sdurws_ird_ui_app sdurws_ird_ui_test sdurws_ird_ui_contract_test sdurws_ird_ui_gui_test sdurws_ird_studio`（缓存 RWS_BUILD_INDUSTRIALROBOT:BOOL=ON 已 grep 确认） | 七目标零错误（integration-build-ui.log） |
| 独立冒烟 | 全树构建于 build_smoke_t21（`cmake -S RobWork/RobWorkStudio/src/rwslibs/industrialrobot -B build_smoke_t21 -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE=<repo>/vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_PREFIX_PATH=D:/software/QT/6.11.1/msvc2022_64`） | 零错误（smoke-configure.log／smoke-build.log） |

## 测试结论（gtest XML＋ird-test-report.json＋控制台日志三件留痕，本目录）

| 目标 | 集成树计数 | 冒烟树计数 | 基线对照 |
| --- | --- | --- | --- |
| sdurws_ird_ui_test | 213/213 PASS | 213/213 PASS | 175→213（＋SelectionService 23 例＋IndustrialProjectTreeModel 15 例） |
| sdurws_ird_ui_contract_test | 23/23 PASS | 22/22 PASS | 基线不减（本任务零跨单元契约面变更——协作全部经既有 O-31 端口形态；冒烟树 22 与基线一致） |
| sdurws_ird_ui_gui_test | 46/46 PASS | 46/46 PASS | 40→46（＋IndustrialProjectTreeGui 6 例） |
| ctest `-L "^ird$"`（ui 叶子目录） | 2/2 PASS | —（契约 verify 口径为集成树） | — |

GUI 运行环境：QT_QPA_PLATFORM=windows＋QT_PLUGIN_PATH=<Qt>/plugins。

## ird_gates 结论（base↔head 引擎直跑归一化比对）

- 引擎直跑（WP-24-T08/WP-10-T20 留痕同款方法）：head（ui-t21 工作树）与 base
  （a1f91eea detached worktree `build/ird-t21-base-wt`，采集后已移除）各跑
  `cmake -DIRD_ROOT=<源根> -P cmake/ird_gates.cmake`，命中行归一化（去
  `[ird_gates] ` 前缀＋去 IRD_ROOT 路径前缀＋排序去重）后 **base 92 条 ↔ head 93
  条——恰增 1 条**：
  `IRD-GATE-R3: ui 产品面疑似包含 Qt 类头（Q+大写约定）：ui/src/IndustrialProjectTree.cpp`
  （NFR-MNT-01 ui 例外类既定形态——面板 TU 含 `<QTreeWidget>` 类头包含；既有 15 个
  ui 产品面 TU 同族在册；SelectionService.cpp 零 Qt 零命中。登记提交件
  `traceability/wp10-t21-gate-registrations.md`，责任方 WP-01-T03）。
- 比对件：ird-gates-{head,base}.log＋ird-gates-{base,head}-norm.txt＋ird-gates-hitset-diff.txt（本目录）。
- 如实登记：构建期 ird_gates 自定义目标即红（93 处存量命中判失败）为 **F-019 登记的存量
  事实**（TK-T03 起），base 端同样即红——非本任务引入。

## 文档与门禁校验

- `pwsh -File RobWork/scripts/industrialrobot/validate-docs.ps1`：PASS（20 units，
  12 trace entries，202 task files）。
- `pwsh -File RobWork/scripts/industrialrobot/validate-task.ps1 <UI-T21 契约路径>`：PASS。

## 文件清单

| 文件 | 内容 |
| --- | --- |
| integration-build-ui.log | 集成模式构建日志（七目标全过零错误） |
| smoke-configure.log / smoke-build.log | 冒烟树配置/全树构建日志（零错误） |
| gtest-ui-{model,contract,gui}.xml ＋ [-smoke 变体] | gtest XML（集成/冒烟双树） |
| ird-test-report*.json | ird-test-report 六件（集成 model/contract/gui＋冒烟 model/contract/gui） |
| console-ui-*.log | 控制台运行日志六件 |
| ctest-ui-ird.log | ctest `^ird$` 标签命令日志 |
| ird-gates-*.log/-norm.txt/hitset-diff.txt | 门禁双端引擎日志＋归一命中集＋diff |
