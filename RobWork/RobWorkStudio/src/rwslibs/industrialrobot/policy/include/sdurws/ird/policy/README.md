# policy

本目录为 policy 单元公共头保留位（命名空间 sdurws/ird/policy）。
源码随对应任务卡（见 doc/industrial-robot-design/units/policy.md §12）落地；本文件不参与编译。

2026-09-11（WP-07-T01≙POL-T01 构建落位）：`sdurws_ird_policy` 已由
INTERFACE 占位升级为 STATIC 库（C++17，链 core；集成模式另链 L1 基线库
sdurw_math/sdurw_kinematics/sdurw_models/sdurw_proximity——R-5 proximity
直链唯一许可方；目标名与别名 `RWS::ird::policy` 不变）；gtest 测试目标
`sdurws_ird_policy_test` 与红线扫描用例（policy/test/BuildRedLineTest.cpp）
已注册。

2026-09-13～14（WP-07-T02~T11≙POL-T02~T11 逐任务落位；2026-09-14
WP-07-T12≙POL-T12 同步）：公共头现有十一个模块——Errors.hpp（POL-T02：
PolicyErrorCode 错误码枚举全表 19 值＋PolicyError 异常＋registryCode 建议码
映射）、PolicySet.hpp（POL-T02：§4 EngineeringPolicySet 数据模型——碰撞
规则/关节阈值/适用范围/构造后不可变＋发布门 make）、PolicyInput.hpp
（POL-T03：RawPolicyInput＋PolicySchema 常量＋PolicyCodec 编码/解码/语义
投影/contentIdentity）、PolicyParsing.hpp（POL-T04：resolvePolicy 七段解析
管线＋IPolicyValidator/IPolicyValidationContext）、Contexts.hpp（POL-T05：
§3.3/§9.7 三个最小注入接口 IPolicyNameContext/IPolicyBytesSource/
IPolicyCallContext——只定义接口零适配器，适配器归 L5/ui 装配）、
PolicyPort.hpp（POL-T05：§9.1 ④端口 IPolicyProvider＋PolicyProvider
(policyObject, ContentVersion) 记忆化实现＋CollisionBackendDescriptor）、
CollisionEvaluator.hpp（POL-T06：§6.1/§6.4/§7.1/§7.2 场景模型＋会话构建期
作用域展开＋CollisionEvaluationSession＋ICollisionEvaluator 唯一实现入口＋
makeRobWorkCollisionEvaluator 唯一构造入口）、CollisionQuery.hpp（POL-T07：
§6.2 查询与输出类型＋SingleState/PathSequence/SampleSet 三形态评估）、
JointLimits.hpp（POL-T08：§9.4 关节限位与行程阈值评估契约＋唯一产品实现）、
Compatibility.hpp（POL-T09：§8.1/§8.2 checkPolicyCompatibility 兼容判定＋
IPolicyCompatibilityChecker）、Diagnostics.hpp（POL-T10：§9.6 PolicyDiagCode
建议码全表＋policyDiagCode 码表＋IPolicyDiagnostics＋PolicyDiagnostics＋
makeComparative 辅助组）。消费 rw 头的模块其 TU 仅集成模式编译（CMake
TARGET sdurw_kinematics 条件 gating——冒烟模式无任何 TU include 框架头；
头部本体零 rw include 或仅值成员 include，详见各头文件头契约注释）；
跨单元契约测试目标 `sdurws_ird_policy_contract_test`（POL-T11 注册，链接
testkit）与规范替身（policy/test/，POL-TD-1 边界声明）见 test/README.md。
实现落位登记见 units/policy.md §15.4（v0.3~v0.12＝POL-T02~T11）；验证留痕
见 traceability/builds/wp07-t01~t11/（POL-T11 口径：集成 223 例、冒烟
139 例，全通过）。POL-T12（文档与门禁同步）已完成本 README 与
units/policy.md §12/§15.4 现状的指向核对（§9→§12 修正复审维持）与零偏差
登记（十一模块逐头与 include/ 目录实证比对，证据
traceability/builds/wp07-t12/README-audit.md）。

任务编排、接口冻结前置与实现状态见
doc/industrial-robot-design/traceability/phase-one-readiness.md；实现验收
记录见 doc/industrial-robot-design/traceability/acceptance/。
