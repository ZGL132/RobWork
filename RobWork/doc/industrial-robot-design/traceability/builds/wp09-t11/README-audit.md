# DIAG-T11 README 指向核对与零偏差登记证据（acceptance 2＋acceptance 1 佐证）

- 日期：2026-09-15
- 对象：`RobWork/RobWorkStudio/src/rwslibs/industrialrobot/diagnostics/include/sdurws/ird/diagnostics/README.md`
- 方法：include/ 目录实测清单 ↔ units/diagnostics.md §3.1 模块表 ↔ README 记载逐头比对；指向核对（任务卡章节引用＝§11）；实现状态段与 units/diagnostics.md §14.4 v0.3~v0.11、traceability/builds/wp09-t02~t11 现状比对。
- 先例口径：CORE-T10（core.md v0.11）/RT-T13（traceability/builds/wp06-t13/README-audit.md）/EV-T12（traceability/builds/wp05-t12/README-audit.md）同款审计证据格式。

## §1 指向核对（§11 维持）

- README 任务卡指向＝`doc/industrial-robot-design/units/diagnostics.md` **§11**（阶段 A 实现任务拆分）——2026-09-10（diagnostics.md v0.1 变更记录行）由"§9"修正为"§11"的修正**复审维持**，无回退、无第二指向。
- §11 DIAG-T11 行原文"README 指向核对（§9→§11 已随本文修正）"与本核对结论一致。核对结果：**零偏差**。
- 本任务对 README 的同步＝落位现状刷新（§3 记载的过期前瞻句消除），指向本体不变。

## §2 include/ 目录实测清单（2026-09-15 DIAG-T11 实施段执行 `ls` 实录）

```
Aggregation.hpp
Catalog.hpp
Confirmable.hpp
CrashReport.hpp
DiagCodes.hpp
Errors.hpp
Factory.hpp
Logging.hpp
README.md
Redaction.hpp
```

契约头（.hpp）计 9 个＋README.md＝**10 文件**，与 §3.1"10 个公共头模块＋1 个编译实现"的表行数一致（表内 10 行＝9 契约头行＋README.md 行——§3.1 将 README.md 保留位说明列于同一"头"表）。

## §3 §3.1 模块表 ↔ 实测目录逐行比对

| # | §3.1 表行 | 磁盘实存 | 落位登记 | 结论 |
| --- | --- | --- | --- | --- |
| 1 | Errors.hpp | 存在 | DIAG-T03（§14.4 v0.4） | 一致 |
| 2 | DiagCodes.hpp | 存在 | DIAG-T03（§14.4 v0.4） | 一致 |
| 3 | Catalog.hpp | 存在 | DIAG-T04（v0.5）＋DIAG-T09 扩展（v0.10） | 一致 |
| 4 | Factory.hpp | 存在 | DIAG-T04（§14.4 v0.5） | 一致 |
| 5 | Confirmable.hpp | 存在 | DIAG-T05（§14.4 v0.6） | 一致 |
| 6 | Aggregation.hpp | 存在 | DIAG-T06（§14.4 v0.7） | 一致 |
| 7 | Logging.hpp | 存在 | DIAG-T07（§14.4 v0.8） | 一致 |
| 8 | Redaction.hpp | 存在 | DIAG-T08（§14.4 v0.9） | 一致 |
| 9 | CrashReport.hpp | 存在 | DIAG-T08（§14.4 v0.9） | 一致 |
| 10 | README.md（保留位说明，不参与编译） | 存在 | 本文件（DIAG-T11 同步） | 一致 |

反向核对：磁盘 10 文件全部被 §3.1 表覆盖，无遗漏、无多列；§1.2 卡头"10 个契约头随 DIAG-T03+ 落地"的计数口径即本表 10 行——**零偏差**（9 编译契约头＋1 保留位说明）。

## §4 src/ 与 test/ 侧清单比对（acceptance 1 佐证）

| 侧 | 磁盘实测（2026-09-15） | §14.4 各落位登记 | 结论 |
| --- | --- | --- | --- |
| src/ 编译实现 | 11 个 .cpp（Aggregation/Catalog/Confirmable/CrashReport/DiagCodes/Diagnostics/Errors/Factory/Logging/ParamSchema/Redaction）＋2 个私有头（EntryDetail.hpp/ParamSchema.hpp——不入 include/，R-2 合规） | T02 锚点 Diagnostics.cpp（v0.3）；T03 Errors/DiagCodes（v0.4）；T04 Catalog/Factory/ParamSchema（v0.5，paramSchema 解析单点自 DiagCodes.cpp 迁入）；T05 Confirmable（v0.6）；T06 Aggregation（v0.7）；T07 Logging（v0.8）；T08 Redaction/CrashReport（v0.9）；T09 无新增翻译单元（v0.10）；T10 无新增（v0.11） | 一致（11＝1＋2＋3＋1＋1＋1＋2） |
| test/ 用例体 | 14 个测试 .cpp＋ScriptedDiagSource.hpp 替身（仅头，实现内联于头文件）＋test/README.md＝16 文件 | T02 BuildRedLineTest＋LinkageContractTest〔落位期契约用例〕（v0.3）；T03 StableCodeRegistryTest（v0.4）；T04 CatalogFactoryTest（v0.5）；T05 ConfirmableTest＋ConfirmableProjectStubTest（v0.6）；T06 AggregationTest（v0.7）；T07 LoggingTest（v0.8）；T08 RedactionTest（v0.9）；T09 CatalogLifecycleTest＋CatalogHostTraceStubTest（v0.10）；T10 ScriptedDiagSource.hpp＋ScriptedDiagSourceTest＋DiagContractCheckTest＋CollaborationQueryTest（v0.11） | 一致（14＝2＋1＋1＋2＋1＋1＋1＋2＋3） |

### §4.1 本审计发现并转登记的卡内措辞偏差（不静默——DTB §5.4）

- §1.2 卡头构建落位行"**已随 DIAG-T10 落位**：`_contract_test` 增列 `test/ScriptedDiagSource.hpp/.cpp` 对"——磁盘实测与 §14.4 v0.11 登记均为**仅头替身**（`ScriptedDiagSource.hpp`，产出逻辑内联于头文件；git 全历史无 `ScriptedDiagSource.cpp`，DIAG-T10 实现提交 a71e42ee 仅增 hpp＋ScriptedDiagSourceTest.cpp 两文件）。"hpp/.cpp 对"为卡头措辞 slips，非实现偏差（§14.4 v0.11 权威记录与实现一致）——本任务按 DTB §5.4 以卡头措辞更正＋v0.12 变更记录行登记，不静默。

## §5 实现状态段比对基准（README 同步的对照源）

| README（同步后）表述 | 对照源 | 结论 |
| --- | --- | --- |
| 九个契约头模块清单（本审计 §3 逐行） | units/diagnostics.md §3.1 表＋include/ 实测 | 一致 |
| 落位登记 §14.4 v0.3~v0.11＝DIAG-T02~T10 | units/diagnostics.md §14.4 变更记录行 | 一致 |
| 验证留痕 builds/wp09-t02~t11 | traceability/builds/ 目录实测（wp09-t02…wp09-t11 齐全） | 一致 |
| 两模式 115/115＋26/26 | 本目录 integration-gtest-*.xml 与 smoke-gtest-*.xml（本任务实跑：115 ran / 115 passed＋26 ran / 26 passed，两模式同构） | 一致 |
| 测试体落位表见 ../test/README.md | diagnostics/test/README.md §3（§10 矩阵→文件落位全表 DIAG-T02~T10 累积）实存 | 一致 |
| 同步前 README 过期句"本文不表示库已实现或测试已通过"（2026-09-10 前瞻口径） | 与 §14.4 v0.3~v0.11（T02~T10 已落位、测试已通过并留痕）矛盾——本任务消除该过期前瞻句，改为"实现与测试通过状态以 units/diagnostics.md §14.4 与上述留痕为准"的如实指向 | 偏差已消除（EV-T12"过期前瞻句消除"同款） |

## §6 结论

- 指向核对：**零偏差**（§11 维持）。
- 清单核对：include/ 10 文件、src/ 11 .cpp＋2 私有头、test/ 16 文件与 §3.1/§14.4 登记逐项一致，**零偏差**（§4.1 一处卡头措辞 slips 已按 DTB §5.4 转登记更正）。
- 状态核对：README 同步后与卡 §14.4、builds/ 留痕、双模式复证实跑逐数一致，**零偏差**（同步前 1 处过期前瞻句已消除并登记于本审计 §5）。
