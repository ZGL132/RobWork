# WP-13-T21 实施留痕（URDF/Xacro 导入命令流依赖树装配修复——F-473 消账）

分支 `wp13-t21`（基点 redesign-main@1a3c9cd7）；任务契约 `tasks/foundation/WP-13-T21.json`。

## 缺陷与修复

- **缺陷（F-473，major）**：生产命令流 `modeling.import-urdf`/`import-xacro` 的 `ValidatedSource` 只装 bytes＋入口快照、恒缺依赖树；映射器契约要求"含 mesh 引用的文档必须携带同源依赖树"，故任何含 mesh 的 URDF/Xacro 经生产流恒被 `SourceInconsistent` 拒绝（无草稿）。无 mesh 的 URDF 不受影响。域级映射器识别能力完好（黄金全链 21 项映射/6 关节/7 连杆/四清单分层诚实）——缺口纯在流半区装配。
- **修复**：`ModelingCommandFlows.cpp` 新增 `assembleImportDependencyTree`（缺失容忍：存在＝io 真实快照/digest 身份；缺失/不可达/被护栏拦截＝exists=false 叶；package:// 不入树；同键去重＋relPath 字典序），接入 URDF/选链重映射/Xacro 三调用点。装配纪律与 GoldenImportTest V-06/V-08 同源；映射器保持唯一权威（提取遗漏→SourceInconsistent fail-closed）。

## 测试（`tests/`，t＝集成树、s＝冒烟树）

| 套件 | 集成 | 冒烟 |
| --- | --- | --- |
| sdurws_ird_modeling_test | **297/297**（294＋3 新用例，t1） | 239/239（236＋3，s1） |
| sdurws_ird_modeling_gui_test | 9/9（t2） | 9/9（s2） |
| sdurws_ird_modeling_contract_test | 17/17（t3——零新增链接边命中） | — |
| sdurws_ird_ui_test | 239/239（t4） | — |

新增用例（`ModelingCommandFlowsTest.cpp`，临时目录真读盘＋io 真扫描）：
`ImportUrdf_WithMeshAndMissingResource_ProducesDraft_WP13_T21`（修复主证：含 mesh＋缺失叶 URDF 导入成功、资源清单两笔、就绪重算触发）、
`ImportUrdf_PlainNoMesh_ProducesDraft_WP13_T21`（回归守卫）、
`ImportUrdf_ConfirmRejected_KeepsSessionDraft_WP13_T21`（SA-15 取消面首覆盖）。

## 构建证据

- `build-integrated.log`：集成四目标零错误；`smoke-configure.log`/`smoke-build.log`：冒烟冷树（`build-smoke-wp13-t21`）零错误。
- `validate-task` PASS（WP-13-T21）；`validate-docs.log` PASS（465 条 findings 无重号）。

## 诚实边界

- xacro 半区树自展开产物提取、基目录＝入口文档目录：单文件 xacro 精确；include 子目录内 mesh 引用可能标缺失（V-08 警告可见）——多文件精确解析随导入向导任务。
- io snapshot 拒绝（预算/网格护栏拦截）一律按缺失叶入树——exists=true 必须携带真实快照（ResourceNode 契约），误标缺失属诚实降级可见面，不阻断不伪存在。
- 跨分支登记注：本批 F-473 与 ui-t43 批次 F-472 并行编号（各自顺延 F-471），合并时序不影响唯一性。
