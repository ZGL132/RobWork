# WP-05-T12（EV-T12）evidence 公共头 README 指向核对与零偏差审计

| 字段 | 值 |
| --- | --- |
| 任务 | EV-T12 文档与门禁同步（≙WP-05-T12，evidence 单元）acceptance 1『README 核对零偏差』 |
| 日期 | 2026-09-13 |
| 分支 | `wp05-t12`（base 1a8b9cb471b77d79e177d4cbe515b38755811a1a） |
| 对象 | `RobWorkStudio/src/rwslibs/industrialrobot/evidence/include/sdurws/ird/evidence/README.md` |
| 比对基准 | `units/evidence.md` §3.1 组成表、§12 任务行、§15.4 落位版本链（v0.3~v1.2＝EV-T02~T11）＋磁盘实测 |
| 性质 | 指向核对＋目录实录＋逐头比对——本任务零代码变更，审计结论＝**零偏差**（同步后） |

---

## §1 任务卡指向核对

| README 指向 | 实测 | 结论 |
| --- | --- | --- |
| "任务卡（见 doc/industrial-robot-design/units/evidence.md §12）" | evidence.md §12＝"阶段 A 实现任务拆分"（EV-T01~T12 任务行所在章） | 一致 |
| "任务编排、接口冻结前置与实现状态见 traceability/phase-one-readiness.md" | 文件存在，evidence 行＝"已编写 v0.1 / Draft-Structured / contract-review / 12 任务 / foundation/" | 一致 |
| "实现落位登记见 units/evidence.md §15.4" | §15.4 变更记录表存在，v0.3~v1.2 逐任务落位登记在案 | 一致 |
| "实现验收记录见 traceability/acceptance/" | EV-T01~EV-T11 验收记录文件在档 | 一致 |
| 留痕路径 traceability/builds/wp05-t01/、wp05-t02/、wp05-t10/（历史条目）与 wp05-t03~t09/（新条目） | builds/ 目录实测 wp05-t01~wp05-t11 全部存在 | 一致 |

## §2 include/ 目录实录（2026-09-13 实测）

`evidence/include/sdurws/ird/evidence/`（11 项＝10 公共头＋本 README）：

```text
Compatibility.hpp  Currentness.hpp  Dependency.hpp  Envelope.hpp  Errors.hpp
Evaluator.hpp      Evidence.hpp     README.md       Slice.hpp     Snapshot.hpp
Verdict.hpp
```

`evidence/src/`（9 实现）：Compatibility.cpp、Currentness.cpp、Envelope.cpp、
Errors.cpp、Evaluator.cpp、Evidence.cpp、Slice.cpp、Snapshot.cpp、Verdict.cpp
（Dependency.hpp header-only 无 .cpp——与 §12 EV-T02 行"契约产物列无 .cpp"一致）。

无多余文件、无缺失文件。

## §3 十头逐头比对（README 声明 ↔ 磁盘/单元卡）

| # | 头 | README 声明（落位任务/设计锚点） | 磁盘与卡实测 | 结论 |
| --- | --- | --- | --- | --- |
| 1 | Errors.hpp | EV-T02；EvidenceErrorCode 全表＋EvidenceError（§3.1/§13）；9 值为 T02 时点值 | 存在；枚举现值 13（SnapshotIncomplete/SnapshotIntegrity/DeclarationInvalid/EvidenceMissing/ProofInvalid/CaseCoverageMissing/EnvelopeIllegalCombination/CacheIncompatible/EvaluatorDuplicate/SliceIncomplete/EvaluatorDescriptorInvalid/ProfileDuplicate/ProfileInvalid）——SliceIncomplete（EV-T04，9→10）与 EvaluatorDescriptorInvalid/ProfileDuplicate/ProfileInvalid（EV-T10，10→13）表尾追加已在新增条目登记 | 一致 |
| 2 | Dependency.hpp | EV-T02；header-only；七类依赖载荷＋DependencyKey＋校验器（§4.2） | 存在；src/ 无同名 .cpp | 一致 |
| 3 | Snapshot.hpp | EV-T03；SnapshotBuilder/SnapshotCodec refs-only＋materialized（§4.1） | 存在；src/Snapshot.cpp 在 | 一致 |
| 4 | Slice.hpp | EV-T04；InputSlice/SliceBuilder/SliceCodec/双层身份（§4.2/§4.3/§5） | 存在；src/Slice.cpp 在 | 一致 |
| 5 | Evidence.hpp | EV-T05；证据项/清单/Profile/证明/覆盖校验（§6.1~§6.3/§6.6） | 存在；src/Evidence.cpp 在 | 一致 |
| 6 | Verdict.hpp | EV-T06；五级决策表/VerdictTrace/资格/基准（§6.4~§6.6） | 存在；src/Verdict.cpp 在 | 一致 |
| 7 | Envelope.hpp | EV-T07；make/validateCombination（§7） | 存在；src/Envelope.cpp 在 | 一致 |
| 8 | Currentness.hpp | EV-T08；computeCurrentness/CurrentnessIndex（§8.1） | 存在；src/Currentness.cpp 在 | 一致 |
| 9 | Compatibility.hpp | EV-T09；judgeCacheHit/judgeCheckpointCompatibility（§8.2） | 存在；src/Compatibility.cpp 在 | 一致 |
| 10 | Evaluator.hpp | EV-T10；descriptor/两注册表/manifest（§9）；两注册表实现 IProducerRegistryView/IProfileRegistryView | 存在；Evaluator.hpp 第 489 行 `class EvidenceProfileRegistry final : public IProfileRegistryView`、IProducerRegistryView 同款兑现；src/Evaluator.cpp 在 | 一致 |

## §4 状态段比对基准（测试实跑口径，EV-T12 本任务实跑复证）

| README 声明 | 实跑实测 | 结论 |
| --- | --- | --- |
| `sdurws_ird_evidence_test` 205 例 | 集成模式 205/205 通过；冒烟模式 205/205 通过（XML：builds/wp05-t12/sdurws_ird_evidence_test-{integration,smoke}.xml） | 一致 |
| `sdurws_ird_evidence_contract_test` 14 例 | 集成模式 14/14 通过；冒烟模式 14/14 通过（XML：builds/wp05-t12/sdurws_ird_evidence_contract_test-{integration,smoke}.xml） | 一致 |
| 与单元卡 §15.4 v1.2（EV-T11）登记口径 205/205＋14/14 | 逐数相同 | 一致 |
| 落位版本链 v0.3~v1.2＝EV-T02~T11 | §15.4 表实测逐行对应 | 一致 |

历史条目数值（T02"18 用例"、T10"EV-REG 系 18 用例"、T01 目标注册）均为落位时点事实，
与各 §15.4 落位登记行（v0.3/v1.1/v0.2 前）逐数一致，不作现时值解读。

## §5 EV-T11 交付面位置核对（测试侧，公共头无新增声明）

| 交付物 | 实测位置 | 结论 |
| --- | --- | --- |
| ScriptedEvaluator 替身与 EV-* 用例体 | evidence/test/EvidenceTestDoubles.hpp＋EvaluatorPortSuiteTest.cpp 等（test/ 目录 16 文件实测在档） | 一致 |
| 替身边界声明（EV-REG-3） | evidence/test/README.md 在档 | 一致 |
| envelope 访问器特化 | evidence/test/EnvelopeAccessorContractTest.cpp 在档 | 一致 |
| 切片契约夹具数据集 | testdata/golden/ev-slice-fixture/1.0.0/（inputs/expected/generate/manifest.json）在档 | 一致 |

## §6 结论

同步后 README 与磁盘实现、单元卡 §3.1/§12/§15.4 现状**零偏差**。本次同步消除的
三处过期表述（T02 条目尾"随 EV-T03 起逐任务落地"前瞻句、T10 条目尾"剩余契约
接口随 EV-T11 落地"句——T11 已落地且交付面在测试侧、缺 T03~T09 落位登记段），
已随新条目归零；历史条目数值保留落位时点口径并经 §4 与落位登记对账。
