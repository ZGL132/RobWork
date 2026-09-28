# WP-10-T20（≙UI-T20）验证留痕

任务：宿主运行时发布桥（方案 B.1 迁移链，SA-18 D10 消费侧；契约 tasks/foundation/UI-T20.json）
分支/基线：ui-t20（base＝redesign-main@564b80748e1eaa5354ad22cf02cf85abf04d2ad7）；流水线 tick#407 领取
日期：2026-09-28

## 构建结论（双模式）

| 模式 | 命令面 | 结论 |
| --- | --- | --- |
| 集成模式 | `cmake --build build --config Release --target sdurws_ird_ui sdurws_ird_ui_plugin sdurws_ird_ui_app sdurws_ird_ui_test sdurws_ird_ui_contract_test sdurws_ird_ui_gui_test`（缓存 RWS_BUILD_INDUSTRIALROBOT:BOOL=ON 已 grep 确认） | 六目标零错误（integration-build-ui-lib.log 为产品库代表件） |
| 独立冒烟 | 全树构建于 build_smoke_t20（`cmake -S RobWork/RobWorkStudio/src/rwslibs/industrialrobot -B build_smoke_t20 -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE=<repo>/vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_PREFIX_PATH=D:/software/QT/6.11.1/msvc2022_64`） | 零错误（smoke-configure.log／smoke-build.log） |

## 测试结论（gtest XML＋ird-test-report.json＋控制台日志三件留痕，本目录）

| 目标 | 集成树计数 | 冒烟树计数 | 基线对照 |
| --- | --- | --- | --- |
| sdurws_ird_ui_test | 175/175 PASS | 175/175 PASS | 158→175（＋RuntimePublishBridge 17 例） |
| sdurws_ird_ui_contract_test | 23/23 PASS | 22/22 PASS | 22→23 集成（＋PresentationBridgeContract 1 例——集成模式专属增列，冒烟树不注册如实呈现 22） |
| sdurws_ird_ui_gui_test | 40/40 PASS | 40/40 PASS | 基线不减（本任务零 GUI 触碰） |
| ctest `-L "^ird$"`（ui 叶子目录） | 2/2 PASS | —（契约 verify 口径为集成树） | — |

GUI 运行环境：QT_QPA_PLATFORM=windows＋QT_PLUGIN_PATH=<Qt>/plugins。

## ird_gates 结论（base↔head 引擎直跑归一化比对）

- 引擎直跑（WP-24-T08 留痕同款方法）：head（ui-t20 工作树）与 base（564b8074 detached
  worktree `build/ird-t20-base-wt`）各跑 `cmake -DIRD_ROOT=<源根> -P cmake/ird_gates.cmake`，
  命中行归一化（去 `[ird_gates] ` 前缀＋去 IRD_ROOT 路径前缀＋排序去重）后
  **base 78 条 ↔ head 79 条——恰增 1 条**：
  `IRD-GATE-SUB: 测试目标 sdurws_ird_ui_contract_test 直链他单元产品目标 sdurws_ird_runtime`
  （O-31 测试面既定形态，UI-T14 同型；登记提交件 `traceability/wp10-t20-gate-registrations.md`，
  责任方 WP-01-T03）。
- 比对件：ird-gates-{head,base}.log＋ird-gates-{base,head}-norm.txt＋ird-gates-hitset-diff.txt（本目录）。
- 如实登记：构建期 ird_gates 自定义目标即红（79 处存量命中判失败）为 **F-019 登记的存量
  事实**（TK-T03 起），base 端同样即红——非本任务引入。

## 文档与门禁校验

- `pwsh -File RobWork/scripts/industrialrobot/validate-docs.ps1`：PASS（20 units，
  12 trace entries，202 task files）。

## 文件清单

| 文件 | 内容 |
| --- | --- |
| integration-build-ui-lib.log | 集成模式产品库构建日志（六目标全过零错误） |
| smoke-configure.log / smoke-build.log | 冒烟树配置/全树构建日志（零错误） |
| gtest-ui-{model,contract,gui}.xml ＋ [-smoke 变体] | gtest XML（集成/冒烟双树） |
| ird-test-report*.json | ird-test-report 五件（集成 model/contract/gui＋冒烟 model/contract/gui） |
| console-ui-*.log | 控制台运行日志六件 |
| ctest-ui-ird.log | ctest `^ird$` 标签命令日志 |
| ird-gates-*.log/-norm.txt/hitset-diff.txt | 门禁双端引擎日志＋归一命中集＋diff |
