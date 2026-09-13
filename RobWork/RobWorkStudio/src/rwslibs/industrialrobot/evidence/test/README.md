# evidence 单元测试设施说明与替身边界声明（EV-T11）

> 单元：evidence（WP-05）。本文件是 `evidence/test/` 的测试文档入口：
> §1 为 **EV-REG-3 替身边界声明**（units/evidence.md §11 EV-REG-3 行的
> "测试文档显式声明"落点，文档留痕随本仓库版本化）；§2 为替身清单与
> 编程面；§3 为 §11 验证矩阵 → 测试文件落位全表（EV-T02～T11 逐任务
> 累积）。

---

## 1. EV-REG-3 替身边界声明（全文）

**替身输出仅验证 evidence 契约，不构成 IK/动力学/碰撞检测等业务算法正确性
证明；替身数据不得冒充真实证据。**

1. **替身只覆盖注入接口与测试设施位**。evidence 的产品依赖边界
   （units/evidence.md §3.2/§3.3——唯一单元边为 core）决定了跨单元协作
   经最小只读接口消费：修订闭包来源（`IRevisionClosureSource`）、当前性
   依赖事实源（`ICurrentnessFactSource`）、产生者注册表投影
   （`IProducerRegistryView`）、Profile 注册表投影（`IProfileRegistryView`）、
   评估调用上下文（`IEvaluationContext`）。测试替身（§2 清单）仅实现这些
   接口与 §11 点名的可控评估器位——它们是 project/policy/runtime/execution/
   各业务域职责在阶段 A 的测试投影；真实实现分别归各单元（阶段 B/C）与
   L5 装配。
2. **ScriptedEvaluator 的脚本是契约形态数据**。评估器返回的"证据项/证明/
   搜索未果记录/Must-Should 违例"均为按 §6 契约形状构造的测试输入，仅用于
   验证 evidence 的评估器端口（§9.3 调用约定）、汇总决策表（§6.4）、构造
   边界（§7.1）与当前性投影（§8.1）——**其中不存在任何真实的运动学求解、
   动力学计算或碰撞检测**；脚本产出对这些算法的正确性零证明力。
3. **被测产品对象不做替身**。SnapshotBuilder/SliceBuilder/各校验器/
   aggregateVerdict/computeCurrentness/judgeCacheHit/judgeCheckpoint-
   Compatibility/EvaluatorRegistry/ResultEnvelope::make 一律使用产品实现
   （库面 `sdurws_ird_evidence`）；ScriptedEvaluator 经真实
   `EvaluatorRegistry` 注册（注册期校验全过）并经 `create()` 取得——替身
   是"合规被调方"样本，注册/发现/调用路径全部走产品代码。
4. **替身数据不进入任何持久形态**。全部用例在测试进程内存中构造与断言；
   不写归档、不写缓存、不进 manifest（"真实证据生成不得以替身数据冒充"
   ——units/evidence.md §13 接入顺序约束行）。产品源码面（src/＋include/）
   零替身符号，由 `EvaluatorPortSuiteTest.ScriptedDoubleBoundary.*` 用例
   机检钉住（EV-REG-3 的机制面，与本文件文字声明互为两面）。
5. **未执行不得标注通过**（§12 完成条件原文）：每个测试执行后留痕（gtest
   XML＋ird-test-report.json，见
   `doc/industrial-robot-design/traceability/builds/wp05-t11/`）；任何未在
   留痕中出现的用例不得在验收材料中标注通过。

## 2. 替身清单（各测试文件自持＋`EvidenceTestDoubles.hpp` 规范落位）

| §11 名称 | 适配接口/落位 | 编程面 | 消费用例（示例） |
| --- | --- | --- | --- |
| `ScriptedEvaluator`（可控评估器替身——§11 点名） | `IEngineeringEvaluator`（EvidenceTestDoubles.hpp，WP-05-T11） | 脚本步骤回放（预设产出/EvidenceError 抛出/取消抛出）；请求记录、回放计数；进度上报与对象读取演练 | EvaluatorPortSuiteTest（EV-VER-1~5/8、EV-VER-6 双例、上下文契约） |
| `ScriptedEvaluatorFactory` | `IEvaluatorFactory`（同上） | 描述符持有；`create()` 产独立实例（脚本全量副本） | 同上（经真实 `EvaluatorRegistry` 注册） |
| `ScriptedEvaluationContext`（宿主上下文替身） | `IEvaluationContext`（同上） | 取消标志置位；进度记录；预置对象字节表＋读取计数 | EvaluatorPortSuiteTest（取消抛出/对象读取/进度） |
| `AcceptAllClosureSource`（修订闭包放行） | `IRevisionClosureSource`（各测试文件自持） | 恒 true——快照组装协议非被测面 | VerdictTest、EnvelopeTest、CurrentnessTest 等 |
| `ScriptedProducerRegistry`（产生者表投影） | `IProducerRegistryView`（VerdictTest/EnvelopeTest 自持） | 固定 (键→契约版本) 表 | VerdictTest（validateProof 查询面） |
| `ScriptedProfileRegistry`（Profile 表投影） | `IProfileRegistryView`（VerdictTest 自持） | (profileId,version)→Profile | VerdictTest（aggregateVerdict 查询面） |
| `MapFactSource`（当前性事实源） | `ICurrentnessFactSource`（CurrentnessTest/EvaluatorPortSuiteTest 自持） | 声明键→预置条目表＋调用计数 | CurrentnessTest（EV-CUR-1~4）、EvaluatorPortSuiteTest（EV-INV 子例） |
| `FixtureClosureSource`（数据集闭包放行） | `IRevisionClosureSource`（SliceFixtureContractTest 自持） | 恒 true——数据集描述内的闭包事实 | SliceFixtureContractTest（数据集重建） |

补充设施：`EvaluatorRegistry`/`EvidenceProfileRegistry` 为**真实产品注册表**
（非替身——§11"注册于测试内 registry"指测试内**实例**，注册期校验路径
全走产品代码）；testkit 的 `checkEnvelopeCombination` 消费面经
`EnvelopeAccessorContractTest` 的访问器特化接入（§10.2 交接）。

## 3. §11 验证矩阵 → 测试文件落位（EV-T02～T11 累积）

| §11 组 | 落位文件（用例名含矩阵组名） | 留痕 |
| --- | --- | --- |
| EV-ID-1 身份确定性 | SnapshotTest、SliceTest、**SliceFixtureContractTest.RebuiltSliceMatchesExpectedIdentityAndEncoding / RebuildTwiceGivesIdenticalIdentity**（数据集载体，EV-T11） | wp05-t03/t04/t11 |
| EV-ID-2 无浮点近似等价 | SliceTest.NoFloatApproximateEquivalence | wp05-t04 |
| EV-ID-3 展示无关性 | SnapshotTest.DisplayAndSessionIndependence | wp05-t03 |
| EV-INV 失效矩阵（§5.3 逐行） | CurrentnessTest（EV-CUR-1 系——电机成本/TCP/几何/负载/策略/求解配置/契约版本行）、**EvaluatorPortSuiteTest.DisplayUnitSwitchProducesNoInvalidationReason**（显示单位子例——无任何 reason，EV-T11） | wp05-t08/t11 |
| EV-VER-1~8 | VerdictTest（决策表正反例，直接装配）、**EvaluatorPortSuiteTest**（端口路径：EV-VER-1~5/8＋EV-VER-6 双例——ScriptedEvaluator 产出→aggregateVerdict，EV-T11） | wp05-t06/t11 |
| EV-COV-1 漏验拦截 | VerdictTest.MissingMandatoryExecutionBlocked | wp05-t06 |
| EV-COV-2 重复/错误引用 | EvidenceTest.CoverageMatrix 系（校验器面） | wp05-t05 |
| EV-COV-3 零样本与分母 | EvidenceTest.RegionCoverage 系（校验器面）＋VerdictTest（汇总裁定面） | wp05-t05/t06 |
| EV-COV-4 基准一致性 | VerdictTest.Baseline 系 | wp05-t06 |
| EV-ENV-1/2 组合矩阵 | EnvelopeTest（表 3 全组合＋快照绑定面）、CompatibilityTest（EV-ENV-2 判定面）、**EnvelopeAccessorContractTest**（testkit 泛型谓词交接面，EV-T11） | wp05-t07/t09/t11 |
| EV-CPA-1~3 | CompatibilityTest | wp05-t09 |
| EV-CUR-1~4 | CurrentnessTest | wp05-t08 |
| EV-REG-1/2 注册边界/并发 | EvaluatorTest | wp05-t10 |
| EV-REG-3 替身边界声明 | **本文件 §1（文档留痕）**＋**EvaluatorPortSuiteTest.ScriptedDoubleBoundary.***（机检两面）＋EvidenceTestDoubles.hpp 文件头同款声明 | wp05-t11 |
| CR-01 词表契约（跨单元红线） | EnvelopeContractTest（类型面/源码面/值面） | wp05-t07 |

> 说明（测试目标分工，§11 头注原文）：`sdurws_ird_evidence_test`＝单元内
> 行为；`sdurws_ird_evidence_contract_test`＝跨单元契约面（CR-01 词表、
> testkit 泛型谓词交接、切片契约夹具数据集——两目标均为两模式构建，证据
> 留痕见 traceability/builds/wp05-t11/）。
