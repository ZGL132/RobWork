# diagnostics 单元测试设施说明与替身边界声明（DIAG-T10）

> 单元：diagnostics（WP-09）。本文件是 `diagnostics/test/` 的测试文档入口：
> §1 为**替身边界声明**（EV-REG-3 同模式——units/diagnostics.md §11
> DIAG-T10 行完成条件"替身边界声明（替身输出仅验证契约——testkit
> EV-REG-3 同模式）"的文档落点，留痕随本仓库版本化）；§2 为替身清单与
> 编程面；§3 为 §10 验证矩阵 → 测试文件落位全表（DIAG-T02～T10 逐任务
> 累积）。

---

## 1. 替身边界声明（全文；EV-REG-3 同模式）

**ScriptedDiagSource 的脚本输出仅验证 diagnostics 契约（条目/finding/
日志行的产出路径与数据形状），不构成真实业务错误、worker 崩溃、磁盘
故障或碰撞/超限等场景的正确性证明；脚本数据不得冒充真实诊断证据。**

1. **替身只覆盖"可控诊断源"这一个测试设施位**。ScriptedDiagSource 是
   project/execution/ui 等诊断生产方在阶段 A 的测试投影（§11 DIAG-T10
   行产物定义）；真实生产方归各单元（阶段 B/C）与 L5 装配。本单元产品
   源码面（`src/`＋`include/`）零替身符号，由
   `ScriptedDoubleBoundary.ProductSourcesFreeOfDoubleSymbols` 用例机检
   钉住（T-1 红线的替身侧对偶：替身也绝不进入产品源码面）。
2. **替身脚本走真实产品路径**（EV-REG-3 模式第 3 条"被测产品对象不做
   替身"）：条目步经真实 `DiagnosticsFactory::create`（码表校验/字段
   派生全生效）入真实 `DiagCatalog`（去重/容量护栏/通知全生效）；finding
   步经真实 `ConfirmableService::create`（绑定冻结/callbackToken 分配
   全生效）；日志步经真实 `LoggingPipeline::log`（脱敏/截断/两级分流
   全生效）。替身只负责按序列供给预设数据——脚本产出对这些产品行为的
   正确性**有**证明力（这正是其验证价值），对未实现的真实业务场景
   **零**证明力。
3. **脚本数据是契约形态数据**。预设条目/finding/日志行均按 §4.2/§5/§7.2
   契约形状构造的测试输入；其中不存在任何真实的运动学求解、碰撞检测或
   worker 进程管理。
4. **替身数据不进入任何持久形态**。全部用例在测试进程内存或每用例独立
   临时目录中构造与断言；临时目录 RAII 清理；不写归档、不进项目文件。
   产品源码面零替身符号由机检面钉住（见第 1 条）。
5. **未执行不得标注通过**（§10/§11 完成条件原文）：每个测试执行后留痕
   （gtest XML＋ird-test-report.json，见
   `doc/industrial-robot-design/traceability/builds/wp09-t10/`）；任何未
   在留痕中出现的用例不得在验收材料中标注通过。

## 2. 替身清单

| §11 名称 | 适配面/落位 | 编程面 | 消费用例 |
| --- | --- | --- | --- |
| `ScriptedDiagSource`（脚本化可控诊断源——§11 点名） | 注入真实 `DiagnosticsFactory`/`IDiagnosticSink`/`IConfirmableFindingService`/`ILogger`（ScriptedDiagSource.hpp，DIAG-T10；命名空间 `sdurws::ird::diagnostics::testdoubles`） | 统一序列步进回放（条目/finding/日志行三形态交错）；回放进度与产出观测面；脚本耗尽为有界正常终态 | ScriptedDiagSourceTest（ScriptedReplay 组） |
| `StubEnvironment`（绑定复核探针桩） | `IConfirmationEnvironment`（各测试文件自持——ConfirmableTest 先例同款） | 恒定 tip/空策略——本套件不做确认提交，探针返回值不被触达 | ScriptedDiagSourceTest 夹具 |
| `ManualClock`/`TempDirGuard`/`TipEnv` | `IClock`/RAII/`IConfirmationEnvironment`（各测试文件自持） | 确定性时间、临时目录隔离、确认放行路径探针 | 各套件夹具 |

补充说明：`DiagCatalog`/`DiagnosticsFactory`/`ConfirmableService`/
`LoggingPipeline`/`RedactionService`/`CrashReportWriter` 均为**真实产品
实现**（非替身）——替身脚本的全部产出都经这些产品路径发生。testkit 的
`checkDiagnosticRecord`/`checkComparativeFields` 消费面经
DiagContractCheckTest 接入（testkit.md §10.2 diagnostics 行交接；
`sdurws_ird_diagnostics_contract_test` 链接 `sdurws_ird_testkit`＝T-1
允许形态 `{被测产品目标, sdurws_ird_testkit, gtest}`——产品目标
`sdurws_ird_diagnostics` 不链 testkit，由 LinkageContractTest 沿用面
＋配置期守卫钉住）。

## 3. §10 验证矩阵 → 测试文件落位（DIAG-T02～T10 累积）

| §10 组 | 落位文件（用例名含矩阵行编号） | 落位任务 |
| --- | --- | --- |
| DT-REG-1~5（注册表） | StableCodeRegistryTest | DIAG-T03 |
| DT-DIAG-1/2、DT-DUP-1/2、DT-CAT-1/2、DT-REG-3/4（工厂半区） | CatalogFactoryTest | DIAG-T04 |
| DT-CFM-1~5（服务端状态机/绑定复核/失效） | ConfirmableTest | DIAG-T05 |
| DT-CFM-6~10（project 桩联动半区） | ConfirmableProjectStubTest | DIAG-T05 |
| DT-AGG-1~5、DT-CHAIN-1、DT-DUP-1 计数半区 | AggregationTest | DIAG-T06 |
| DT-LOG-1~5（两级分流/稳定序/写失败/禁用独立/worker 重放） | LoggingTest | DIAG-T07 |
| DT-SEC-1~4、DT-LIFE-4、AT-11 崩溃文件 | RedactionTest | DIAG-T08 |
| DT-LIFE-1/2/3（容量/保留/归档失败） | CatalogLifecycleTest | DIAG-T09 |
| DT-LIFE-5＋宿主留痕形状冻结 | CatalogHostTraceStubTest | DIAG-T09 |
| DT-BUILD（零 Qt/依赖图/头布局红线） | BuildRedLineTest＋LinkageContractTest | DIAG-T02 |
| **DT-COLLAB-1**（命令/任务/运行关联——DiagQuery 过滤） | **CollaborationQueryTest.DtCollab1_CommandAndTaskDiagnosticsFilteredByRevisionAndTask** | **DIAG-T10** |
| **DT-AT 映射**（AT-01/10/11/13/19/30/34 观测点数据形状——不代验 AT） | **CollaborationQueryTest.DtAt01~DtAt34 七用例** | **DIAG-T10** |
| **ScriptedDiagSource 基座＋替身边界机检** | **ScriptedDiagSourceTest（ScriptedReplay＋ScriptedDoubleBoundary 组）** | **DIAG-T10** |
| **testkit 契约消费**（checkDiagnosticRecord/checkComparativeFields） | **DiagContractCheckTest** | **DIAG-T10** |

> 测试目标分工（§10 头注原文）：`sdurws_ird_diagnostics_test`＝单元内
> 行为（CollaborationQueryTest 落位于此）；`sdurws_ird_diagnostics_
> contract_test`＝跨单元契约面（ScriptedDiagSource 基座、testkit 谓词、
> project/execution sink 形态——两目标均为两模式构建，证据留痕见
> traceability/builds/wp09-t10/）。
