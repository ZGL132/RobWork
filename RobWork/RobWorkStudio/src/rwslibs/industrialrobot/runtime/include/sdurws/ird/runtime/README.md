# runtime

本目录为 runtime 单元公共头保留位（命名空间 sdurws/ird/runtime）。
源码随对应任务卡（见 doc/industrial-robot-design/units/runtime.md §12）落地；本文件不参与编译。

2026-09-12（WP-06-T01~T08≙RT-T01~T08 落位）：`sdurws_ird_runtime` 已由
INTERFACE 占位升级为 STATIC 库（C++17，链 core；集成模式另链 L1 基线库
sdurw_kinematics/sdurw_models/sdursim——目标名与别名 `RWS::ird::runtime`
不变）；gtest 测试目标 `sdurws_ird_runtime_test` 已注册。公共头现有九个
模块——Errors.hpp（错误码全表＋RuntimeError＋Expected 两态）、Sources.hpp
（注入最小契约）、Description.hpp（中性输入值类型）、Resource.hpp（资源
引用值类型）、CanonicalModel.hpp（§4.3 规范模型）、Codec.hpp（RT-Codec
三形态）、NameMap.hpp（§7 名称双射映射）、BaseWorldTransform.hpp（§6
基座—世界变换规则）、Adapter.hpp（WorkCell/DWC 只读视图＋RobWork 异常
转译）。S6/S7 编译器（src/WorkCellCompiler、src/DynamicWorkCellCompiler，
单元私有头）与对应测试仅集成模式编译（TARGET sdurw_kinematics 条件增列
——冒烟模式无框架库可链）。实现落位登记见 units/runtime.md §15.4
（v0.4~v0.9＝RT-T03~T08）；验证留痕见 traceability/builds/wp06-t01~t08/
（RT-T08 口径：集成 199/199、冒烟 172/172）；RT-T09 起任务未落地。

任务编排、接口冻结前置与实现状态见
doc/industrial-robot-design/traceability/phase-one-readiness.md；实现验收
记录见 doc/industrial-robot-design/traceability/acceptance/。
