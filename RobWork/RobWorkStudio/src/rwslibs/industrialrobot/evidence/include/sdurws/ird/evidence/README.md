# evidence

本目录为 evidence 单元公共头（命名空间 sdurws/ird/evidence）。
源码随对应任务卡（见 doc/industrial-robot-design/units/evidence.md §12）落地；本文件不参与编译。

2026-09-11（WP-05-T01≙EV-T01 构建落位）：`sdurws_ird_evidence` 已由
INTERFACE 占位升级为 STATIC 库（C++17，仅链 core——目标名与别名
`RWS::ird::evidence` 不变）；gtest 测试目标 `sdurws_ird_evidence_test` 与
红线扫描用例（evidence/test/BuildRedLineTest.cpp）已注册。验证留痕见
traceability/builds/wp05-t01/。

2026-09-12（WP-05-T02≙EV-T02 错误与依赖类型）：本目录新增首批公共头——
`Errors.hpp`（EvidenceErrorCode 9 值全表稳定 token＋registryCode 建议码
＋EvidenceError 异常轨，§3.1 组成表/§13 建议码清单）与 `Dependency.hpp`
（§4.2.1 七类依赖载荷值类型＋DependencyKey 语法闸门＋声明闭包/条目语法
校验器——header-only，契约产物列无 .cpp）。单元测试 EV-ERR/EV-DEP 18 用例
随 `sdurws_ird_evidence_test` 注册；验证留痕见 traceability/builds/wp05-t02/。

2026-09-12（WP-05-T03~T09≙EV-T03~T09 契约落位）：公共头按 §12 依赖序增至
十模块——`Snapshot.hpp`（§4.1 快照 builder/冻结校验/SnapshotCodec
refs-only＋materialized）、`Slice.hpp`（§4.2/§4.3/§5 InputSlice/
SliceBuilder/SliceCodec/双层身份 sliceId＋inputBaselineId）、
`Evidence.hpp`（§6.1～§6.3/§6.6 证据项/清单/Profile/证明/搜索未果/覆盖
证据校验）、`Verdict.hpp`（§6.4～§6.6 五级汇总判定/VerdictTrace/资格/
基准检查）、`Envelope.hpp`（§7 ResultEnvelope 构造校验 make/
validateCombination）、`Currentness.hpp`（§8.1 当前性纯投影
computeCurrentness/CurrentnessIndex）、`Compatibility.hpp`（§8.2
judgeCacheHit/judgeCheckpointCompatibility）；src/ 侧对应实现落地
（Dependency.hpp header-only 无 .cpp，其余九头各有同名 .cpp）。
EvidenceErrorCode 表尾追加 SliceIncomplete（EV-T04，9→10）；
EvaluatorDescriptorInvalid/ProfileDuplicate/ProfileInvalid 随 EV-T10 再
追加（10→13——现值 13，EV-T12 实测）；EV-T07 另注册跨单元契约测试目标
`sdurws_ird_evidence_contract_test`。验证留痕见
traceability/builds/wp05-t03~t09/。

2026-09-12（WP-05-T10≙EV-T10 评估器接口与注册表）：本目录新增 `Evaluator.hpp`
（§9——ThreadSafety 档位＋EvaluationKey＋EvaluatorDescriptor 七字段〔P-EX-7：
不含执行期能力字段〕＋EvaluationRequest/Output＋IEvaluationContext/
IEngineeringEvaluator/IEvaluatorFactory＋EvaluatorRegistry〔注册边界/注册期
校验/manifest 摘要——D-12 不建调度设施〕＋EvidenceProfileRegistry〔§9.5〕＋
RegistrationManifest）；EvaluatorRegistry 实现 IProducerRegistryView、
EvidenceProfileRegistry 实现 IProfileRegistryView（EV-T05/EV-T06 预登记的
视图适配兑现）。单元测试 EV-REG 系 18 用例随 `sdurws_ird_evidence_test`
注册；验证留痕见 traceability/builds/wp05-t10/。

2026-09-13（WP-05-T11≙EV-T11 测试替身与契约套件＋WP-05-T12≙EV-T12 同步）：
EV-T11 交付面在测试侧，公共头无新增——ScriptedEvaluator 规范替身与 EV-*
全量用例体（evidence/test/，替身边界声明见 evidence/test/README.md §1）、
envelope 访问器特化（evidence/test/EnvelopeAccessorContractTest.cpp，消费
sdurws_ird_testkit——仅测试目标链接）与切片契约夹具数据集
（testdata/golden/ev-slice-fixture/1.0.0）。EV-T12 已完成本 README 与
units/evidence.md §3.1 组成表/§12 任务行的指向核对与零偏差登记：include/
目录实测公共头十模块＋src/ 九实现，落位版本链 §15.4 v0.3~v1.2＝
EV-T02~T11；当前测试口径 `sdurws_ird_evidence_test` 205 例＋
`sdurws_ird_evidence_contract_test` 14 例，集成/冒烟两模式实跑全绿
（逐头比对与执行证据 traceability/builds/wp05-t12/README-audit.md）。

任务编排、接口冻结前置与实现状态见
doc/industrial-robot-design/traceability/phase-one-readiness.md；实现落位
登记见 units/evidence.md §15.4，实现验收记录见
doc/industrial-robot-design/traceability/acceptance/。
