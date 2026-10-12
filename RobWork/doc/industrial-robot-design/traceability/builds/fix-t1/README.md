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

**留痕构成更正（attempt 1 验收 B-1 返工——2026-10-12）**：首次实施提交 25a7f92a 的六套件运行未产出 gtest XML（`--gtest_output=xml:<MSYS 风格路径>` 的 `/d/...` 前缀不被 Windows 运行时解析，XML 静默未产出），而 README/提交正文声称"XML 落库"＝申报失实——attempt 1 验收（acc/fix-t1/1 @78fc038b）如实判 fail。本返工轮以 Windows 风格路径（`xml:D:/...`）真实重跑六套件，XML 与 ird-test-report JSON 同次运行产出、全部落库 xml/ 与 reports/；下表"结果"列以返工轮 XML 为准。

| 套件 | 结果（返工轮 XML 实证） | 留痕 |
| --- | --- | --- |
| sdurws_ird_modeling_test | 375/375 PASS（failures=0，含新增 2 用例） | xml/modeling_test.xml＋reports/ird-test-report-modeling_test.json＋modeling_test.log |
| sdurws_ird_requirements_test | 267 用例全 PASS（failures=0；含环境条件 skip） | xml/requirements_test.xml＋reports/…requirements_test.json＋requirements_test.log |
| sdurws_ird_ui_test | 214 用例全 PASS（failures=0；含环境条件 skip） | xml/ui_test.xml＋reports/…ui_test.json＋ui_test.log |
| sdurws_ird_modeling_gui_test | 42 用例（41 过＋1 环境条件 skip——F-620 剪贴板面，failures=0） | xml/modeling_gui_test.xml＋reports/…modeling_gui_test.json＋modeling_gui_test.log |
| sdurws_ird_requirements_gui_test | 47/47 PASS（failures=0） | xml/requirements_gui_test.xml＋reports/…requirements_gui_test.json |
| sdurws_ird_ui_gui_test | 81 用例（80 过＋1 环境条件 skip，failures=0） | xml/ui_gui_test.xml＋reports/…ui_gui_test.json |

（attempt 1 验收者亲跑的独立口径：requirements 213+1 skip／ui 267 全过／ui_gui 80+1 skip——与返工轮同绿，skip 均为环境条件面非缺陷。）

运行时前置（DTB §5.1 fresh 行实跑口径）：①先构建 sdurws_ird_studio 使框架 DLL 复制入 bin/Release；②PATH 补 vcpkg installed/x64-windows/bin；③PATH 补 Miniconda3（python313.dll——ui_test 依赖链）。

## 门禁与文档校验

- ird_gates：全部检查通过（R-1/R-2/R-3/R-4/R-5/T-1/T-2/SUB/GRAPH/LIB/SA02 零命中，exit 0）——build/ird-gates.log（构建树内，未入库）。
- validate-docs：PASS（20 units, 12 trace entries, 317 task files）。
- validate-task（FIX-T1.json）：PASS。

## 边界自查

- 批内触达文件：modeling/src/StructureEdit.cpp＋modeling/test/{StructureEditTest,CommandHandlersTest}.cpp＋traceability/findings.json＋DTB＋FIX-T1.json＋本留痕目录——零越界。
- project/io/diagnostics/runtime/selection/trajectory/drivetrain/reporting 零 diff（并行路线 B 与非交互域避让）；RT-T15/UI-T78 契约与 F-550~F-553 零触碰；mesh 真实网格升级不碰。
