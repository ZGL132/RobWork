# RPT-T13 README 核对与零偏差登记审计（2026-09-20）

任务：RPT-T13（reporting 文档与门禁同步）；分支 wp12-t13；基线 3012df850b15d61b75e1ce1421d83f0556668a4b。
本文为契约 acceptance 1（README 核对）与 acceptance 4（RPT-T01~T12 零偏差登记）的核对证据正本；登记行＝units/reporting.md §14.4 v0.16。

## 1. 核对对象与总结论

| 对象 | 路径 |
| --- | --- |
| README 正本 | `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/reporting/include/sdurws/ird/reporting/README.md` |
| 单元卡现文 | `RobWork/doc/industrial-robot-design/units/reporting.md`（v0.15→本任务 v0.16） |

**总结论：指向核对零偏差（无更正项）；实现状态段已同步；"不参与编译/不表示已实现"声明保留；RPT-T01~T12 零未登记偏差。**

## 2. 指向三方一致性核对（acceptance 1）

| # | 核对项 | 实测 | 结论 |
| --- | --- | --- | --- |
| 1 | README 指向语句 | "源码随对应任务卡（见 doc/industrial-robot-design/units/reporting.md §11）落地"（README 第 4 行） | 指向 §11 |
| 2 | 单元卡 §3.1 README 行现文 | "既有保留位说明（不参与编译；指向本文 §11〔任务拆分〕——v0.4 RPT-T01 复核更正……；RPT-T13 复核〔2026-09-20〕指向核对零偏差……）" | 载明指向 §11，与 README 一致 |
| 3 | 单元卡 §11 节真实存在 | `## 11. 阶段 A/B/C 实现任务拆分`（reporting.md 行 1331；含 RPT-T13 治理行） | 指向目标真实存在，零悬空 |
| 4 | 前置事实复审（RPT-T01 acceptance 4，traceability/builds/wp12-t01/ird-test-report.json notes） | "README 现文指向 §11……卡 v0.1 所载'已指向 §9'与磁盘不符，已随 v0.4 更正" | 复审通过：无回退、无第二指向 |
| 5 | 其余指向面 | README 对 phase-one-readiness.md、traceability/builds/wp12-t01/ 的引用 | 目标均存在，零悬空 |

## 3. 实现状态段同步（README 2026-09-20 段）

- 目录现状实测：`include/sdurws/ird/reporting/`＝**13 契约头＋README.md**（Errors/Identity/ReportModel/Sections/SectionProvider/Builder/Render/Consistency/Export/Archive/Bundle/ModelSummary/TaskStatusSource——与 §3.1 组成表逐行对应）；`src/`＝12 翻译单元＋2 私有头（ReportCodec.hpp/RenderText.hpp）。
- README 原 2026-09-19 段"§3.1 契约头随 RPT-T02+ 落地"前瞻句已被 2026-09-20 段取代（历史段保留不改写——文件按日期追加惯例）。
- **声明保留**："本文件不参与编译""本目录与库目标不表示公共契约已实现或测试已通过——实现与测试通过状态以 units/reporting.md §14.4 变更记录与 builds 留痕为准"。

## 4. 零偏差清单核对（acceptance 4——RPT-T01~T12 逐任务）

| 面 | 磁盘实测 | 与登记比对 | 结论 |
| --- | --- | --- | --- |
| include/ | 14 文件＝13 契约头＋README.md | §3.1 组成表 13 头＋README 行逐行对应；13 头落地序列＝RPT-T02~T12 累计（2＋1＋2＋1＋1＋1＋1＋2＋1＋1；SectionProvider/ModelSummary/TaskStatusSource 三头 header-only——各卡产物列形态） | 一致 |
| src/ | 12 .cpp＋2 私有头 | 累加 1（T01 锚点）＋2（T02）＋2（T03）＋1（T04）＋1（T05）＋1（T06）＋1（T07）＋2（T09）＋1（T10）＝12；私有头 ReportCodec.hpp（v0.6）/RenderText.hpp（v0.10 等价重构）——R-2 不出 include/ | 一致 |
| test/ | 14 测试 .cpp＋5 替身/夹具 .hpp＋README.md | 与 v0.4~v0.14 各登记一致（ErrorsIdentity/ReportModel/Sections/SectionProvider/Builder/Render/Consistency/ArchiveExport/Bundle/ModelSummary/TaskStatusSource/BuildRedLine/ReportingContractSuite/TestMainReport＋FakeArtifactSink/FakeArchiveWriter/ReportingScenario/ScriptedResultSource/ScriptedSectionProvider＋替身边界声明正本 README） | 一致 |
| contract_test/ | 10 测试 .cpp＋TestMainReport.cpp | Linkage/Identity/Evidence/Section/Builder/Render/Consistency/Archive/Bundle/ModelSummaryTaskStatus 十文件与 v0.4~v0.15 各登记一致 | 一致 |
| 测试复证 | 集成 208/208＋61/61；冒烟 208/208＋61/61 | wp12-t12 验收基线 208＋61 逐数一致，零新增用例（纯文档任务）——ird-test-report 四份＋四份 gtest XML 同目录 | 零回归 |
| 门禁复证 | ird_gates 双树直跑 | 命中集与 wp12-t12 验收基线全等零新增（gates-hit-set-diff.txt——GATES-HIT-SET-IDENTICAL） | 零新增命中 |

### 偏差登记面核查（实现偏差是否全部按 DTB §5.4 就地登记）

| 版本 | 登记偏差 | 登记状态 |
| --- | --- | --- |
| v0.6 | SectionStatus/RenderHint/条目冻结视图族暂载 ReportModel.hpp；ReportLevelRule 注入 | 已登记（§14.4 v0.6） |
| v0.8 | IReportResultSource 增补 eligibilityOf/tryReproduction 两方法 | 已登记（§14.4 v0.8） |
| v0.9 | io 注入契约族暂载 Render.hpp；entryKey 增列；覆盖附表零行；限定语两词推导面等 | 已登记（§14.4 v0.9） |
| v0.10 | ConsistencyResult.notes 增补；比对维度覆盖冻结；提取器失败语义边界 | 已登记（§14.4 v0.10） |
| v0.12 | IReportPublishedIndex/IReportResolver/取消全空形态/withEvidenceBundle Usage/只读探测/DiskFull 可用侧六项 | 已登记（§14.4 v0.12） |
| v0.13 | 放弃路径 RAII 澄清/条目编址/totalDigest 规范域/reproduction.json 形状/状态词面 | 已登记（§14.4 v0.13） |
| v0.15 | ITaskStatusSource 独立成头/provenanceNote/两投影词表/archivePhase 不投影四项 | 已登记（§14.4 v0.15） |

**结论：RPT-T01~T12 实现与卡行逐任务零未登记偏差（已登记偏差全部在案，均零契约语义变更）。**

## 5. §1.2"构建骨架"行处置说明

§1.2 表为"上游与磁盘现状登记（2026-09-10 实测）"历史快照，卡头注记（第 3 行）已声明"历史磁盘调查仅表示编写时事实"。其"README 引用本文 §9（实测一致）"为 v0.1 编写时记载——v0.4 已在两处**现行口径载体**（卡头构建落位行＋§3.1 README 行）更正；本次不改写快照表（保持"编写时事实"口径完整），在 §14.4 v0.16 行如实登记该处置。

## 6. 门禁与验证留痕（同目录）

validate-docs.log（契约 verify 命令）、validate-task.log（RPT-T01~T13 十三份契约三查）、gates-ird-gates-integration-tree.log / gates-ird-gates-smoke-tree.log / gates-hit-set-diff.txt、smoke-configure.log / smoke-build.log、integration-build.log、四份 ird-test-report*.json＋四份 gtest XML、integration/smoke-test-run-{unit,contract}.log。
