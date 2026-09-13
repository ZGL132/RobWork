# policy 测试面说明（§11 测试设施与替身边界声明）

本文是 `sdurws_ird_policy_test` / `sdurws_ird_policy_contract_test` 两个测试
目标的说明面：§1 为替身边界声明全文（POL-TD-1 的文书面载体），§2 为规范
替身清单，§3 为 units/policy.md §11 验证矩阵→测试文件落位映射（ev 前例：
evidence/test/README.md 同款三段结构）。

---

## 1. 替身边界声明（POL-TD-1，全文）

**policy 单元的全部测试替身——无论脚本化后端（ScriptedCollisionBackend /
ScriptedDistanceBackend）、脚本化评估器（ScriptedCollisionEvaluator）、④端口
替身（StubPolicyProvider）、注入面替身（ScriptedNameContext /
ScriptedValidationContext / ScriptedBytesSource / ScriptedCallContext）——其
脚本产出（碰撞/距离应答值、预设解析结果、异常注入、取消脚本）都只是
契约形态数据，仅用于验证评估状态机、稳定排序、作用域过滤、端口契约与
消费方逻辑，不构成任何碰撞算法正确性证明。**

真实碰撞数值正确性由两条独立通道承担：

1. 内置后端（ProximityStrategyRW）对程序化构造场景（已知相交/分离几何）
   的解析算例——POL-T07 交付于 CollisionEvaluationTest.cpp；
2. `testdata/golden/pol-collision-analytic`（analytic-case 类黄金数据集，
   手工可推导的立方体解析几何，POL-T11 交付）——碰撞对象对与手算一致
   的解析算例对照（POL-AT-4 观测点载体）。

替身约束（机检面＝`PolicyTestDoubleBoundary` 用例，ev 前例 EV-REG-3 同款
双面声明）：

- 替身全部类型只存在于测试面（`policy/test/`），产品库源码面
  （`src/`＋`include/`）替身符号零命中；
- 脚本描述符的 backendId 一律带 `test.` 前缀、toleranceModel 显式标注
  `not-a-real-backend`——替身数据不得进入任何产品路径，不得被解读为
  复现要素主张；
- 脚本实测值（距离/碰撞应答）不是几何真值——依赖其"数值正确性"的
  断言只能指向判定逻辑（状态机/边界决策表），不得指向几何算法。

## 2. 规范替身清单（§11 命名设施；header-only，
## `test/sdurws/ird/policy/testdouble/PolicyTestDoubles.hpp`）

| 替身 | 实现接口 | 脚本面 | 用途 |
| --- | --- | --- | --- |
| `ScriptedCollisionEvaluator` | `ICollisionEvaluator` | 碰撞/距离 FIFO 应答、rw 异常注入、脚本描述符 | 消费方（业务域/契约套件）取得真实会话（作用域/状态机/稳定排序走产品代码），几何应答脚本化——预设 findings/Failed/Canceled/异常四轨 |
| `StubPolicyProvider` | `IPolicyProvider` | 预设 PolicyResolution、脚本抛错、预设描述符、评估器注入槽 | ④端口消费方契约演练；实例同一性断言（POL-SHARE-1）；装配分步（PortAssemblyIncomplete）镜像 |
| `ScriptedNameContext` | `IPolicyNameContext` | 双向名称表＋映射身份 | 会话构建期名称解析 |
| `ScriptedValidationContext` | `IPolicyValidationContext` | 对象存在/角色/组定义应答表 | 解析④⑤步闭包查询 |
| `ScriptedBytesSource` | `IPolicyBytesSource` | (对象， 版本)→字节表 | ④端口真实解析链取数 |
| `ScriptedCallContext` | `IPolicyCallContext` | 取消阈值脚本＋存活标志 | POL-EVAL-6 取消/POL-LATE-1 迟到 |
| `ScriptedCollisionBackend` / `ScriptedDistanceBackend` | rw `CollisionStrategy`/`DistanceStrategy` | FIFO 应答＋异常注入 | 上述评估器替身的几何应答承载（TU 内同型类见 CollisionEvaluationTest.cpp——POL-T07 既有验收面零改动） |

线程约束：全部替身为单线程设施（并发面归 POL-CONC-1 的真实后端＋互斥
承载）。确定性：脚本按构造序回放，无时间/随机源。

## 3. §11 验证矩阵→测试文件落位映射

| §11 组 | 落位（目标＋文件） | 状态 |
| --- | --- | --- |
| POL-ID-1~5 | `sdurws_ird_policy_test` / PolicyInputTest.cpp、PolicyPortTest.cpp（POL-ID-1 缓存一致） | POL-T03/T05 交付 |
| POL-PARSE-1~6 | `sdurws_ird_policy_test` / PolicyParsingTest.cpp | POL-T04 交付 |
| POL-SCOPE-1/2 | `sdurws_ird_policy_test` / CollisionSessionTest.cpp（构建期）＋CollisionEvaluationTest.cpp（运行期 POL-SCOPE-2 行） | POL-T06/T07 交付 |
| POL-EVAL-1~9、POL-EXC-1、POL-CONC-1、POL-LATE-1 | `sdurws_ird_policy_test` / CollisionEvaluationTest.cpp | POL-T07 交付 |
| POL-JNT-1/2 | `sdurws_ird_policy_test` / JointLimitsTest.cpp | POL-T08 交付 |
| POL-COMPAT-1/2 | `sdurws_ird_policy_test` / CompatibilityTest.cpp（POL-COMPAT-2 的 evidence 联动素材面另见 `_contract_test` / PolicyProofMaterialContractTest.cpp） | POL-T09 交付＋POL-T11 补契约面 |
| POL-SHARE-1 | `sdurws_ird_policy_contract_test` / PolicyContractDoublesTest.cpp | POL-T11 交付 |
| POL-AT-1（AT-01 观测点） | `sdurws_ird_policy_contract_test` / PolicyContractDoublesTest.cpp（替身 project 处理器走查 §10.4 全链） | POL-T11 交付 |
| POL-AT-2（AT-19 观测点） | `sdurws_ird_policy_contract_test` / PolicyContractDoublesTest.cpp（三入口替身消费者） | POL-T11 交付 |
| POL-AT-3（AT-27 观测点） | `sdurws_ird_policy_contract_test` / PolicyContractDoublesTest.cpp（显示单位切换＋分析配置替身） | POL-T11 交付 |
| POL-AT-4（AT-37 观测点） | `sdurws_ird_policy_contract_test` / CollisionAnalyticDatasetTest.cpp（倒挂算例对照 analytic-case 数据集） | POL-T11 交付 |
| POL-EVAL-3 契约夹具（evidence 证明字段形状对齐） | `sdurws_ird_policy_contract_test` / PolicyProofMaterialContractTest.cpp | POL-T11 交付 |
| POL-TD-1（替身边界） | 文书面＝本 README §1；机检面＝`sdurws_ird_policy_contract_test` / PolicyTestDoubleBoundaryTest.cpp（产品源码替身符号零命中扫描＋声明在案断言） | POL-T11 交付 |
| 解析/编码契约夹具数据集 | `sdurws_ird_policy_contract_test` / PolicyCodecFixtureContractTest.cpp（testdata/golden/pol-policy-codec-fixture 消费——双实现互证） | POL-T11 交付 |
| 诊断/码表/构建红线 | `sdurws_ird_policy_test` / DiagnosticsTest.cpp（POL-T10）、BuildRedLineTest.cpp、PolicySetTest.cpp | POL-T01/T02/T10 交付 |
