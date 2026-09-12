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
后续契约接口（Snapshot/Slice/Evidence/Verdict/Envelope/Currentness/
Compatibility/Evaluator）随 §12 的 EV-T03 起逐任务落地。

任务编排、接口冻结前置与实现状态见
doc/industrial-robot-design/traceability/phase-one-readiness.md。

2026-09-12（WP-05-T10≙EV-T10 评估器接口与注册表）：本目录新增 `Evaluator.hpp`
（§9——ThreadSafety 档位＋EvaluationKey＋EvaluatorDescriptor 七字段〔P-EX-7：
不含执行期能力字段〕＋EvaluationRequest/Output＋IEvaluationContext/
IEngineeringEvaluator/IEvaluatorFactory＋EvaluatorRegistry〔注册边界/注册期
校验/manifest 摘要——D-12 不建调度设施〕＋EvidenceProfileRegistry〔§9.5〕＋
RegistrationManifest）；EvaluatorRegistry 实现 IProducerRegistryView、
EvidenceProfileRegistry 实现 IProfileRegistryView（EV-T05/EV-T06 预登记的
视图适配兑现）。单元测试 EV-REG 系 18 用例随 `sdurws_ird_evidence_test`
注册；验证留痕见 traceability/builds/wp05-t10/。剩余契约接口随 §12 的
EV-T11（ScriptedEvaluator 与全量用例体）落地。
