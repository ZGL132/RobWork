# runtime

本目录为 runtime 单元公共头保留位（命名空间 sdurws/ird/runtime）。
源码随对应任务卡（见 doc/industrial-robot-design/units/runtime.md §12）落地；本文件不参与编译。

2026-09-12（WP-06-T01~T08≙RT-T01~T08 落位）：`sdurws_ird_runtime` 已由
INTERFACE 占位升级为 STATIC 库（C++17，链 core；集成模式另链 L1 基线库
sdurw_kinematics/sdurw_models/sdursim——目标名与别名 `RWS::ird::runtime`
不变）；gtest 测试目标 `sdurws_ird_runtime_test` 已注册。
2026-09-12（RT-T09~T12 落位＋RT-T13 同步）：公共头现有十二个模块——
Errors.hpp（错误码全表＋RuntimeError＋Expected 两态）、Sources.hpp
（注入最小契约：IObjectBytesSource/IRevisionClosureSource＋robot-design
token）、Description.hpp（中性输入值类型＋IRobotDesignReader 注入接口）、
Resource.hpp（资源引用值类型＋ResourceBytes/ResourceReadError/
IRuntimeResourceProvider）、CanonicalModel.hpp（§4.3 规范模型＋
RuntimeCapability＋builder 不变量）、Codec.hpp（RT-Codec 三形态＋IRDNAME
映射编码）、NameMap.hpp（§7 名称双射映射＋kNameMapRuleVersion）、
BaseWorldTransform.hpp（§6 基座—世界变换规则＋kBaseWorldRuleVersion）、
Adapter.hpp（WorkCell/DWC 只读视图＋RobWork 异常转译＋
RobWorkBaselineVersion）、Snapshot.hpp（§9.1～§9.3/§9.5 快照/视图实现/
工厂/物化）、Compiler.hpp（§10.0 编译契约面＋§5.1 十段链 compile() 事务
入口）、CacheKey.hpp（§9.4 编译缓存分层键＋IRuntimeCompileCacheKey 纯
判定）。src/ 侧编译器链（WorkCellCompiler、DynamicWorkCellCompiler、
Snapshot、CompilerImpl 等，单元私有头）与消费真实框架类的测试仅集成模式
编译（TARGET sdurw_kinematics 条件增列——冒烟模式无框架库可链；纯值
契约头两模式均可用）。实现落位登记见 units/runtime.md §15.4
（v0.4~v0.14＝RT-T03~T12）；验证留痕见 traceability/builds/wp06-t01~t12/
（RT-T12 口径：集成 261 例＝260 通过＋1 例 worker 占位按设计跳过、冒烟
176/176）。RT-T13（文档与门禁同步）已完成本 README 与 §12/§15.4 现状的
指向核对（§9→§12 修正复审维持）与零偏差登记（十二模块逐头与 include/
目录实证比对，证据 traceability/builds/wp06-t13/README-audit.md）。

任务编排、接口冻结前置与实现状态见
doc/industrial-robot-design/traceability/phase-one-readiness.md；实现验收
记录见 doc/industrial-robot-design/traceability/acceptance/。
