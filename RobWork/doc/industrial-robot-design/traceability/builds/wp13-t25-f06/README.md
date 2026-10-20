# WP-13-T25 F06 批证据——外部 WorkCell 反向导入正式命令入口

任务：WP-13-T25 第 1 项（F06／审计 M14／MDL-18／AT-33）。
基线：f873984b（分支 wp13-t25 第 1 提交前工作树）。
日期：2026-10-20。执行：ZCode 旁支会话（独立 worktree C:/zcode-wt/wp13-t25）。

## 缺口与实现

- 缺口（红证锚定）：提取引擎 mapWorkCellXml/extractWorkCellDevice 可用（WP-13-T17），但正式命令目录 11 条无 import-workcell——用户无法从可见入口选择外部 .wc.xml 生成可编辑草稿。
- 实现：PanelCommandCatalog 第十二号 `modeling.import-workcell`（Project/写路径）＋ModelingCommandFlows 流程（loadWorkCellEntry：io openGenericXml 良构校验＋readFileBytes 真实字节＋两窗口 SHA-256 摘要对账；assembleWorkCellDependencyTree：Polytope 缺失容忍树；mapWorkCellXml→多设备显式选链→confirmImport 逐项报告→恰一根草稿替换）＋isAssembledModelingCommand 词表＋UiText title/tooltip 两键＋单元卡 §9.7.3 表行/v0.55＋DTB §2.14 WP-13-T25 行/v0.91。

## 红证（先红后绿——失败来自产品缺口）

| 文件 | 内容 |
| --- | --- |
| red-standalone-modeling.{xml,log} | 5 项流测试全红（unknown domain command id modeling.import-workcell）——独立树 f873984b+红测试态 |
| red-integration-gui.{xml,log} | WorkCellImportCommandButtonRegistered 红（button==nullptr，"正式面板缺少 modeling.import-workcell 命令按钮（审计 M14）"）——集成树 |

## 绿证（实现后）

| 文件 | 口径 | 结果 |
| --- | --- | --- |
| green-standalone-modeling.{xml,log} | 独立树 sdurws_ird_modeling_test 全量 | 337 通过/0 失败/0 跳过 |
| green-standalone-modeling-contract.{xml,log} | 独立树 contract | 8/0/0 |
| green-integration-modeling.{xml,log} | 集成树全量 | 413/0/0 |
| green-integration-modeling-contract.{xml,log} | 集成树 contract | 17/0/0 |
| green-integration-modeling-gui.{xml,log} | 集成树 GUI 全量（QT_QPA_PLATFORM=windows，无外部 smoke 变量） | 61/0/0（含新增 2 例） |
| green-integration-ui.{xml,log} | 集成树 ui 全量（PATH 前置 Miniconda3——python313.dll 前置） | 326/0/0 |
| gate-all.log | gate-all 全量（-PythonDir D:/software/Miniconda3） | 91/99；4 失败定位见下 |

GUI 截图：gui-shots/workcell-import-report.png（真实报告对话框承载设备名/映射/未保留项）。

## gate-all 失败逐项定位（基线对照）

| 目标 | 失败用例 | 归因 | 基线证据 |
| --- | --- | --- | --- |
| sdurws_ird_modeling_gui_test | WorkCellRealFileDialogToReportCancelSafe（本批新增例） | ctest 上下文无 IRD_UI_PLUGIN_SMOKE→原生对话框不可自动化；已改为用例内 qputenv 自备并复验 ctest＋直跑双绿 | 修复后 ctest 100% 通过＋直跑 61/61 |
| sdurws_ird_ui_test | CrashCapture.Emit*（"files 2≠1"） | gate-all 高负载时段瞬态；同二进制事后 ctest 100%＋直跑 CrashCapture 5/5（baseline-ui-crashcapture-pass.log） | 独立树 267/267＋集成直跑 326/326 |
| sdurws_ird_optimization_contract_test | ApplierContractFixture.TwoStepApplyCreatesBranchAndRevision_WP20T07_ACC2（OPT-VER-145 断言） | **基线 f873984b 固有失败**（stash 全部本批改动后以基线二进制复现，与本批零关联——本批未触 project/optimization） | baseline-optimization-contract-fail.log |
| sdurws_ird_project_perf_test | GoldenProjectOpen.HotStartP95_Within3s（实测 P95≈3730ms>3000ms） | **基线固有性能预算缺口**（stash 态复现；本 worktree/盘环境量级，与本批零关联） | baseline-project-perf-fail.log |

## 四层状态

- 数据：PASS（真实临时 .wc.xml 单/双设备/缺资源/非 WC 根/从动关节面未入本批流断言——引擎面已有 ImportTest 覆盖；deg→rad 数值独立核对）。
- Qt 交互：PASS（真文件对话框→真报告→取消零替换；报告对话框内容承载断言＋截图）。
- 正式宿主：PASS（命令按钮注册/文案/只读门控/装配集对账——UI-T43 生产装配序 GUI 用例）。
- 真实三维：NOT RUN（本项不含三维消费面——草稿替换后三维呈现归既有面板刷新链，AT-33 不要求）。

## 环境口径

集成树 C:/zcode-wt/build-wp13-t25（vcpkg toolchain＋Qt 6.11.1 前缀，RWS_BUILD_INDUSTRIALROBOT=ON grep 确认）；独立树 C:/zcode-wt/build-wp13-t25-standalone（同两参数）。GUI：QT_QPA_PLATFORM=windows、绝对路径、单 GUI 进程。python313.dll 前置＝D:/software/Miniconda3（PATH）。

## 框架 RobWorkSim 全新树编译失败（基线固有——重要登记）

- 现象：全新集成树全量构建中 **RobWorkSim 框架目标**（sdurwsim_gui／bt_plugin.rwplugin）编译失败——Math.hpp min/max 宏冲突族（C2059/C2143/C4003），148 处 error；**industrialrobot 全部产品目标与 RobWorkStudio 主链零错误**。
- 归因（stash 对照，决定性）：**全部本批改动 stash 后（纯 f873984b 基线源）同一项目同错误复现**（baseline-framework-sdurwsim-build-fail.log＝148 error；postchange-framework-build-fail.log＝含改动态）。本批改动未触碰 RobWorkSim 及其 include 闭包任何文件——与 F06 零关联。
- 机理推测：主任务 worktree 同源该 obj 于 2026-10-18 20:02 编译成功（build 产物在案）；本机 10-19/10-20 间疑有 Windows SDK/工具链更新使全新编译暴露框架潜伏 include 顺序问题（windows.h min/max 宏）。既有树靠陈旧产物免重编，全新树必现。
- 处置：SA-02 框架零源码修改——本批不修（修复需先登记 patches/PATCHES.md）；按 findings 口径上报（同 F-907 环境类先例），建议登记为待所有者裁决项。**"集成模式零错误"以产品目标（industrialrobot＋RobWorkStudio 链）口径达成；框架目标失败如实呈报不粉饰。**
- verify-task 影响：契约 verify 第 1 条（全量构建）在该命令面上因框架目标失败——非本批引入（对照证据如上）。
