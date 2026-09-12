# runtime 单元测试设施说明与替身边界声明（RT-T12）

> 单元：runtime（WP-06）。本文件是 `runtime/test/` 的测试文档入口：
> §1 为 **RT-STUB-0 替身边界声明**（units/runtime.md §11 矩阵行的"测试文档
> 显式声明"落点，文档留痕随本仓库版本化）；§2 为替身清单与编程面；§3 为
> §11 验证矩阵 → 测试文件落位全表（RT-T02～T12 逐任务累积）。

---

## 1. RT-STUB-0 替身边界声明（全文）

**替身输出仅验证 runtime 契约，不构成 RobWork 算法/业务算法正确性证明。**

1. **替身只覆盖注入接口**。runtime 的产品依赖边界（units/runtime.md §3.3、
   ARCH §3.5）决定了编译链通过五个最小只读接口消费外部能力：对象字节来源
   （`IObjectBytesSource`）、修订闭包来源（`IRevisionClosureSource`）、
   RobotDesign 解析器（`IRobotDesignReader`）、资源提供者
   （`IRuntimeResourceProvider`）、取消令牌（`ICompileCancelToken`）。
   测试替身（§2 清单）仅实现这五个接口——它们是 project/modeling/io/
   execution 职责在阶段 A 的测试投影；真实实现分别归 modeling/io（阶段 B）
   与 execution/L5 装配。
2. **RobWork/rwsim 本体不做替身**。对 RobWork 行为的一切断言（Frame 树、
   SerialDevice FK、DWC Body、异常转译触发点）直接针对真实基线库构造的
   WorkCell/DWC——不以伪造输出充当框架行为。凡断言涉及基线读数的用例
   （RT-AD-2、RT-EQ-1、RT-BW-4/5、RT-SNAP 系）均在集成模式运行（真实库可链）。
3. **被测产品对象不做替身**。十段编译器（`CanonicalModelCompiler`）、快照
   工厂（`RuntimeSnapshotFactory`）、名称映射（`RuntimeNameMap`）、缓存判定
   （`judgeCompileCacheCompatibility`）等一律使用产品实现。唯一的例外面是
   RT-T09 快照工厂用例中的 `ScriptedCompiler`（§10.0 原文把
   `buildCanonicalModel` 分段入口定义为"测试替身注入点"——替身注入该接缝
   是契约原文的一部分，不是对编译器行为的伪造）。
4. **canonical 侧手写 FK 是测试对照工具**（RT-EQ-1）：它与 RobWork Device
   FK、黄金数据集闭式解三方互证，只证明**编译映射正确**（CanonicalModel→WC
   的逐关节变换一致性），不构成产品 FK 算法正确性证明——真值以 RobWork 为
   消费基线（§11 RT-EQ-1 行原文）。
5. **未执行不得标注通过**（§12 RT-T12 完成条件原文）：每个测试执行后留痕
   （gtest XML＋ird-test-report.json，见
   `doc/industrial-robot-design/traceability/builds/wp06-t12/`）；任何未在
   留痕中出现的用例不得在验收材料中标注通过。

## 2. 替身清单（`RuntimeTestDoubles.hpp`——§11 设施清单的规范落位）

| §11 名称 | 适配接口 | 编程面 | 消费用例（示例） |
| --- | --- | --- | --- |
| `ScriptedObjectSource`＋`ScriptedClosureSource`（内存对象库 `ScriptedObjectLibrary`） | `IObjectBytesSource`/`IRevisionClosureSource` | 闭包条目登记；`rejectInClosure`（CM-0 注入）、`hideBytes`、`hideRevision` | CompilerTest（RT-CONT-1）、ContractSuiteTest（RT-ID 系） |
| `ScriptedReader`（Description 直构） | `IRobotDesignReader` | 预置 Description；`failWith`、`throwBadAlloc`、`resetTo`、`reads()` | CompilerTest（RT-CPX-3/4）、ContractSuiteTest |
| `FakeResourceProvider`（可编程缺失/变化/预算） | `IRuntimeResourceProvider` | 逐资源字节应答序列（末项重复）；`failWith(ResourceBudget/ResourceChanged)`；未登记＝缺失 | CompilerTest（RT-RES-1/2）、ContractSuiteTest（RT-RES-3、RT-BW-5） |
| `CancelToggle`（取消注入） | `ICompileCancelToken` | 原子 `request()`/`reset()`（可跨线程置位） | SnapshotTest（RT-CPX-4）、D-11 系用例 |

补充设施：`ContractHarness`（确定性闭包＋Description 一站式装配——id/摘要
由固定种子派生，跨进程字节一致）；`CanonicalModelFixture.hpp`（RT-T04 起的
模型层部件包，RT-ID/RT-NM 系模型层用例输入）。

## 3. §11 验证矩阵 → 测试文件落位（RT-T02～T12 累积）

| §11 组 | 落位文件（用例名含矩阵组名） | 留痕 |
| --- | --- | --- |
| RT-ID-1 重复编译稳定＋跨进程 | CodecTest（模型层）、SnapshotTest（快照层）、WorkCellCompilerTest（WC 结构）、**ContractSuiteTest.CrossProcessIdentityStable**（跨进程，RT-T12） | wp06-t04/t09/t07/t12 |
| RT-ID-2 无浮点近似等价 | CodecTest（位模式）、**ContractSuiteTest.TinyFloatDeltaSplitsIdentityAndCache**（缓存判定集成面，RT-T12） | wp06-t04/t12 |
| RT-ID-3 展示无关性 | CodecTest（排除域）、**ContractSuiteTest.DisplayFieldsNeverEnterRuntimeIdentity**（RT-T12） | wp06-t04/t12 |
| RT-BW-1 变换数值例 | BaseWorldTransformTest（规则层）、**ContractSuiteTest.NumericExampleThroughSnapshotView**（快照视图＋档案 rt，RT-T12） | wp06-t06/t12 |
| RT-BW-2/3 正反一致/默认地面 | BaseWorldTransformTest（四预设精确矩阵）、**ContractSuiteTest.DefaultBaseCompilesToIdentityTransform**（编译面，RT-T12） | wp06-t06/t12 |
| RT-BW-4 二次叠加反例 | BaseMountConsistencyTest（规则）、WorkCellCompilerTest（S9 写入面）、**ContractSuiteTest.ConsumerDoubleApplicationIntercepted**（消费端，RT-T12） | wp06-t06/t07/t12 |
| RT-BW-5 四消费方一致 | **ContractSuiteTest.FourConsumersConsistentOnInvertedModel**（AT-37 载体，RT-T12） | wp06-t12 |
| RT-BW-6 非法变换 | BaseWorldTransformTest＋DescriptionValidatorTest（校验器面） | wp06-t03/t06 |
| RT-NM-1～7 | NameMapTest（全组）、**ContractSuiteTest.GoldenNameMapRoundtripDataset**（数据集载体，RT-T12） | wp06-t05/t12 |
| RT-CPX-1～4 | DynamicWorkCellCompilerTest、CompilerTest、SnapshotTest | wp06-t08/t09/t11 |
| RT-RES-1/2 | CompilerTest | wp06-t11 |
| RT-RES-3 路径不作身份 | **ContractSuiteTest.SourcePathHintIsNotIdentity**（RT-T12） | wp06-t12 |
| RT-CACHE-1～4 | CacheKeyTest、ContractSuiteTest（RT-ID-2/3 的判定面复用） | wp06-t10/t12 |
| RT-SNAP-1～4 | SnapshotTest | wp06-t09 |
| RT-AD-1/2/3 | WorkCellCompilerTest、DynamicWorkCellCompilerTest | wp06-t07/t08 |
| RT-CPL-1 | DescriptionValidatorTest | wp06-t03 |
| RT-CAP-1/2 | **ContractSuiteTest.MixedMissingInputsDeriveCorrectCapabilities / MissingCapabilityNeverEscalates**（RT-T12） | wp06-t12 |
| RT-CONT-1/2 | CompilerTest | wp06-t11 |
| RT-EQ-1 canonical↔WC FK | WorkCellCompilerTest（三方对照）、**ContractSuiteTest.GoldenFkEquationDataset**（数据集载体，RT-T12） | wp06-t07/t12 |
| RT-STUB-0 替身边界声明 | **本文件 §1（文档留痕）**；RuntimeTestDoubles.hpp 文件头同款声明 | wp06-t12 |

> 观测点适配说明（工具链事实，均已在各任务单元卡 §15.4 登记）：MSVC 工具
> 链无 ASAN/TSAN——RT-SNAP-1 以"多线程并发压力＋结果等价评审"承载、
> RT-SNAP-4 以 weak_ptr 过期时序承载、RT-AD-3 以销毁顺序可观测面（控制块
> 计数）承载、RT-CPX-4 的"清理失败"子项归 project 事务（runtime 零磁盘写）。
