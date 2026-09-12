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
