# RPT-T12 schema 冻结留痕（ModelSummary——runtime §13.2 交接凭据）

- 任务：RPT-T12（模型摘要与任务投影接入），分支 wp12-t12，base 0636368285a360ed56df4d869239853319a488f8
- 日期：2026-09-20
- 冻结对象：`RobWork/RobWorkStudio/src/rwslibs/industrialrobot/reporting/include/sdurws/ird/reporting/ModelSummary.hpp`（ModelSummary 结构＋IModelSummaryProvider 接口）
- 冻结性质：**schema 冻结留痕**（reporting.md §11 RPT-T12 卡行完成条件"schema 冻结留痕（runtime 交接）"——本文件即 reporting.md §12.1 runtime.md §13.2 行"已冻结"状态的落地凭据，runtime 侧 IRuntimeModelView→IModelSummaryProvider 适配以此为对接基线）

## 一、schema 与 §9.7 契约原文逐字段核对结论

核对方法：ModelSummary.hpp 与 units/reporting.md §9.7 契约块逐行对照；类型面由契约测试 `contract_test/ModelSummaryTaskStatusCrossUnitContractTest.cpp` 的 static_assert 编译期常驻自证（core::RevisionId/core::ContentIdentity/core::Digest256 类型恒等——P-RPT-9 基线＝core.md v0.1 Draft）。

| §9.7 原文字段行 | ModelSummary.hpp 落地 | 核对结论 |
| --- | --- | --- |
| `core::RevisionId revision;` | `core::RevisionId revision{}` | 一致（static_assert 锁定） |
| `core::ContentIdentity snapshotIdentity;  // 快照身份块` | `core::ContentIdentity snapshotIdentity{}` | 一致（注释行"快照身份块"保留） |
| `core::ContentIdentity modelIdentity;  // runtime 计算（含编译器契约版本——evidence v0.2 口径）` | 同型落地，注释保留 | 一致 |
| `core::ContentIdentity nameMapIdentity;  // RuntimeNameMap 内容身份（CON-06 呈现）` | 同型落地，注释保留 | 一致 |
| `core::ContentIdentity policyContentIdentity;  // 已解析策略内容身份（快照 policyRef 值传递）` | 同型落地，注释保留 | 一致 |
| `std::uint32_t jointCount; bool allRevolute;  // 轴数与全旋转（链型摘要）` | `std::uint32_t jointCount = 0; bool allRevolute = false;` | 一致（默认值＝未提供语义，注释消歧） |
| `std::string installPresetToken;  // Ground/Inverted/Wall/Custom（呈现用，不入身份）` | 同型落地；头内另设禁项注释（不入任何判定——§9.7 维度表非法调用行）＋接口面无以其为键的方法 | 一致 |
| `struct Capabilities { hasDynamicWorkCell, hasFullMassInertia, hasJointVelocityLimits, hasCollisionGeometry, hasTools, hasScene, hasFrictionModel, hasCouplingMatrix; } capabilities;` | 内嵌 `struct Capabilities` 八布尔，字段序即声明序 | 一致（八位逐一，测试 FullFieldRoundtripThroughProvider 逐位断言） |
| `std::vector<ResourceSummary> resources;  // {resourceId, state(Solidified/Recorded), digest}` | `std::vector<ResourceSummary> resources;`＋ResourceSummary{resourceId:string, state:ResourceStateKind{Solidified,Recorded}, digest:core::Digest256} | 一致（词表两态投影——落位偏差③，§14.4 v0.15） |
| `std::vector<ConfigSummary> configurations;  // {configKindToken, contentIdentity}（AnalysisConfiguration 引用）` | ConfigSummary{configKindToken:string, contentIdentity:core::ContentIdentity} | 一致 |
| （原文无注记字段） | `std::string provenanceNote;` | **落位增量②**——§9.7 后置行"＋注记（不伪造）"的 schema 承载位（ConsistencyResult.notes v0.10 同型先例），§14.4 v0.15 登记 |

接口签名核对：`virtual std::optional<ModelSummary> trySummary(core::RevisionId) const = 0;` 与 §9.7 原文逐字一致——static_assert（TrySummaryFn）编译期锁定；P-RPT-2 处置＝架构所有者确认注入为正式形态或补边裁决前签名不变（acceptance 4）。

## 二、后置三支语义（§9.7 前置/后置行——runtime §10.12 口径承接）

1. revision 在案＋快照在案 → 全字段投影，provenanceNote 为空；
2. revision 在案＋快照已释放 → snapshotIdentity 非空（isValid）＋其余身份字段可空（全零保留值）＋provenanceNote 非空注记——**不伪造**（runtime.md §10.12"以归档的快照身份元数据呈现"；P-RPT-9 基线＝runtime.md v0.1 Draft-Structured，冻结 diff 后增量同步）；
3. revision 不在权威闭包 → nullopt（try 轨不抛——§1.4 约定）。

以上三支由 test/ModelSummaryTest.cpp 的 FullFieldRoundtripThroughProvider / ReleasedSnapshotArchivedMetadataWithNote / UnknownRevisionReturnsNullopt 三具名用例承载。

## 三、禁项锁定（§9.7 维度表——acceptance 5）

- 经本接口要求编译/读取 WC/DWC 实例＝禁（runtime §10.12）——返回值为纯值投影，类型层无 WC/DWC 形态；
- installPresetToken 为呈现字段不入任何身份判定（§9.7 维度表）——接口面恰一个方法，无以其为键的查询/判定面（InstallPresetTokenPresentationOnly 用例自证）；
- DH/显式权威模式呈现经 model 章节 modeling 提供方（§9.2 域注册通道），不经本接口——头内禁项注释逐字落位。

## 四、runtime 侧交接义务提示（承接 §12.2 runtime 行）

- L5 适配器：IRuntimeModelView→IModelSummaryProvider（装配要点见 ModelSummary.hpp 头内"L5 适配建议"——适配代码归 L5 装配，本任务零 runtime 编译边）；
- 快照释放时的归档身份元数据供给：后置第 2 支的数据源（归档在案的 snapshotIdentity 等）；
- 本 schema 字段**只增不改**：任何调整＝runtime 交接契约变更，须走 units/reporting.md §14.4 变更记录并同步本留痕。

## 五、验证证据

- 双模式构建零错误；测试两模式 208＋61 全绿（本任务新增单测 12＋契约 4）；构建/测试留痕见本目录（integration-build.log、smoke-build.log、*-integration.xml、*-smoke.xml、ird-test-report*.json）；
- ird_gates 双树直跑命中集归一化 diff 为空（GATES-HIT-SET-IDENTICAL——gates-hit-set-diff.txt）；
- validate-task：PASS。
