# 缺陷修复批A（交互域）验证留痕（FIX-T1）

| 字段 | 值 |
| --- | --- |
| 批次 | FIX-T1（缺陷修复批A——ui/requirements/modeling 交互域 open 缺陷） |
| 日期 | 2026-10-12 |
| 分支 | pipeline3/redesign-main（worktree build/pipeline-wt3，集成构建树 build/——RWS_BUILD_INDUSTRIALROBOT=ON＋vcpkg toolchain＋Qt prefix，AGENTS §4.1 口径） |
| 基点 | origin/redesign-main @ a82f58bc（立卡提交 3e540267 前的远程最新） |

## 批面收敛结论（立卡核实冻结）

- **F-800（major，modeling）真修复**：六轴重置不清 defaultTcp——复现测试 `StructureEditTest.ResetToSixAxis_ClearsDefaultTcp_WithChainRefs` 修复前真实红（defaultTcp 残留断言失败）→修复（StructureEdit.cpp 链引用面清空同拍 `defaultTcp.reset()`）→绿。
- **F-799（major，modeling）核实翻牌＋回归钉**：立卡所据 logic_error 逃逸已由 F-746①（audit-r2d1 批 0e7cb3111，行程面转值面）顺带消除；本批补混合场景回归钉 `MdlCommandPrepare.MixedBadBoundsRevoluteAndUnconfirmedContinuous_RejectsOnValueTrack`（红阶段如实登记：缺陷已不在，直接绿）。Revolute 非法限位载荷在 decode 层 fail-fast，回归钉取合法限位 Revolute 保证行程校验段必达。
- **F-460（blocking，requirements）核实翻牌**：audit-r2c 批已修复（实现 2fe3da66f＋验收 pass a17df640a＋合入 c028c550d）而账面漏翻；本批复现实证绿（AppliedReceiptRewiresWorksetRefs_SecondApplyExplicit_F460 等三用例），findings 翻 fixed。
- **F-495（blocking，ui）核实登记**：UI-T64/T65 均已验收 pass 合入基线；本批复跑 ui_test 三 UI_T65 用例绿，数据链在位实证；status 维持 open（宿主 GUI 复验归所有者，不代翻）。

## 受影响单元回归（fresh worktree 集成树实跑）

| 套件 | 结果 | 留痕 |
| --- | --- | --- |
| sdurws_ird_modeling_test | 375/375 PASS（含新增 2 用例） | xml/modeling-test.xml＋reports/ird-test-report-modeling_test.json＋modeling-test.log |
| sdurws_ird_requirements_test | 全 PASS（exit 0） | xml/requirements-test.xml＋reports/…requirements_test.json＋requirements-test.log |
| sdurws_ird_ui_test | 全 PASS（exit 0） | xml/ui-test.xml＋reports/…ui_test.json＋ui-test.log |
| sdurws_ird_modeling_gui_test | 42/42 PASS | xml/modeling_gui.xml＋reports/…modeling_gui_test.json＋modeling_gui.log |
| sdurws_ird_requirements_gui_test | 全 PASS（exit 0） | xml/requirements_gui.xml＋reports/…requirements_gui_test.json |
| sdurws_ird_ui_gui_test | 全 PASS（exit 0） | xml/ui_gui.xml＋reports/…ui_gui_test.json |

运行时前置（DTB §5.1 fresh 行实跑口径）：①先构建 sdurws_ird_studio 使框架 DLL 复制入 bin/Release；②PATH 补 vcpkg installed/x64-windows/bin；③PATH 补 Miniconda3（python313.dll——ui_test 依赖链）。

## 门禁与文档校验

- ird_gates：全部检查通过（R-1/R-2/R-3/R-4/R-5/T-1/T-2/SUB/GRAPH/LIB/SA02 零命中，exit 0）——build/ird-gates.log（构建树内，未入库）。
- validate-docs：PASS（20 units, 12 trace entries, 317 task files）。
- validate-task（FIX-T1.json）：PASS。

## 边界自查

- 批内触达文件：modeling/src/StructureEdit.cpp＋modeling/test/{StructureEditTest,CommandHandlersTest}.cpp＋traceability/findings.json＋DTB＋FIX-T1.json＋本留痕目录——零越界。
- project/io/diagnostics/runtime/selection/trajectory/drivetrain/reporting 零 diff（并行路线 B 与非交互域避让）；RT-T15/UI-T78 契约与 F-550~F-553 零触碰；mesh 真实网格升级不碰。
