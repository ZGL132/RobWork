# 工业机械臂设计软件 · testkit 单元详细设计（阶段 A）

> 2026-09-10 同步：本单元详设编写完成（第一批 11/20）；保留本文 Draft/Draft-Structured 评审状态，不代表接口已冻结或实现通过。当前任务与准入结论见 [阶段一同步记录](../traceability/phase-one-readiness.md)；历史磁盘调查仅表示编写时事实，现状以该记录为准。

| 字段 | 值 |
| --- | --- |
| 文档版本 | v0.1（首版草案） |
| 日期 | 2026-09-09 |
| 状态 | **`Draft-Structured`**（本文只做详细设计；不自行宣布 Accepted，不视任何自审为实现测试或正式验收） |
| 文档代号 | UNIT-TESTKIT |
| 单元 | testkit（测试支撑单元；ARCHITECTURE §3.1 单元总表：黄金数据集清单、容差档案、稳定集合断言、契约测试基座——**不随产品分发**）。testkit 不属于产品分层（§2.3 L1～L5）任何一层，只被各级测试目标消费 |
| 上游 | `REQUIREMENTS.md` **v1.16（`Accepted`，2026-09-09 签署；签署后变更 C1～C8 已留痕）**；`ARCHITECTURE.md` **v0.11（`Draft`，待评审）** |
| 协作输入 | `units/core.md` **v0.1（`Draft`，未冻结）**——testkit 消费的 core 公共契约以其 v0.1 签名为基线，逐项标注状态（§1.2、§3.2）；core 冻结版本如有变更，本文按影响面增量同步（待裁决 P-TK-2） |
| 上游下游链位置 | ARCHITECTURE §11.1：`DETAILED-DESIGN.md`（已建立）→ `units/*.md`（单元任务卡）。本文即 `units/testkit.md`，按任务卡深度编写（接口签名、数据类型在本文件冻结） |
| 构建落位 | `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/`（骨架已建：目标 `sdurws_ird_testkit`〔INTERFACE 占位〕＋别名 `RWS::ird::testkit`＋公共头保留位 `include/sdurws/ird/testkit/README.md`；见上级 `industrialrobot/CMakeLists.txt`） |
| 任务归属 | 详见 `development-task-breakdown.md` WP-B～WP-I 映射；本文只维护单元内部任务
| 适用 AGENTS.md | 仓库根 `AGENTS.md` 适用：产品代码详细中文注释、框架零源码修改、双模式构建与留痕、提交后推送。Windows Qt GUI 测试须在 VS x64 环境设 `QT_QPA_PLATFORM=windows`，逐个绝对路径启动。 |
| 实现口径 | 从头构建（REQUIREMENTS v1.9/v1.11）；`old/` 历史实现仅功能范围对照且**当前磁盘缺失**（core.md §1.2 R-5 同源登记）；历史输出不作为正确性标准 |

---

## 1. 文档信息、上游基线与设计目标

### 1.1 文档定位

本文是 testkit 单元的唯一详细设计：依据 ARCHITECTURE §3.1 分配给 testkit 的职责（黄金数据集清单、容差档案、稳定集合断言、契约测试基座），把其数据类型、公共接口、不变量、夹具与故障注入设计、测试报告契约、阶段 A 实现任务拆分写到可直接实现的深度。

testkit 的产品语义只有一条：**让各业务单元能独立编写可追溯、可复现、可与 REQUIREMENTS 附录 D 容差对齐的测试**。需求语义与验收标准一律以 `REQUIREMENTS.md` 条目为自足定义，本文不重定义、不放宽、不收窄；本文引用的需求 ID 与章节号均指 v1.16 当前正文。架构归属以 `ARCHITECTURE.md` v0.11 为准。

### 1.2 上游与磁盘现状登记（2026-09-09 实测）

| 项 | 状态 | 说明与对本文的影响 |
| --- | --- | --- |
| `REQUIREMENTS.md` | 存在，v1.16，`Accepted` | 唯一需求权威源。本文消费其附录 D（P-01 冻结容差表，含 C1/C3/C4/C7 修订）、§8.1（证据与合法组合）、NFR-COR-01/02/03、AT 清单（§21） |
| `ARCHITECTURE.md` | 存在，v0.11，`Draft`（待评审） | 唯一架构权威源。testkit 行见其 §3.1；§3.5 单元依赖表**未列 testkit**（其为测试单元、非产品依赖图成员）——本文按"testkit→core（仅测试目标可用）"设计并登记建议（P-TK-4）；若评审产生 A9+ 处置按影响面同步 |
| `units/core.md` | 存在，v0.1，`Draft`（**未冻结**） | 协作输入。testkit 消费其公共值类型与比较公式（§3.2 逐项登记状态）；其 §10.3 已向 testkit 交接"黄金数据集容差档案与 core Tolerance 的一致引用方式"——本文 §4.3 即该交接的承接答复 |
| `DETAILED-DESIGN.md` | 已建立 | 20 个单元详设总目录，本文是单元详设正文 |
| `development-task-breakdown.md` | 已建立 | WP-A～WP-I 主 WP 计划，本文只维护单元任务 |
| `units/testkit.md` | **不存在**（本文新建） | 其余 18 个单元任务卡（core.md 除外）均未产出 |
| 构建骨架 | 存在：`industrialrobot/CMakeLists.txt`＋20 单元目录＋`patches/`，共 23 文件 | `sdurws_ird_testkit` 为 INTERFACE 占位，无源码；`_test`/`_contract_test` 等目标按任务卡逐个登记、不预建空目标（骨架注释明文） |
| 仓库既有测试框架 | **googletest（捆绑源码）**：`RobWork/RobWork/gtest/`（`ADD_RW_GTEST` 宏＋`sdurw-gtest-main` 静态库＋XML 报告目标；经 `RW_BUILD_TESTS`/`RW_HAVE_GTEST` 启用，`RobWork/CMakeLists.txt` L75~90 实测）；`RobWork/RobWorkStudio/gtest/`（`ADD_RWS_GTEST` 宏，含 Qt 测试目标先例）；另有导出目标 `RW::gtest`/`RW::gtest_main`（`cmake/gtestTargets.cmake`） | **本文决定沿用 googletest，不引入第二套测试框架**（决策 D-01）；测试资源定位先例：RWS gtest 的 `testfiles/`＋`TestSuiteConfig.xml` 生成模式（本文 §4.6 数据根定位借鉴其思路但独立实现） |
| `old/` | **不存在于磁盘**（git 亦未跟踪） | 与 REQUIREMENTS v1.10 声明不符（core.md §1.2 R-5 已登记）；对本文无输入：从头构建口径下历史输出不得作为黄金真值 |
| CI 基础设施 | `RobWork/gitlab-ci/`（基线框架的 GitLab CI 配置存在）；industrialrobot 自身无 CI 配置 | 黄金数据 CI 访问设计（§7.5）给出源内默认＋可覆写变量两级方案，不假设专用 artifact 通道 |

### 1.3 设计目标

1. **可追溯**：每条测试结果机器可读地关联需求 ID、AT、数据集版本、容差档案版本、种子与线程配置（§7）；Skipped/NotRun 永不计为 Passed。
2. **可复现**：固定种子＋固定线程＋确定性重放记录（ReproRecord），满足 NFR-COR-02 的测试侧要素；不要求浮点结果文件逐字节相同（附录 D 第 8 项口径）。
3. **与需求容差零漂移**：比较语义**只**经 core `closeWithin`/`allCloseWithin`（附录 D C4 公式唯一实现，core.md §4.5）；容差数值**只**来自附录 D 或数据集在允许范围内的逐例声明（第 9 项）；声明缺失即"容差未定义"报错，不静默通过（C4）。
4. **不成为第二套产品代码**：不实现业务算法、事务、调度、RunRegistry、ResultEnvelope 汇总、模型编译、碰撞或动力学（§2.2）；产品库、插件、worker 不得反向依赖 testkit（分发红线，§3.6）。
5. **最小可落地**：阶段 A 交付数据清单、容差档案、基础断言、夹具生命周期、测试报告与 core 接入示例（§9）；进程测试等能力按消费者单元实际阶段需要交付（§6.5、§10）。

### 1.4 语言、标准库与构建约束（按仓库实测确认，与 core.md §1.4 同源）

| 项 | 实测事实 | 本文决定 |
| --- | --- | --- |
| 编译器/生成器 | 构建缓存 `build/CMakeCache.txt`：Visual Studio 17 2022（MSVC x64） | 按 MSVC 设计；不用平台特定扩展（进程支撑的 Windows API 仅限 `ProcessRunner` 实现文件内，阶段按需交付） |
| C++ 标准 | 基线 RobWork `CMAKE_CXX_STANDARD 11`；industrialrobot 目标显式 C++17（core.md D-01） | testkit 同口径：显式 `cxx_std_17`；`std::filesystem`、`std::optional`、`std::variant`、`std::string_view` 允许；不使用 C++20 特性；不引入 Boost 等第三方运行库 |
| Qt | Qt 6.11.1（msvc2022_64） | **`sdurws_ird_testkit` 库零 Qt**（与 L2 同纪律）：测试断言与数据设施不需要 Qt；未来 GUI 测试辅助（若有）单列 `sdurws_ird_testkit_qt` 独立目标，禁止传递进入生产计算库（§3.5） |
| 测试框架 | 捆绑 googletest（§1.2） | 沿用；断言宏在消费方翻译单元展开（testkit 库本体不链接 gtest，§3.3） |
| JSON | vcpkg installed 目录实测无独立 JSON 库（xercesc/zlib 有，nlohmann 无） | testkit 自带**受限 JSON 解析器**（测试侧自用工具，决策 D-02；边界论证见 §4.1） |
| RobWork 数学 | `rw::math`（`sdurw_math` 目标） | testkit 经 core 间接依赖（core 公共头传递）；不直接包含 Eigen |

---

## 2. 需求承接与职责边界

### 2.1 直接承接的需求（testkit 为支撑责任方；实现责任方在各业务/平台单元）

| 需求 | testkit 承接的部分 | 不可越界的部分（其他单元所有） |
| --- | --- | --- |
| NFR-COR-01（解析算例/独立参考实现对照＋默认容差） | 黄金数据集清单模型与完整性校验（§4）、容差档案（§4.3）、对照断言（§5.3）；**数据集内容本身由 WP-02 会同各域逐步交付**，testkit 拥有清单格式与校验机制 | 各域算法与解析算例的具体推导（kinematics/trajectory/dynamics/…）；附录 D 各容差项数值权威＝REQUIREMENTS |
| NFR-COR-02（等价候选集合＋稳定排序；并行归约容差） | 稳定集合与顺序断言（§5.4）、种子/线程固定与复现记录（§6.3、§7.1） | 端到端确定性由各评估域＋execution 保障（core.md §2.1 同款边界） |
| NFR-COR-03（非有限/非法单位/引用缺失不得静默转 0 或通过） | 断言失败语义：NaN/Inf→失败并给原因码；单位不匹配→数据集非法错误；缺失元素定位（§5.3、§5.4） | 运行时数据纪律的实现归 core/各消费单元 |
| 附录 D（P-01 冻结容差表） | **逐项承接测试侧角色**：第 4/5/8/9 项为测试对照容差（黄金数据/契约测试消费）；第 6/7/12 项为固定判据（测试断言消费）；第 1/2/3/10 项为产品运行校验容差（testkit 不替产品持有默认值——产品侧经 core `runtimeAbsoluteTolerance` 及分析配置取得）；第 11 项为工程策略默认（测试经被测 policy 对象取值，testkit 只记录快照） | 任何默认值修改走需求变更；**本文不放宽任何阈值**（自审项 A-3） |
| §8.1 表 3／EVI-01（ResultEnvelope 合法组合） | 通用断言助手：结果身份五元组合法性、诊断字段完整性（§5.5，经模板谓词注入，不依赖 evidence 头） | ResultEnvelope 类型、构造校验器、汇总判定归 evidence（core.md §2.1） |
| ERR-01／UX-03（诊断字段规范、比较型三要素） | 通用断言助手：DiagnosticRecord 必填字段、ComparativeFields 三要素、NotApplicable 不伪造数值（§5.5） | 诊断码注册、文案、显示归 diagnostics/ui |
| WP-02（黄金数据集） | 清单 schema、五类数据集分类与升级规则（§4.2）、生成脚本登记格式、完整性校验工具 | 各域数据集内容、参考实现、生成脚本编写归各域＋WP-02 协同 |
| AT 支撑（AT-01/03/06~11/13/16/18/19 等） | 各 AT 的自动化载体＝各单元 `_test`/`_contract_test` 目标，消费 testkit 设施（§10.2 映射表） | AT 验收归对应主责单元与需求验收流程；**testkit 自测通过≠AT 通过** |
| NFR-MNT-01/02（内核零 Qt、模型测试可直接调用） | testkit 本体零 Qt，支撑"计算内核可被模型测试直接调用"的测试形态（无 GUI 依赖断言，§6.7） | 合规由构建红线 R-3 门禁保障（WP-01-T01） |
| NFR-REL-01/02、PM-08（保存事务、崩溃恢复） | 故障注入原语与进程测试支撑（§6.4、§6.5，按需交付） | 事务协议、恢复扫描实现归 project；崩溃隔离归 execution |

### 2.2 明确不做（testkit 的非目标）

以下能力有明确的其他所有者或明令排除，testkit **不实现、不预建桩、不建空目标**：

1. 项目事务、项目写锁、草稿/恢复协议（project；ARCH §6.4/§6.8）；
2. 任务调度、状态机、运行登记表（RunRegistry）、检查点（execution；ARCH §4）；
3. ResultEnvelope 汇总、证据门禁、RequiredEvidenceProfile（evidence；§8.1 表 2/4）；
4. 模型编译、FK/IK/碰撞/动力学/传动等一切业务算法（runtime/policy/各域）；
5. Pareto 非支配判定、IK 去重、采样计划等**业务规则断言**——testkit 只提供匹配机制，业务断言适配器归相应单元测试（§5.4.5）；
6. 生产容差算法——比较公式唯一实现归 core（`closeWithin`/`allCloseWithin`），testkit 不复制（§4.3、自审项 A-1）；
7. 产品运行校验的容差默认值持有（附录 D C7：产品侧不得依赖黄金数据集；`runtimeAbsoluteTolerance` 归 core）；
8. 诊断码表、文案权威、用户可见诊断呈现（diagnostics/ui）；测试报告**不是**产品 ReviewReport、不依赖 reporting 单元（§7.4）；
9. CSV/JSON 产品读写器（io 的 NFR-SEC-03 方言/转义体系）——testkit 的 JSON 解析仅服务测试数据格式，与产品文件格式无关（D-02 边界）；
10. 第二套测试框架、独立断言语言、参数化测试引擎——gtest 已覆盖（D-01）；
11. GUI 测试驱动、控件自动化——阶段 A 无消费者；未来若需要，单列 `sdurws_ird_testkit_qt` 目标并按 §6.7 Windows 规则执行，不进入 testkit 本体。

### 2.3 testkit 拥有／消费／不拥有对照表

| 相对单元 | testkit 提供（testkit→该单元的测试目标） | testkit 消费（该单元→testkit 方向的产品契约） | 易误认为 testkit 的、但明确归该单元的内容 |
| --- | --- | --- | --- |
| core | 数值/集合/契约断言、容差档案装载、夹具与报告；core 的 `_test` 目标是 testkit 首个接入示例（§9 TK-T10） | **全部公共值类型与比较公式**：`Tolerance`、`closeWithin`、`allCloseWithin`、`runtimeAbsoluteTolerance`、`UnitToken`/`QuantityKind`、`ObjectId` 等身份类型、`ContentDigester`（完整性校验复用）、`DiagnosticRecord`/`ComparativeFields`、`SourcedValue`（状态：core.md v0.1 `Draft` 未冻结，签名以其为基线，P-TK-2） | 数值比较实现、单位换算、身份类型定义本身 |
| 各业务/平台单元 | 各 `_test`/`_contract_test` 目标消费的全部测试设施；数据集清单 schema 与 lint 工具 | 无（**testkit 不依赖任何产品单元**；被测类型经模板参数/访问器注入，§5.5） | 各域黄金数据集内容、生成脚本、业务断言适配器、契约套件注册 |
| io | 故障注入适配模板（其写入口的 fake 实现，§6.4） | 无（同上） | CSV 方言/转义、SafePath/BudgetGuard（归 io，core.md P-AR-1） |
| execution | 进程测试支撑（按需，§6.5）、可观察事件等待原语 | 无 | WorkerLauncher、取消协议、心跳 |
| reporting | 无（测试报告不经 reporting 生成，§7.4） | 无 | ReviewReport 渲染、证据包导出 |
| WP-24（打包） | 分发排除检查建议（§3.6） | 无 | 安装包装配与安装树扫描门禁的实施 |

### 2.4 分发与依赖红线（本文新增 T-1/T-2，与 ARCH §3.2 四条红线并列执行于测试侧）

| 红线 | 内容 | 依据 | 检验方式 |
| --- | --- | --- | --- |
| T-1 | 产品目标（产品库、插件、worker、应用壳）**不得链接或包含 testkit**；testkit 头不出现在任何产品目标的 include 集 | 任务约束（本文§三）、ARCH §3.1"不随产品分发" | 构建依赖图门禁（随 WP-01-T01 启用）＋安装树扫描（§3.6） |
| T-2 | testkit 库只依赖 core＋标准库；**不得为测试方便链接其他产品单元** | 任务约束"不得让 testkit 依赖全部产品单元" | testkit 目标链接清单检查（§8 TK-BUILD） |

允许的依赖形态（无环）：`sdurws_ird_<unit>_test → { 被测产品目标, sdurws_ird_testkit, gtest }`，而 `sdurws_ird_testkit → sdurws_ird_core`。`core_test → testkit → core` 是 DAG 边而非环；被禁止的环是 `core（产品目标）→ testkit`（T-1）。

---

## 3. 组成、依赖、构建目标与目录布局

### 3.1 组成（公共头模块）

testkit 由 11 个公共头模块＋gtest 适配层（2 个头）＋编译实现组成（实现文件随 §9 任务落地，头为契约权威）：

| 头（`testkit/include/sdurws/ird/testkit/`） | 内容 | 详见 |
| --- | --- | --- |
| `JsonLite.hpp` | 受限 JSON 值模型、解析与序列化（测试侧自用） | §4.1 |
| `Dataset.hpp` | DatasetKind、DatasetManifest、GoldenDataset 装载与完整性校验 | §4.2、§5.1 |
| `ToleranceProfile.hpp` | ToleranceEntry/ToleranceProfile、档案装载与解析 | §4.3、§5.2 |
| `Check.hpp` | 数值断言：checkCloseWithin/checkAtMost/checkIdentical 族＋比较详情 CompareDetail/CheckResult＋宏 `IRD_EXPECT_*` | §5.3 |
| `SetCheck.hpp` | 稳定集合与顺序断言：SetMatchTraits、checkSetEquivalent/checkStableOrder、SetCheckResult＋宏 | §5.4 |
| `ContractCheck.hpp` | 通用契约断言：诊断记录、比较型三要素、任务身份、身份唯一性、数据完整性 | §5.5 |
| `Fixture.hpp` | TempDir、ReproRecord、DeterministicEnv、GoldenFixture 夹具基座 | §6.1、§6.2、§6.3 |
| `Fault.hpp` | FaultPlan/FaultTrigger/FaultLog、FaultInterceptor 适配模板（进程内故障注入原语） | §6.4 |
| `ProcessRunner.hpp` | TestProcessRunner/EventWatch（跨进程测试支撑；**设计冻结、实现按需**） | §6.5 |
| `Report.hpp` | TestOutcome/ComparisonRecord/TestRecord、报告写出（ComparisonRecord 复用 `Check.hpp` 的 CompareDetail） | §7.2、§7.3 |
| `TestPaths.hpp` | 黄金数据根定位（环境变量/编译期默认） | §4.6 |
| `gtest/AssertMacros.hpp` | gtest 适配层：`IRD_EXPECT_*`/`IRD_TEST_INFO` 宏（仅在消费方 `_test` 目标的翻译单元展开；**要求消费方已链 gtest**） | §5.3.3、§7.3 |
| `gtest/RecordListener.hpp` | gtest 适配层：TestRecordListener（header-only，消费方 TU 编译；testkit 库本体零 gtest） | §7.3 |
| `README.md` | 既有保留位说明（不参与编译） | — |

`src/` 实现（随 TK-T01 建）：JSON 解析、清单/档案装载、集合匹配、报告写出、临时目录（Win32 实现）、事件等待。

### 3.2 依赖（含 core 接口消费状态登记）

```
sdurws_ird_testkit ──► RWS::ird::core（公共值类型与比较公式；sdurw_math 经其传递）
                   ──► C++17 标准库（含 <filesystem>）
                   ──► （无其他：零 Qt〔T-2 纪律〕、零产品单元、零 Eigen 直接包含；
                        gtest 仅在消费方 _test 目标的翻译单元内经宏展开出现，
                        testkit 库本体不链接 gtest〔§3.3〕）
```

消费的 core 契约清单（全部来自 core.md v0.1 §4/§5；**状态：`Draft` 未冻结**，P-TK-2）：

| core 契约 | testkit 用途 | 状态标注 |
| --- | --- | --- |
| `Tolerance{relative,absolute}`＋`Tolerance::make` | 容差档案与断言的承载类型（一致引用方式的承接答复，core.md §10.3） | v0.1 §4.5 |
| `closeWithin` / `allCloseWithin` | **唯一**比较语义实现；testkit 断言只包转、不重写 | v0.1 §4.5/§5.5 |
| `QuantityKind` / `UnitToken` / `convert` | 容差条目量纲与单位标注、比较前换算到 SI | v0.1 §4.4/§5.4 |
| `runtimeAbsoluteTolerance` | 档案 lint 用（核对档案中"产品运行校验"类条目的 ε_abs 与附录 D C7 一致） | v0.1 §4.5 |
| `ContentDigester`（SHA-256） | 数据集文件完整性校验（复用，不另写摘要算法） | v0.1 §4.2 |
| `ObjectId` 等身份类型规范文本 | 集合断言的稳定身份键来源 | v0.1 §4.1 |
| `DiagnosticRecord`/`ComparativeFields`/`SourcedValue` | 契约断言助手字段访问 | v0.1 §4.8 |
| `TaskIdentity` | 结果身份五元组断言 | v0.1 §4.1 |
| `CoreError` | core 抛错的捕获与分类转发 | v0.1 §4.10 |

### 3.3 命名空间、目标与 CMake 集成

- 命名空间：`sdurws::ird::testkit`。
- 目标：`sdurws_ird_testkit`（骨架 INTERFACE → TK-T01 升级 STATIC），别名 `RWS::ird::testkit`；`target_compile_features(... cxx_std_17)`；`target_link_libraries(sdurws_ird_testkit PUBLIC RWS::ird::core)`。
- **gtest 接入**（D-01；P-TK-1 已关闭——2026-09-10 随 development-task-breakdown v0.2 §5.5 定稿，本行同步更正先前失实记载）：googletest 经 vcpkg 安装（`vcpkg install gtest:x64-windows`，经典模式），industrialrobot 全部 `_test`/`_contract_test` 目标统一经 `find_package(GTest CONFIG REQUIRED)` 接入——失败即停（REQUIRED），不静默跳过、不回落；**不**消费 `RW::gtest`（实测：构建树 `USE_gtest=OFF`、`RobWork/cmake/gtestTargets.cmake` 从未生成——2026-09-10 CR-07 复核确认，先前"实测存在"记载失实）、不源码 vendor、不混用两份 gtest；测试 main 自持（`GTest::gtest_main` 或 RecordListener 适配）；沿用仓库宏先例（`ADD_RW_GTEST` 模式：`add_test`＋XML 报告目标），宏定义放各单元自己的 CMake。testkit 库本体**不**链接 gtest；`IRD_EXPECT_*` 宏展开为 `EXPECT_*`/`ADD_FAILURE_AT`，要求消费方 `_test` 目标已链 gtest（文档化前置条件）。vcpkg 安装动作与版本登记随 TK-T01（≙WP-02-T01）执行；`find_package` 失败 → TK-T01 转 `blocked`，不自行引入第二框架。
- testkit 自测目标：`sdurws_ird_testkit_test`（随 TK-T01/T02 登记；gtest 接入同上）。
- 消费目标（各单元所有、各单元登记）：`sdurws_ird_<unit>_test`（单元内测试）与 `sdurws_ird_<unit>_contract_test`（跨单元契约测试），命名沿用 ARCH §3.3 既有约定；testkit 不代建任何空目标。
- 头包含形式：`#include <sdurws/ird/testkit/Dataset.hpp>`；私有实现头不入 `include/`（R-2 同款纪律）。

### 3.4 目录与黄金数据布局

```
industrialrobot/testkit/
  include/sdurws/ird/testkit/   # §3.1 公共头
  src/                          # 实现
  test/                         # testkit 自测（TK-*，§8）
industrialrobot/testdata/       # testkit 拥有的测试数据根（CMake 变量指向，见 §4.6）
  golden/
    <datasetId>/                # 每数据集一目录
      manifest.json             # 当前版本清单＋历史链（§4.2）
      v<major>.<minor>.<patch>/ # 每版本一目录（旧基线保留，§4.7）
        inputs/                 # 输入资源
        expected/               # 参考结果
        generate/               # 生成脚本＋生成记录（脚本与产物同置，可追溯）
  tolerance/
    <profileId>/v<version>.json # 容差档案（§4.3）
industrialrobot/tools/testdata/ # 清单 lint / 完整性校验工具（TK-T03 随交付）
```

数据根由 testkit 拥有（T-2：testkit 可拥有数据目录；数据集**内容**的责任归各域＋WP-02，见 §4.2 数据集五类与 §10.2 交接）。`testdata/` 不安装（§3.6）。

### 3.5 Qt/进程设施的隔离

阶段 A 无 Qt 依赖目标。若后续 ui/workflow 测试需要 Qt 辅助（如 QCoreApplication 模型测试环境、事件泵），登记规则：

- 单列目标 `sdurws_ird_testkit_qt`（Qt Core 起，按需 Widgets），**只被 GUI/模型测试目标链接**；
- `sdurws_ird_testkit`（本体）永不链接它，保证 Qt 不经 testkit 传递进入生产计算库（红线 R-3 的测试侧延伸）；
- 必要性登记：任何 `_qt` 目标的建立必须在其消费者单元任务卡中给出理由（避免"顺手预建"）。

### 3.6 构建、CI 与分发边界

| 项 | 规则 |
| --- | --- |
| 黄金数据定位 | 编译期默认 `industrialrobot/testdata`（CMake `SDURWS_IRD_TESTDATA_DIR` 缓存变量）；运行期 `TestPaths::goldenDataRoot()` 先读环境变量 `SDURWS_IRD_TESTDATA_DIR`，回落编译默认（CI 复制/挂载后只需设变量，§7.5） |
| CI 访问 | 数据随源码树版本化（默认；大体积数据集触发 P-TK-5 裁决 LFS/制品方案）；CI 步骤：配置→构建（含 `*_test`）→`ctest`；gtest XML（既有宏产物）与 `ird-test-report.json`（§7.2）一并归档 |
| 安装/打包排除 | industrialrobot 安装规则（未来由 WP-24 侧登记）**不得**包含：`sdurws_ird_testkit*` 目标、`testdata/`、`tools/testdata`；CI 安装树扫描断言零命中（脚本随 TK-T10 交付建议，门禁实施归 WP-01/WP-24）；依赖图门禁含 T-1/T-2 两条测试侧红线（§2.4） |
| 分发红线复述 | testkit、测试程序、测试数据不进入产品安装包（任务约束§三；NFR-SEC-05 依赖清单也不因测试目标膨胀） |

---

## 4. 黄金数据集与容差档案

### 4.1 JsonLite：测试侧受限 JSON（D-02）

**范围**：对象/数组/字符串（UTF-8，标准转义）/数字（IEEE double 可精确解析区间）/`true`/`false`/`null`。**拒绝**：重复键、`NaN`/`Infinity` 字面、尾随内容、注释、BOM。错误消息含行/列。

**为何自带而不复用/引入**：(a) io 的 JSON 读写器是产品单元且未产出——依赖 io 违反 T-2；(b) vcpkg installed 实测无独立 JSON 库，引入新第三方依赖超出"不擅自增加"边界；(c) RobWork 侧无共享 JSON 头可借。**边界声明**：JsonLite 是测试工具，不构成产品持久化格式的第二个实现点（NFR-MNT-03 不受影响——单位/身份/比较的权威仍在 core/io），其输出只入 `testdata/` 与测试报告。若 development-task-breakdown 未来统一测试侧 JSON 机制，本文按增量修订对齐（登记于 §12.2）。

```cpp
// JsonLite.hpp（节选）
struct JsonValue;                                   // variant: null/bool/double/string/array/object
JsonValue parseJson(std::string_view text);         // 非法抛 TestKitError（含 line:col）
std::string dumpJson(const JsonValue&, bool pretty);// 键序＝插入序（确定性输出）
```

### 4.2 黄金数据集清单（DatasetManifest）

#### 4.2.1 数据集五类与升级规则（任务约束§四.1 的落定）

| DatasetKind（token） | 定义 | 参考结果来源要求 | 可否作为独立正确性依据 |
| --- | --- | --- | --- |
| `analytic-case` 解析算例 | 闭式解/教科书算例/手工可推导结果 | 解析推导（manifest 内登记推导出处与公式编号） | **可以**（NFR-COR-01 首选） |
| `reference-impl` 独立参考实现 | 与生产实现**不同代码基**的参考程序输出 | 生成脚本＋参考实现版本＋种子/线程；`independentOfProductionImpl=true` 必填 | **可以**（须附独立性说明） |
| `contract-fixture` 契约夹具 | 领域对象/序列化/事件序列的构造样本 | 手工构造；合法性由对应单元契约定义 | 仅对该契约（不是数值真值） |
| `regression` 回归样本 | **生产实现**在指定版本的输出快照 | 生产实现生成（manifest 记录版本/配置/种子/线程） | **不可以**——只锁定"行为不回退"；升级为独立依据须换源重新审核（下述升级规则） |
| `performance-baseline` 性能基准 | 吞吐/时延/内存的基准场景与统计口径 | 生产实现测量＋硬件/负载口径 | 不作正确性依据；只作性能对照（NFR-PERF 系列，阶段 D） |

**升级规则**：`regression` 数据集**不得**因"长期通过"自动升级为 `analytic-case`/`reference-impl`；升级＝新建数据集（新 datasetId 或新 major 版本）＋按新来源重新审核（审核人＝该域详设所有者＋WP-02），manifest `history` 留痕。生产实现的输出**只能**进入 `regression` 与 `performance-baseline` 两类（自审项 A-4）。

#### 4.2.2 manifest 字段表（`manifest.json`；schemaVersion `ird-golden-manifest/1`）

字段类型记法：`string`/`int`/`uint64`/`bool`/`double`/`T[]`/`object`。"单位"列标注物理单位仅适用于数值字段。

| 字段 | 类型 | 必填 | 默认 | 单位/格式 | 有效范围与关联约束 | 版本兼容规则 | 合法/非法示例 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `schemaVersion` | string | 是 | — | `ird-golden-manifest/<n>` | 主版本未知→装载失败（datasetInvalid）；次版本新增字段可选 | major=破坏性；minor=追加可选字段 | 合法 `ird-golden-manifest/1`；非法 `ird-golden/1` |
| `datasetId` | string | 是 | — | `[a-z0-9-]{3,64}` | 与目录名一致；全局唯一（lint 校验） | 不随版本变更 | `kin-fk-planar-2r`；非法 `Kin/FK` |
| `version` | string | 是 | — | 语义化 `M.m.p` | 与版本目录名一致 | 内容任何变化至少 minor+1；语义变化 major+1 | `1.2.0` |
| `kind` | string | 是 | — | §4.2.1 五 token 之一 | — | 换类＝新 datasetId | `analytic-case` |
| `scenarioCategory` | string | 是 | — | `[a-z0-9-]{1,64}` | 场景分类（域前缀建议：`kin/`、`trj/`、`dyn/`…） | — | `kin/fk-forward` |
| `coveredRequirements` | string[] | 是（≥1） | — | 需求 ID 句法 | 逐项对 REQUIREMENTS 现行 ID lint | — | `["KIN-01","ARC-03"]`；非法 `["KIN-99"]`（lint 未命中） |
| `coveredAt` | string[] | 否 | `[]` | `AT-xx` | 同上 | — | `["AT-16"]` |
| `toleranceProfile` | object | 是 | — | `{id, version}` | 须存在于 `testdata/tolerance/`；档案条目须覆盖本集全部参考字段（lint） | 档案版本升级须重审 | `{"id":"kin-fk","version":"1.0.0"}` |
| `inputs` | string[] | 是（≥1） | — | 版本目录内相对路径 | 逐文件在 `integrity` 登记 | — | `["inputs/planar-2r.json"]` |
| `expected` | string[] | 是（≥1） | — | 同上 | 同上；`regression`/`performance-baseline` 亦填（快照即期望） | — | `["expected/fk-tables.csv"]` |
| `parameters` | object | 否 | `{}` | 算例参数（JsonLite 值） | 领域自定义；键须在生成脚本中消费 | — | `{"link1":0.5}` |
| `units` | object | 是 | — | `{field: UnitToken}` | token 须在 core 单位表注册（装载时校验）；缺某字段单位→datasetInvalid | — | `{"position":"m","angle":"rad"}`；非法 `{"x":"meter"}` |
| `frames` | object | 是 | — | 坐标系约定声明 | 至少含 `base`；涉及世界系/TCP 时必含 `world`/`tcp`；语义引用 core.md §4.6（`T_ab`＝b 相对 a） | — | `{"base":"robot-base","world":"installation"}` |
| `referenceSource` | object | 条件 | — | `{method, scope, independentOfProductionImpl, description}` | `analytic-case`/`reference-impl` **必填**且 `independentOfProductionImpl=true`；`regression` 必填且 `=false`；`method`∈{`closed-form`,`textbook`,`independent-program`,`production-snapshot`,`hand-built`,`measured`} | — | 见附录 A.2 |
| `producer` | object | 是 | — | 见下表 | 记录生成环境（复现要素） | — | 见附录 A.2 |
| `edgeCases` | object | 条件 | — | `{zeroValue, nearZero, signCancellation, sampleRefs[]}` | `analytic-case`/`reference-impl` **必填且三布尔至少全 true**（附录 D C4："黄金数据集必含零值/近零值/正负抵消样例"——lint 强制）；`sampleRefs` 指向 expected 内具体条目 | — | `{"zeroValue":true,…"sampleRefs":["expected/edge.json#zero"]}` |
| `integrity` | object[] | 是（≥1） | — | `{path, sha256, sizeBytes}` | 覆盖 inputs+expected+generate 全部文件；装载时逐文件校验（core `ContentDigester`） | 任一不符→datasetInvalid | `{"path":"inputs/a.json","sha256":"<64hex>","sizeBytes":812}` |
| `generator` | object | 是 | — | `{script, invocation, committed}` | `script` 相对路径（`generate/` 内）；`committed=true` 要求脚本入库；`analytic-case` 的 `script` 可为推导文档 | — | `{"script":"generate/make_fk.py","invocation":"python make_fk.py","committed":true}` |
| `history` | object[] | 是（≥1） | — | `{version, date, change, reReviewedBy, supersededBy}` | 首版也登记；`supersededBy` 保留旧基线链（§4.7） | — | 见 §4.7 |

`producer` 子字段：`softwareVersion`（生成软件/工具版本，`reference-impl` 填参考实现版本）、`algorithmId`（算法稳定标识）、`solverConfig`（求解配置快照：IK 容差/迭代数等，JSON 值）、`seed`（uint64，可选；随机性参与时必填）、`threadCount`（int ≥1，默认 1）、`generatedAtUtc`（ISO-8601）。

装载接口（签名见 §5.1）在任何字段违反上表时抛 `TestKitError`（kind=`dataset-invalid`），**不猜测、不补默认关键项**；`coveredRequirements`/`coveredAt` lint 需求 ID 字典随 REQUIREMENTS 修订同步（lint 数据文件 `testdata/requirements-ids.json`，TK-T03 维护流程见 §9）。

#### 4.2.3 与既有测试资源先例的关系

RobWorkStudio gtest 的 `testfiles/`＋`TestSuiteConfig.xml` 生成模式服务于基线框架自身测试；industrialrobot 数据集不共用该目录（避免与基线框架耦合），但沿用"编译期生成配置＋运行期路径解析"的思想（§4.6）。

### 4.3 容差档案（ToleranceProfile）

档案文件 `testdata/tolerance/<profileId>/v<version>.json`，schemaVersion `ird-tolerance-profile/1`。

#### 4.3.1 档案字段表

| 字段 | 类型 | 必填 | 默认 | 约束 | 合法/非法示例 |
| --- | --- | --- | --- | --- | --- |
| `schemaVersion` | string | 是 | — | `ird-tolerance-profile/<n>` | `ird-tolerance-profile/1` |
| `profileId` | string | 是 | — | `[a-z0-9-]{3,64}`，与目录一致 | `kin-fk` |
| `version` | string | 是 | — | `M.m.p` | `1.0.0` |
| `basis` | string | 是 | — | 依据声明：引用附录 D 项号（`appendixD#4` 等）＋类别 | `"appendixD#9 + #4/#5"` |
| `entries[]` | object[] | 是（≥1） | — | 见下表 | — |

`entries[]` 子字段：

| 字段 | 类型 | 必填 | 约束 |
| --- | --- | --- | --- |
| `fieldPath` | string | 是 | 结果字段路径模板，如 `fk[*].tcp.position.x`；`*`＝逐元素（C4 逐元素口径的档案表达） |
| `quantityKind` | string | 是 | core `QuantityKind` token（`length`/`angle`/…） |
| `unit` | string | 是 | core `UnitToken`（该字段参考值的声明单位；比较前经 core 唯一换算入口转 SI） |
| `tolerance` | `{relative, absolute}` | 是 | 两分量 ≥0 且有限（与 core `Tolerance::make` 同校验）；**导出量（时间/功率等）的 absolute 必须由数据集/档案逐例声明**，未声明→`tolerance-undefined` 错误（C4：不得静默通过） |
| `source` | string | 是 | `appendixD-fixed`／`analysis-config-default`／`engineering-policy-default`／`dataset-declared` 四者之一（附录 D 类别说明的档案化） |
| `allowedMax` | `{relative, absolute}` | 条件 | **声明上限**：`appendixD-fixed` 条目＝附录 D 对应值（**数据集只准更严、不准放宽**）；`dataset-declared` 条目必填并给出依据（如参考实现的数值精度声明） |

#### 4.3.2 三类默认的不同用途（逐项承接附录 D；类别语义＝附录 D"类别说明"原文）

| 类别 | 档案中的角色 | 测试消费方式 | testkit 禁止事项 |
| --- | --- | --- | --- |
| **固定**（第 4/5/6/7/8/9/10/12 项） | `source=appendixD-fixed`，`allowedMax`=附录 D 值 | 断言直接使用；数据集可在**更严**方向覆盖（第 9 项明文允许"黄金算例可自带更严值并在数据集内声明"） | 不提供任何放宽通道；lint 发现 `tolerance > allowedMax` → 档案非法 |
| **分析配置默认**（第 1/2/3 项） | `source=analysis-config-default`，登记默认值与配置项名（KIN-13） | 测试经被测单元的求解配置对象设值；数据集 manifest `producer.solverConfig` 记录实际使用值；默认值校验用（断言配置对象默认值＝附录 D） | 不把默认值拷贝为第二权威——数值以被测单元配置对象为准，档案只登记与核对 |
| **工程策略默认**（第 11 项） | `source=engineering-policy-default`，登记策略键与默认值（4π） | 测试经被测 policy 对象取值（ARC-05 唯一权威）；档案记录策略快照 | 同上；测试不得旁路策略对象私设阈值 |

#### 4.3.3 比较语义细则（全部经 core 实现，档案只携带数值）

- **公式**：`|a−b| ≤ ε_rel·|ref| ＋ ε_abs`（core `closeWithin`；ref＝参考值，零参考退化为 ε_abs，不要求精确零）——附录 D C4。
- **逐元素**：向量/序列/矩阵逐项比较（`fieldPath` 的 `*` 展开；core `allCloseWithin` 同语义）；不以差值总和替代。
- **单位/量纲**：actual/expected 先按条目 `unit` 换算到 SI（core `convert`）；`quantityKind` 与字段实际量纲不符→`unit-mismatch` 错误（数据集非法级，非容差失败级）。
- **零值/近零值**：公式固有处理＋数据集 `edgeCases` 强制覆盖（§4.2.2）。
- **三级断言不可混用**（§5.3 宏一一对应）：
  - 容差匹配（`IRD_EXPECT_CLOSE` 族）——用于连续量对照；
  - 上界校验（`IRD_EXPECT_AT_MOST`）——用于"≤阈值"类判定（残差、误差度量、内存、耗时上限）；
  - 精确相等（`IRD_EXPECT_IDENTICAL`）——用于身份/名称/枚举 token（附录 D 第 12 项：无容差）。
  交叉使用（如对 ObjectId 用容差匹配、对浮点值用精确相等）由 lint/断言内部类型规则拒绝（§5.3 表）。
- **失败行为总表**（任务约束§四.2 末条）：

| 情形 | 行为 | 结果分类 |
| --- | --- | --- |
| actual 或 expected 为 NaN/±Inf | 断言失败，reason=`non-finite`（**不静默通过**，NFR-COR-03） | 测试失败 |
| 长度不匹配（序列比较） | 失败，列缺失/额外索引清单 | 测试失败 |
| 期望非空而实际为空集合 | 失败（缺失＝全部期望元素） | 测试失败 |
| 双方皆空（集合等价断言） | 通过（平凡等价）；顺序断言要求索引一致，双方空亦通过 | — |
| 单位 token 未注册/量纲不符 | 抛 `TestKitError(unit-mismatch)` | **数据集非法**（§7.2 分类） |
| 容差条目缺失（fieldPath 无解析） | 抛 `TestKitError(tolerance-undefined)`（C4：报错不默认） | 数据集非法 |
| 声明值超 `allowedMax` | 档案装载失败 | 数据集非法 |

**职责分界复述**：数值基础（公式、逐元素、ε_abs 量纲默认）＝core；testkit＝档案加载、参考数据匹配（fieldPath→值提取由适配器提供，§5.3.4）、断言组织与失败报告；**产品运行校验不依赖 testkit 或黄金数据集**（C7 原文口径；testkit 不为产品提供任何运行时比较路径）。

### 4.4 数据集生成脚本与独立性

- `generate/` 内脚本必须可**独立重跑**产出与 `expected/` 逐字节一致（analytic/reference 类）或在声明星子/线程下容差内一致（regression 类）的结果；
- `reference-impl` 的参考实现源码或其版本锚（commit/tag）随 `generate/` 入库；
- `independentOfProductionImpl=true` 的审计口径：参考实现不得 import/include 生产单元代码；lint 不做静态证明，由审核人在 `history.reReviewedBy` 留痕负责。

### 4.5 资源完整性校验

装载时逐文件：size＋SHA-256（core `ContentDigester`）；`manifest.json` 自身不入 `integrity`（由 git 版本化保证）；校验失败→datasetInvalid（测试结果分类为"数据集非法"而非"测试失败"，§7.2——避免把数据损坏误报为算法回归）。CI 可独立运行 `tools/testdata` 校验工具全量扫描（TK-T03）。

### 4.6 数据根定位（TestPaths）

```cpp
// TestPaths.hpp
std::filesystem::path goldenDataRoot();   // 环境变量 SDURWS_IRD_TESTDATA_DIR 优先，回落编译期 CMake 默认
std::filesystem::path datasetDir(std::string_view datasetId);
std::filesystem::path toleranceProfileDir(std::string_view profileId);
```

未设环境变量且编译默认不存在→`TestKitError(env-unavailable)`（环境不可用级，§7.2）。CI：源内默认即开箱可用；外置数据时设一次变量。

### 4.7 数据集变更、重新审核与旧基线保留

- **变更触发重新审核**：算法/编译链变更影响结果（生产侧）、参考来源更新、需求容差修订（附录 D 变更）、core 比较语义变更；
- **旧基线保留**：每版本一目录（§3.4）；至少保留最近一个被取代版本；更早版本删除须在 `history` 留痕（删除人/理由）；git 历史天然保全提交层历史；
- `history[]` 每版一条：`{version, date, change, reReviewedBy, supersededBy}`；`reReviewedBy`＝审核人（域详设所有者或其委托人），解析算例数值变化必须有 change 说明。

---

## 5. 公共接口与断言设计

约定（适用本章）：全部类型值语义、可拷贝移动；函数除注明外不抛非 TestKitError 异常；线程安全＝并发只读安全，无共享可变状态。错误类型：

```cpp
// 位于 Dataset.hpp（全 testkit 共用）
enum class TestKitErrorKind { DatasetInvalid, ToleranceUndefined, UnitMismatch, EnvUnavailable, Usage };
class TestKitError : public std::runtime_error { public: TestKitError(TestKitErrorKind, std::string detail); TestKitErrorKind kind() const noexcept; };
```

### 5.1 Dataset.hpp（装载与完整性）

```cpp
struct DatasetRef { std::string datasetId; std::string version; /* 语义化版本 */ };

class GoldenDataset {
public:
    // 前置：manifest 位于 <root>/golden/<datasetId>/manifest.json，版本目录存在
    // 行为：解析→schema 校验→integrity 全量校验（§4.5）→字段交叉校验（§4.2.2）
    // 错误：任何校验失败抛 TestKitError(DatasetInvalid)（含字段路径与原因）；
    //       数据根不可达抛 TestKitError(EnvUnavailable)
    static GoldenDataset load(const DatasetRef& ref);

    const DatasetManifest& manifest() const noexcept;
    std::filesystem::path resolveInput(std::string_view relPath)  const; // 前置：relPath∈inputs
    std::filesystem::path resolveExpected(std::string_view relPath) const;
    const ToleranceProfile& toleranceProfile() const noexcept;           // 随 load 一并装载
};
```

所有权：返回值语义；确定性：同目录内容装载结果逐字节一致（不含时间戳）；调用示例见附录 A.2。

### 5.2 ToleranceProfile.hpp（档案解析）

```cpp
struct ToleranceEntry {                 // §4.3.1 的 C++ 投影
    std::string fieldPath;              // 含 '*' 逐元素模板
    core::QuantityKind kind;
    core::UnitToken unit;
    core::Tolerance tolerance;
    ToleranceSource source;             // AppendixDFixed | AnalysisConfigDefault | EngineeringPolicyDefault | DatasetDeclared
    std::optional<core::Tolerance> allowedMax;
};
struct ToleranceProfile {
    std::string profileId, version, basis;
    std::vector<ToleranceEntry> entries;

    static ToleranceProfile load(const std::filesystem::path& profileVersionJson); // 校验失败抛 TestKitError
    // 解析 fieldPath（已展开 '*' 为具体索引，如 "fk[3].tcp.position.x"）：
    //   命中→条目；未命中→抛 TestKitError(ToleranceUndefined)（不默认、不通过——C4）
    //   allowedMax 存在且条目超限→装载期已拒绝，此函数不会返回超限值
    const ToleranceEntry& resolve(std::string_view concreteFieldPath) const;
};
```

### 5.3 Check.hpp（数值断言与比较详情）

#### 5.3.1 数据类型

```cpp
struct CompareDetail {                  // 单个失败点的机器可读详情（§7.2 ComparisonRecord 的来源）
    std::string fieldPath;              // 具体路径（含索引）
    std::size_t elementIndex = 0;       // 无索引语义时 0 且 hasElement=false
    bool hasElement = false;
    double actual = 0.0, expected = 0.0, diff = 0.0;   // diff = |a-b|（SI）
    std::string unit;                   // SI 规范 token（附录 D 比较在 SI 域执行）
    core::Tolerance tolerance;          // 采用的容差
    std::string toleranceSource;        // appendixD-fixed / dataset-declared / …
    std::string reason;                 // non-finite / exceeds-tolerance / length-mismatch / empty-actual / …
};
struct CheckResult { bool passed = true; std::vector<CompareDetail> failures; };
```

#### 5.3.2 函数族（语义实现＝core；testkit 只组织与报告）

```cpp
// 标量容差匹配（value=实际，reference=参考＝公式中的 ref）
CheckResult checkCloseWithin(std::string_view fieldPath, double actualSi, double referenceSi,
                             core::Tolerance t, std::string_view sourceTag);
// 序列/向量逐元素（长度不等→失败并枚举缺失/额外索引；空实际非空期望→失败；双空→通过）
CheckResult checkAllCloseWithin(std::string_view fieldPathTemplate,
                                const std::vector<double>& actualSi, const std::vector<double>& referenceSi,
                                core::Tolerance t, std::string_view sourceTag);
// 上界校验：actualSi ≤ boundSi（不与容差匹配混用；非有限→失败）
CheckResult checkAtMost(std::string_view fieldPath, double actualSi, double boundSi, std::string_view unit);
// 精确相等：身份/名称/token（附录 D 第 12 项；仅接受字符串/整数句法域，模板重载）
CheckResult checkIdentical(std::string_view fieldPath, std::string_view actual, std::string_view expected);
CheckResult checkIdentical(std::string_view fieldPath, std::int64_t actual, std::int64_t expected);
```

#### 5.3.3 宏（gtest 适配层 `gtest/AssertMacros.hpp`；展开于消费方 TU，要求已链 gtest；Check.hpp 本体零 gtest）

```cpp
IRD_EXPECT_CLOSE(fieldPath, actualSi, referenceSi, profile, unitToken)   // 容差经 profile.resolve(fieldPath)
IRD_EXPECT_ALL_CLOSE(fieldPathTemplate, actualVecSi, referenceVecSi, profile, unitToken)
IRD_EXPECT_AT_MOST(fieldPath, actualSi, boundSi, unit)
IRD_EXPECT_IDENTICAL(fieldPath, actualText, expectedText)
IRD_EXPECT_FINITE(fieldPath, value)                                      // NaN/Inf 守卫（前置检查）
```

宏行为：失败时逐 CompareDetail 输出 `ADD_FAILURE_AT`（文件/行＋字段路径＋actual/expected/unit/容差/差值），并把详情写入本测试的 TestRecord（§7.3）；容差缺失/单位不匹配**不产生断言失败**而是抛 TestKitError（数据集非法级，由夹具转 EnvUnavailable/DatasetInvalid 结果，§7.2）——两类失败不得混淆。

#### 5.3.4 类型规则与参考数据匹配

| 值域 | 允许的断言 | 拒绝（编译期/装载期） |
| --- | --- | --- |
| 浮点连续量 | CLOSE 族、AT_MOST | IDENTICAL（禁浮点精确相等——NaN 语义与需求容差体系不符） |
| ObjectId/名称/token/枚举文本 | IDENTICAL | CLOSE（身份无容差，附录 D 第 12 项） |
| 计数/长度/版本号 | IDENTICAL（int 重载） | CLOSE |

从参考文件到比较值的提取（CSV/JSON 行→向量）由各域测试提供**适配器**（读取器归域，testkit 只接 `std::vector<double>`/JsonLite 值）；`fieldPath` 命名约定：`<结果域>.<对象/元素>[*].<字段>`，首版登记于各域数据集，跨域一致由 lint 保留字表约束（`fk`/`ik`/`trj`/`dyn`/`sel`/`opt`）。

### 5.4 SetCheck.hpp（稳定集合与顺序断言）

#### 5.4.1 前提声明：容差匹配不是等价关系

`closeWithin` 定义的"相近"不具备传递性（A≈B ∧ B≈C ⇏ A≈C），因此：

- **排序后逐项比较不可作为集合等价判定**（两两错位会误判；本文 TK-SET-3 用例钉住该反例）；
- **一个参考元素不得匹配多个实际元素**——采用**一一配对**（匹配）语义；
- 判等准则＝**二分图存在完美匹配**（全覆盖双方），这是"存在一一配对使全体满足容差"的准确语义。

#### 5.4.2 匹配规则（两阶段）

```
阶段 0 重复检测：
  expected 内身份键重复 → 装载/调用期即 DatasetInvalid（参考集自相矛盾）；
  actual 内身份键重复（expected 无对应重复计数）→ 失败，reason=duplicate-identity，逐键列出。
阶段 1 稳定身份匹配（身份键存在时）：
  身份键精确相等（ObjectId 规范文本等；附录 D 第 12 项）——O(n) 哈希；
  身份相等但数值字段超容差 → 失败，reason=identity-value-mismatch（定位到元素与字段）。
阶段 2 数值一一匹配（无身份键的元素）：
  相容边＝全部声明数值字段经 profile 容差满足（逐元素，C4）；
  匹配＝Hopcroft–Karp 最大匹配；equivalent ⇔ 匹配覆盖双方全部未定身份元素
  （连同阶段 1 结果覆盖双方全部元素）。
  多个完美匹配存在 → 仍判通过，但报告 ambiguousMatch=true（附一组见证配对）；
  报告生成采用确定性 tie-break（按身份键/首个数值字段排序），保证同输入同报告。
复杂度：阶段 1 O(n)；阶段 2 建图 O(n·m)＋匹配 O(E√V)；
  规模护栏：单次断言元素数默认上限 100,000，超限抛 TestKitError(Usage)
  （性能基准的大规模数据不得用本断言——按索引逐对或摘要比较，§5.3）。
```

失败定位输出：`missing`（期望未匹配，逐个列出其身份/最近实际距离）、`extra`（实际未匹配）、`mismatched`（参与相容边计算但无法配对的最近对详情，复用 CompareDetail）；三者合成 SetCheckResult。

#### 5.4.3 两种断言的区分

| 断言 | 语义（"候选集合等价"） | 语义（"稳定排序一致"） |
| --- | --- | --- |
| 问题 | 实际集合与参考集合含相同元素（不论顺序） | 实际序列在**指定排序规则下**与期望顺序逐位一致 |
| 匹配 | §5.4.2 一一配对 | 按索引逐位（身份精确相等；数值可按位容差） |
| 顺序敏感 | 否 | 是（首个错位索引报出） |
| 典型用途 | IK 解集对照（AT-03：同位姿不同构型均保留）、Pareto 候选集 | NFR-COR-02 稳定排序验证（同输入两次运行顺序一致）、KIN-02 稳定编号 |
| 互替 | **不可**：集合通过＋顺序失败是合法结果（元素对但序错） | 同左 |

```cpp
template <class T> struct SetMatchTraits {
    static std::optional<std::string> identity(const T&);      // 稳定身份键（缺省 nullopt→走数值匹配）
    static std::vector<NumericFieldView> numerics(const T&);   // {fieldPath, valueSi} 列表（容差经 profile 解析）
};
struct SetCheckResult {
    bool equivalent = true;
    std::vector<std::size_t> missingExpected, extraActual;     // 索引
    std::vector<MatchPair> matched;                            // 见证配对（报告用）
    bool ambiguousMatch = false;
    std::vector<CompareDetail> mismatchedPairs;                // 最近对失败详情
    std::vector<std::string> duplicates;
};
template <class T>
SetCheckResult checkSetEquivalent(const std::vector<T>& expected, const std::vector<T>& actual,
                                  const ToleranceProfile&, const SetMatchTraits<T>&);
struct OrderCheckResult { bool sameOrder = true; std::size_t firstDivergence = 0; std::vector<CompareDetail> details; };
template <class T>
OrderCheckResult checkStableOrder(const std::vector<T>& expectedOrder, const std::vector<T>& actual,
                                  const SetMatchTraits<T>&, const ToleranceProfile&);  // 数值按位容差；身份按位精确

IRD_EXPECT_SET_EQ(expected, actual, profile, TraitsT)
IRD_EXPECT_STABLE_ORDER(expectedOrder, actual, profile, TraitsT)
```

#### 5.4.4 身份匹配 vs 数值匹配的选择与歧义处理

- 有稳定身份（ObjectId/稳定编号）**必须**用身份匹配（确定性、O(n)、无歧义）；数值匹配仅用于参考数据不带身份的场景（如解析算例输出的裸数值表）；
- 歧义（多个完美匹配）**不判失败**（等价关系成立），但报告 `ambiguousMatch`——持续歧义提示参考数据区分度不足，建议数据集增强（登记于报告 notes，域所有者决定是否改进数据集）；
- 不提供"贪心最近邻"模式（会产生次优误判，违背一一配对原则）。

#### 5.4.5 业务断言适配器归域（不搬入 testkit）

Pareto 非支配验证（OPT-02）、IK 去重口径（附录 D 第 3 项关节空间逐轴）等是**业务规则**：各域测试以其类型特化 `SetMatchTraits`（如 kinematics 以"逐轴 1×10⁻⁶ 内视为同构型"构造 expected 去重后的参考集，再与 actual 集合断言等价）。testkit 不包含任何"Pareto""去重"逻辑（自审项 A-6）。

### 5.5 ContractCheck.hpp（契约通用断言）

经模板/访问器消费 core 类型（testkit 不依赖 evidence 等产品单元；evidence 契约测试在自己的 `_contract_test` 目标内将其类型喂入以下泛型断言）：

```cpp
// 诊断记录（core::DiagnosticRecord，状态：core.md v0.1 Draft）
CheckResult checkDiagnosticRecord(const core::DiagnosticRecord& r, DiagnosticCheckOptions opt);
  // 校验：code 句法；稳定诊断项 subject 必填（opt.allowTransient 时可空）；context/cause/recommendedAction 非空；
  //       比较型条目 comparison 存在且三要素完整（actual/expected/unit，unit 已注册）；
  //       NotApplicable 值不得伪造数值（SourcedValue 状态检查）——ERR-01/UX-03。
CheckResult checkComparativeFields(const core::ComparativeFields& f, core::QuantityKind expectedKind);
  // 量纲与 unit.token 的 kind 一致（expectedKind 由调用域给出）。
CheckResult checkTaskIdentity(const core::TaskIdentity& id);      // 五元组全 isValid（TASK-03）
CheckResult checkStableIdsUnique(const std::vector<core::ObjectId>& ids);  // 重复→失败并列出重复项
CheckResult checkFileIntegrity(const std::filesystem::path& file, std::string_view sha256Expected); // core::ContentDigester
// 结果包络合法组合的通用谓词（evidence 侧提供访问器）：
template <class EnvelopeT, class Accessors>
CheckResult checkEnvelopeCombination(const EnvelopeT& e, Accessors a);
  // §8.1 表 3：outcome∈{Canceled,Failed,Interrupted} ⇒ engineeringStatus==NotApplicable 且无正式结论字段；
  //           Completed ⇒ payload 含证据清单；DataInsufficient ⇒ 缺失项全量清单存在。
```

---

## 6. 夹具、故障注入与资源生命周期

### 6.1 夹具生命周期（GoldenFixture）

四阶段：**建立→执行→断言→清理**。建议用法＝继承 `::testing::Test` 的 `GoldenFixture`：

```
SetUp()      ① TestPaths 解析数据根（env）→ ② GoldenDataset::load（含完整性）→
             ③ ToleranceProfile 就绪 → ④ DeterministicEnv 建立（种子/线程）→
             ⑤ TempDir 建立 → ⑥ TestRecord 初始化（测试 ID/需求/AT/数据集引用登记）
测试体       业务计算＋§5 断言
TearDown()   ⑦ TestRecord 定稿写出 → ⑧ TempDir 清理（失败保留，§6.2）→ ⑨ 环境复位
```

②③失败（数据集非法/环境不可用）**不进入测试体**，结果分类见 §7.2（不是 Passed）。夹具不实现项目事务/写锁/调度（§2.2）；各域契约套件的"业务阶段"（建立项目、产生修订…）归各自测试目标编写，GoldenFixture 只提供上述通用六步。

### 6.2 临时目录与资源隔离

```cpp
class TempDir {
public:
    explicit TempDir(std::string_view tag);     // <系统临时目录>/ird-test/<pid>-<tag>-<随机后缀>
    ~TempDir();                                 // RAII 递归删除；删除失败记录告警不抛出
    const std::filesystem::path& path() const noexcept;
    void keepOnFailure(bool);                   // 默认 true：测试失败时保留现场并在报告 artifacts 登记
};
```

规则：测试不得写共享位置（源码树/用户目录）；需要文件系统现面的被测代码一律指向 TempDir；进程测试的子进程工作目录＝各自 TempDir（§6.5）。并行测试（ctest -j）安全：目录按 pid＋tag＋随机后缀隔离。

### 6.3 确定性环境与重放（DeterministicEnv／ReproRecord）

```cpp
struct ReproRecord {                    // 复现记录（§7.1；亦随 TestRecord 持久化）
    std::uint64_t seed = 0;             // 默认 20260909（固定值：无随机性测试也要可复现）
    int threadCount = 1;                // 默认 1；多线程归约测试显式设值（附录 D 第 8 项）
    std::string softwareVersion;        // 被测目标版本（CI 注入或编译期宏）
    std::string gitCommit;              // 编译期宏 IRD_GIT_COMMIT
    DatasetRef dataset;
    std::string toleranceProfile;       // "<id>@<version>"
    std::string notes;
    std::string toJson() const; static ReproRecord fromJson(std::string_view);
};
class DeterministicEnv {
public:
    explicit DeterministicEnv(ReproRecord r);   // 记录＝测试的确定性上下文唯一来源
    const ReproRecord& record() const noexcept;
    // 约束：种子经被测代码的注入点传入（各域 solver 接口显式收 seed），
    //       testkit 不改写全局 rand；线程数经被测执行接口显式设置。
};
```

重放验证＝同一 ReproRecord 下两次运行：结果集合等价＋稳定排序一致＋逐元素在档案容差内（**不要求浮点文件逐字节相同**，NFR-COR-02/附录 D 第 8 项）。跨进程复现：ReproRecord JSON 可独立传给另一次运行（附录 A.4）。

### 6.4 故障注入（进程内原语）

**接缝原则（依赖方向安全）**：生产代码**不包含** testkit 头、不加 `#ifdef TEST`；故障接缝＝生产代码本就要求的窄接口（ARCH §7.2 端口、io 写入口、execution 派发/通道接口）。fake 实现编译进**测试目标**，实现这些消费者自有接口：

```cpp
// Fault.hpp —— 计划与观测（机制）；适配器模板把计划接到具体接口
struct FaultTrigger { std::string faultPointId; std::uint64_t occurrence = 1; }; // 第 occurrence 次命中触发
struct FaultPlan  { std::vector<FaultTrigger> triggers; };
struct FaultLog   { std::vector<std::pair<std::string, std::uint64_t>> hits; };  // 命中序列（观测）

template <class I>                     // I＝被测单元声明的接口（如 IFileWriter、IChannel）
class FaultInterceptor {
public:
    FaultInterceptor(std::shared_ptr<I> real, FaultPlan plan);
    FaultLog log() const;
    // 用法：测试目标内实现 I，方法内先 shouldFire("io/write-chunk", n) 决定注入失败（抛被测接口声明的错误类型）
};
```

- **故障点标识登记**：`<unit>/<接口>/<动作>`（如 `io/file-writer/write-chunk`）；接缝最小集与各单元义务登记于 §10.2（io：文件写入器；execution：进程启动/通道发送；project：经 io 间接）；
- **文件系统故障**覆盖：打开失败/写中途失败/磁盘满模拟（写入 N 字节后失败）/rename 失败——驱动 AT-13（保存事务：旧版本完整、未提交版本忽略）类测试；
- **构建边界**：fake 只在 `_test`/`_contract_test` 目标；生产库无感知（T-1 红线不变）。

### 6.5 进程测试支撑（设计冻结；实现按消费者阶段需要交付）

```cpp
// ProcessRunner.hpp
struct ProcessSpec { std::filesystem::path executable; std::vector<std::string> args;
                    std::map<std::string, std::string> env; std::chrono::milliseconds timeout; };
enum class ProcessExitKind { Exited, Crashed, TerminatedByTimeout, Killed };
struct ProcessOutcome { ProcessExitKind kind; int exitCode = 0; std::chrono::milliseconds duration{}; };
class EventWatch {   // 可观察事件（跨进程同步唯一判据）
public:
    bool awaitFile(const std::filesystem::path& marker, std::chrono::milliseconds deadline);   // 有界等待
    bool awaitLineInFile(const std::filesystem::path& log, std::string_view needle, std::chrono::milliseconds deadline);
};
class TestProcessRunner {
public:
    void start(const ProcessSpec&, std::filesystem::path workingDir /*= TempDir*/);
    ProcessOutcome outcome();     // 超时→终止进程树后返回 TerminatedByTimeout
    void kill();                  // 主动终止（崩溃注入）
};
```

- **崩溃/取消/超时/框架错误区分**：崩溃＝子进程非零退出/无正常完成事件；取消＝被测取消协议的事件序列＋进程自然退出（UX-03：正常取消不是错误）；超时＝deadline 内未见完成事件→TerminatedByTimeout（**区别于被测行为失败**）；测试框架自身错误（gtest 崩溃、夹具异常）＝EnvUnavailable，不得计入被测失败；
- **超时后回收**：Windows Job Object 绑子进程树，TerminateJobObject 兜底；TempDir 失败保留（§6.2）；
- **同步纪律**：正确性判据**只**来自可观察事件（事件文件/日志标记），禁止固定 sleep 后断言（有界轮询是 EventWatch 的实现细节，不是判据）；
- **真实崩溃 vs 文件系统故障的覆盖分工**：文件系统故障（§6.4）验证**逻辑鲁棒性**（错误路径、事务回滚）；真实进程崩溃（kill/访问违例触发）验证**进程隔离与恢复**（NFR-REL-02/03、AT-11）——两者不互相替代；
- **交付节奏**：阶段 A 仅交付 §6.4 进程内原语；TestProcessRunner 实现挂接 project/execution 契约测试的实际需要（AT-11/13 载体，§10.2），当前不建目标、不写空测试（任务约束§八）。

### 6.6 时钟与执行控制的测试适配

被测单元若需可注入时钟/执行控制，接口由**消费者单元**定义（其详设登记）；testkit 提供通用 fake 模式（不绑定产品接口）：

```cpp
struct ManualClock {                       // 满足 std::chrono 风格 Clock 概念（now()）
    std::chrono::steady_clock::time_point now() const noexcept;
    void advance(std::chrono::milliseconds);   // 测试推进虚拟时间（超时/心跳类场景）
};
```

心跳超时、暂停窗口等**场景逻辑**归 execution/project 契约测试编写（§2.2 排除项）。

### 6.7 Windows 测试执行规则（GUI 与无界面测试的区分）

依据用户级 `AGENTS.md`（`C:\Users\zgl18\.codex\AGENTS.md`，实测已读；本节为**流程设计**，本阶段不启动任何 GUI 测试）：

| 规则 | 内容 | 适用对象 |
| --- | --- | --- |
| GUI 测试环境 | RobWorkStudio Qt Widgets/GUI 测试在 **Visual Studio x64 开发环境**运行；设置 `QT_QPA_PLATFORM=windows` | 仅含 Widgets/GUI 的测试可执行文件（未来 ui/workflow 测试目标） |
| 单实例启动 | 每次仅通过**绝对路径**启动**一个** GUI 测试可执行文件；不合并 Widget/Meta 测试可执行文件启动 | 同上 |
| offscreen 禁用 | 不使用 `QT_QPA_PLATFORM=offscreen` | 同上 |
| 平台插件故障处置 | Qt 报平台插件初始化失败→停止受影响进程→检查继承的 `QT_*/QML_*` 环境变量→按上述设置重启 | 同上 |
| 模型测试豁免 | QCoreApplication 模型测试不要求 GUI 平台插件 | **无界面测试**（NFR-MNT-01"计算内核可由模型测试直接调用"——industrialrobot 全部 L2 计算库测试属此类，不需要 Qt 平台插件、不涉上述 GUI 规则） |

industrialrobot 测试目标分层登记：`*_test`/`*_contract_test`（计算库）＝无界面（本单元阶段 A 全部如此）；未来 GUI 测试单列目标并在其 CMake 注释引用本节。ctest 并行调度 GUI 目标时须以串行标签（LABELS `ird_gui`）约束，避免同机多 GUI 实例。

---

## 7. 确定性、重放与结果报告

### 7.1 确定性与重放

- 复现四要素＝ReproRecord（种子/线程/版本/数据集引用）＋被测配置快照（manifest `producer.solverConfig`）；
- 复现判据（NFR-COR-02）：等价候选集合（§5.4 集合断言）＋稳定排序一致（顺序断言）＋逐元素容差内（附录 D 第 8 项并行归约 1×10⁻¹² 相对＋ε_abs）；
- 浮点结果文件**不要求**逐字节相同（需求原文；报告与断言均按值比较）；
- ReproRecord 独立成文件（`repro.json`）随失败报告 artifacts 归档（附录 A.4）。

### 7.2 机器可读测试结果最小契约（TestRecord）

每测试一条记录，运行结束聚合为 `ird-test-report.json`（与 gtest XML 并存，§7.3）。

| 字段 | 类型 | 必填 | 说明 |
| --- | --- | --- | --- |
| `testId` | string | 是 | `<suite>.<case>`（gtest 全名） |
| `requirementIds[]` | string[] | 否 | 经 IRD_TEST_INFO 登记（§7.3）；建议 P0 测试必填（lint 提示，不强制失败） |
| `atIds[]` | string[] | 否 | 同上 |
| `dataset` | `{id, version}` | 条件 | 使用黄金数据时必填 |
| `toleranceProfile` | string | 条件 | `<id>@<version>` |
| `repro` | ReproRecord | 是 | 种子/线程/版本/commit |
| `environment` | `{compiler, qtVersion?, arch, os, machineTag}` | 是 | CI 注入；qtVersion 仅 Qt 相关目标 |
| `outcome` | enum | 是 | `passed`/`failed`/`skipped`/`notRun`/`envUnavailable`/`datasetInvalid`（六值，见下分类） |
| `reason` | string | 条件 | 非 passed 时必填（机器前缀＋人读说明） |
| `comparisons[]` | CompareDetail[] | 否 | 全部失败比较点（字段/索引/实际/期望/单位/容差/来源/差值） |
| `failureLocation` | `{path?, elementIndex?, setRole?}` | 否 | 失败对象定位（集合断言含 missing/extra/mismatched 角色） |
| `artifacts[]` | `{path, kind}[]` | 否 | TempDir 现场/repro.json/子进程日志等 |
| `durationMs` | int | 是 | — |

**四类状态区分**（任务约束§四.6；Skipped/NotRun **不得**计为 Passed——聚合统计分列）：

| 类别 | outcome | 触发 | 处置 |
| --- | --- | --- | --- |
| 测试失败 | `failed` | 断言失败/被测行为不符 | 计入失败统计 |
| 环境不可用 | `envUnavailable` | 数据根缺失/必备进程不可启动/gtest 自身错误（§6.5） | 不计失败；CI 必须显式标注并阻断"全绿"误判 |
| 数据集非法 | `datasetInvalid` | manifest/档案/完整性校验失败、容差未定义、单位不匹配 | 不计失败；属数据资产缺陷，指向数据集修复 |
| 测试未执行 | `skipped`（显式跳过，含理由）／`notRun`（未启动，如前置失败级联） | — | 不计通过；发布检查表必须逐条消账 |

### 7.3 与 gtest 的关系与追溯登记

- 断言＝gtest 宏（唯一框架，D-01）；IRD 宏（`gtest/AssertMacros.hpp`）是其薄包装＋结构化详情旁路；
- `TestRecordListener : public ::testing::EmptyTestEventListener`——header-only 适配头（`gtest/RecordListener.hpp`），在消费方 TU 编译；提供 `installTestRecordListener(int& argc, char** argv)`（消费方 main 或 gtest main 替换处调用一次）；testkit 库本体因此保持零 gtest（§3.3）；
- 追溯登记：`IRD_TEST_INFO("KIN-02", {"AT-03"}, datasetRef)`——在测试内首行声明，写入当前 TestRecord；不修改 gtest 注册机制、不发明参数化引擎；
- **独立性**：报告生成不依赖 reporting 单元（ReviewReport 是产品对象，测试报告不是；两者数据契约无共享，§2.2 第 8 条）。

### 7.4 与产品报告的边界复述

`ird-test-report.json` 仅测试消费（CI/开发者）；不进入安装包（§3.6）；字段命名与 ReviewReport 体系无耦合。

### 7.5 CI 消费方式

配置（`-DSDURWS_IRD_TESTDATA_DIR=...` 可覆写）→ 构建 → `ctest -L ird`（标签约定：`ird`、`ird_gui`）→ 归档 gtest XML＋ird 报告；聚合脚本按 §7.2 四类分列统计，`envUnavailable`/`datasetInvalid`/`notRun` 非零时 CI 以"不可判定"状态收场（不得绿灯）。

---

## 8. testkit 自身验证方案

自测目标 `sdurws_ird_testkit_test`（gtest；本表均为**设计用例**，未实现未运行；实现状态随 §9 登记）。方法要点：断言设施的"元测试"＝用期望失败的断言校验其失败详情（gtest `EXPECT_NONFATAL_FAILURE` 族/has_failure 检查）。

| 组 | 用例（含正/反例） | 针对的风险 | 追踪 |
| --- | --- | --- | --- |
| TK-JSON | 合法往返（dump(parse(x))==x，键序稳定）；重复键/NaN 字面/尾随内容/BOM 拒绝并给行列 | 解析器缺陷污染全部下游 | §4.1 |
| TK-MAN | manifest 合法装载（附录 A.2 全字段）；反例：schemaVersion 未知/字段越界/单位未注册/`analytic-case` 缺 edgeCases/integrity 缺文件/需求 ID 不在字典 | 清单校验走样 | §4.2 |
| TK-INT | 篡改一字节的输入文件→datasetInvalid；size 不符同理 | 完整性校验失效 | §4.5 |
| TK-TOL | 档案解析；`tolerance>allowedMax` 拒绝；fieldPath 未命中→ToleranceUndefined；逐元素 `*` 展开；三类 source 均可装载且 lint 与 core `runtimeAbsoluteTolerance` 对照（C7 一致性） | 容差档案成为第二权威/静默默认 | §4.3 |
| TK-CMP | close/atMost/identical 正反例；NaN/Inf→failed(non-finite)；长度不匹配→缺失/额外索引；双空通过；浮点用 IDENTICAL 被类型规则拒绝 | 断言语义走样 | §5.3 |
| TK-SET | **排序误判反例**：A={1.0,2.0}，B={2.0,1.0}，容差 1.5——排序逐项会误判不等价，本断言判等价（传递性缺失场景）；**重复匹配反例**：两实际元素同时接近一参考元素且另一参考无伴→判失败或正确配对，绝不允许一参考配两实际；expected 身份重复→DatasetInvalid；ambiguousMatch 报告；10 万元素性能护栏触发 | 集合断言重复匹配/顺序误判（自审项 A-6） | §5.4 |
| TK-ORD | 顺序断言：首错位索引；集合等价但顺序不同的样本判"集合通过＋顺序失败" | 两断言混淆 | §5.4.3 |
| TK-CTR | DiagnosticRecord/TaskIdentity/envelope 谓词正反例（构造 core 值类型） | 契约断言字段遗漏 | §5.5 |
| TK-FIX | TempDir 建立即存在、析构即消失、keepOnFailure 语义；DeterministicEnv 记录往返；并行目录隔离（多线程建目录） | 资源泄漏/串扰 | §6.2、§6.3 |
| TK-FAULT | FaultInterceptor 按 occurrence 触发（第 2 次写失败）；FaultLog 命中序列 | 注入原语失准 | §6.4 |
| TK-RPT | 六类 outcome 分类；skipped/notRun 聚合不计通过；报告 JSON 可被独立解析回读 | 结果误报 | §7.2 |
| TK-BUILD | testkit 目标链接清单仅 core＋std（无产品单元/无 Qt/无 gtest 库级链接）；T-1 门禁用例（产品目标零 testkit 边） | 依赖红线 | §2.4、§3.3 |

---

## 9. 阶段 A 实现任务拆分

局部编号 `TK-Txx`（testkit＝**WP-02**，REQUIREMENTS §3/ARCH §3.1 既有登记；不重排任何上游 WP 编号；DETAILED-DESIGN/development-task-breakdown 产出后如需对齐，以增量修订处理，P-TK-3）。依赖顺序自上而下。

| 任务 | 输入 | 产物 | 依赖 | 涉及文件 | 验证方式 | 完成条件 |
| --- | --- | --- | --- | --- | --- | --- |
| TK-T01 构建落位与框架接入 | 骨架 CMakeLists、§3.3/§3.4、development-task-breakdown §5.5 定稿 | `sdurws_ird_testkit` 升级 STATIC（C++17、链 `RWS::ird::core`）；`sdurws_ird_testkit_test` 注册（gtest 按 §5.5：vcpkg 安装＋`find_package(GTest CONFIG REQUIRED)`，安装与版本登记随本任务；find_package 失败→本任务转 blocked）；`testdata/` 根＋`SDURWS_IRD_TESTDATA_DIR` 变量＋`TestPaths.hpp` | 无（与 CORE-T01 平行可做） | `industrialrobot/CMakeLists.txt`、`testkit/CMakeLists.txt`（新）、`testkit/include/.../TestPaths.hpp`、`testkit/test/*` | 独立冒烟＋集成构建配置成功；TK-BUILD 前两个用例 | 两模式构建零错误；数据根解析（env/默认/缺失三态）用例通过；gtest 版本登记于 breakdown §4.4 定稿行 |
| TK-T02 受限 JSON | §4.1 | `JsonLite.hpp/.cpp` | TK-T01 | 同名文件 | TK-JSON | 往返/拒绝用例全过；行列错误消息可定位 |
| TK-T03 清单模型与完整性 | §4.2、§4.5、TK-T02 | `Dataset.hpp/.cpp`；`tools/testdata` 校验工具（lint＋全量扫描）；`testdata/requirements-ids.json` 种子（v1.16 ID 清单） | TK-T02 | 同名文件＋`tools/testdata/*` | TK-MAN、TK-INT | 附录 A.2 示例数据集装载通过；全部反例拒绝且消息含字段路径 |
| TK-T04 容差档案 | §4.3 | `ToleranceProfile.hpp/.cpp`；示例档案 `testdata/tolerance/kin-fk/`（附录 D 第 4/9 项条目） | TK-T02（TK-T03 联合 lint） | 同名文件 | TK-TOL | resolve 命中/未命中/超限三态用例通过；C7 对照用例通过 |
| TK-T05 数值断言与比较详情 | §5.3 | `Check.hpp`（＋宏）、失败详情输出 | TK-T04 | 同名文件 | TK-CMP | 全部反例（NaN/长度/空/类型规则）按 §4.3.3 表落位 |
| TK-T06 集合与顺序断言 | §5.4 | `SetCheck.hpp`（Traits＋两阶段匹配＋差异定位） | TK-T05 | 同名文件 | TK-SET、TK-ORD | 排序误判/重复匹配反例通过（元测试）；护栏用例通过 |
| TK-T07 契约断言 | §5.5 | `ContractCheck.hpp` | TK-T05（core 类型可用性随 core 冻结状态，P-TK-2） | 同名文件 | TK-CTR | 正反例通过；evidence 谓词模板编译（以 core 值类型演练） |
| TK-T08 夹具与确定性 | §6.1~§6.3 | `Fixture.hpp/.cpp`（TempDir/ReproRecord/DeterministicEnv/GoldenFixture） | TK-T03/T04 | 同名文件 | TK-FIX | 生命周期六步用例；keepOnFailure 现场保留 |
| TK-T09 故障注入原语 | §6.4 | `Fault.hpp/.cpp`；`ProcessRunner.hpp`（仅头文件设计冻结，不实现） | TK-T01 | 同名文件 | TK-FAULT | occurrence 触发/命中记录用例通过；无产品目标链接 testkit（TK-BUILD 复验） |
| TK-T10 报告、示例与门禁同步 | §7、§3.6、core.md §8 | `Report.hpp/.cpp`（数据类型＋写出）；`gtest/RecordListener.hpp` 适配头；**core 接入示例**（附录 A.1 作为 `sdurws_ird_core_test` 的样板用例建议，随 core CORE-T01~T05 落地）；安装排除扫描脚本建议（交 WP-01/WP-24）；README 更新 | TK-T03~T09 | 同名文件、`testkit/include/.../README.md`、本文 | TK-RPT、TK-BUILD | 六类 outcome 聚合正确；示例数据集随 CI 跑通；门禁建议登记给 WP-01 |

每任务完成条件均含"测试通过并留痕"；任何未执行测试不得标注通过。阶段 A **不含**：TestProcessRunner 实现、`_qt` 目标、任何业务域数据集与契约套件（§10 登记）。

---

## 10. 后续阶段扩展及单元交接清单

### 10.1 阶段扩展登记（不建空目标、不提前承诺）

| 能力 | 启用阶段/触发 | 承接 |
| --- | --- | --- |
| TestProcessRunner 实现（Job Object/事件等待） | project/execution 契约测试需要进程级崩溃/恢复场景时（AT-11/13 自动化载体；阶段 A 末期或 B 初） | §6.5 设计已冻结；实现任务届时补登 TK-T11 |
| `sdurws_ird_testkit_qt`（Qt 测试辅助） | ui/workflow 出现 QCoreApplication/GUI 测试目标时 | §3.5、§6.7 |
| 性能基准夹具（计时统计、吞吐对照口径） | NFR-PERF-01~06 所属阶段（C/D） | performance-baseline 数据集类别已预留（§4.2.1）；夹具细则届时由 WP-23 消费者提出 |
| 各业务域黄金数据集与契约套件 | 阶段 B~E 各域 | §10.2；数据集责任归域＋WP-02 |

### 10.2 单元交接清单（各域须提供/将消费的 testkit 能力）

| 单元 | 从 testkit 接收 | 须自行提供（责任） | 典型 AT 载体 |
| --- | --- | --- | --- |
| core | 断言/档案/夹具/报告（首个接入示例，TK-T10） | `sdurws_ird_core_test` 目标注册；core.md §8 用例体 | 附录 D C4/C7 锁定（UT-TOL 同源） |
| project | FaultInterceptor（经 io 写入口）、TempDir、（按需）TestProcessRunner | 保存事务故障场景编写（io 写器 fake 实现）；AT-13 场景逻辑 | AT-11/13 |
| evidence | ContractCheck 谓词、数据集清单 | envelope 访问器特化；切片序列化契约夹具数据集 | §8.1 表 3 |
| runtime | 集合/数值断言、契约夹具 | FK/编译链等价数据集（analytic，附录 D 第 4 项容差）；名称往返测试 | AT-16/18/37 |
| policy | 断言、档案（engineering-policy-default 类条目） | 策略一致性三入口场景；碰撞判定数据集 | AT-19 |
| execution | DeterministicEnv、（按需）TestProcessRunner/EventWatch | 状态机/取消/迟到回调场景逻辑（不搬入 testkit） | TASK-01~03、AT-10 |
| diagnostics | ContractCheck（DiagnosticRecord） | StableCodeRegistry 契约夹具 | ERR-01 |
| io | FaultInterceptor 模板、JsonLite（不用于产品格式） | 写入口 fake；CSV 方言 roundtrip 用例 | AT-02/20 |
| modeling~optimization（各域） | 全部断言/数据集 schema/夹具 | 各域黄金数据集＋生成脚本＋`SetMatchTraits` 适配器＋契约套件目标注册 | AT-01/03/06~09/16/22/30/34~38 |
| WP-24（打包） | §3.6 排除清单 | 安装树扫描门禁实施 | NFR-SEC-05 |

---

## 11. 需求—设计—验证追踪矩阵

| 需求/上游条款 | testkit 设计落点 | 验证（§8 组） |
| --- | --- | --- |
| NFR-COR-01（解析算例/独立参考对照＋默认容差） | §4.2 数据集五类与升级规则、§4.3 档案、§5.3 断言 | TK-MAN、TK-TOL、TK-CMP |
| NFR-COR-02（等价集合＋稳定排序；并行归约容差） | §5.4 集合/顺序断言、§6.3 ReproRecord、§7.1 | TK-SET、TK-ORD、TK-FIX |
| NFR-COR-03（非有限/非法单位/缺失不得静默通过） | §4.3.3 失败行为表、§5.3.2/5.3.3 | TK-CMP、TK-TOL |
| 附录 D C4（通用公式/逐元素/零参考/导出量声明/边值样例） | §4.3.1（导出量 absolute 必填）、§4.2.2 edgeCases、§5.3 | TK-TOL、TK-MAN |
| 附录 D C7（测试/产品容差适用范围分离） | §4.3.2 三类角色、§4.3.3 禁区、lint 对照 `runtimeAbsoluteTolerance` | TK-TOL |
| 附录 D 第 3 项（IK 去重＝业务口径） | §5.4.5 适配器归域（kinematics 提供） | TK-SET（机制） |
| 附录 D 第 4/5/8/9 项（测试对照容差） | §4.3.2 固定类条目＋allowedMax 单向收紧 | TK-TOL |
| 附录 D 第 11 项（工程策略默认 4π） | §4.3.2 经被测 policy 对象取值 | TK-TOL |
| 附录 D 第 12 项（身份精确等值） | §5.3.4 类型规则、§5.4.2 阶段 1 | TK-CMP、TK-SET |
| ERR-01/UX-03（诊断字段/比较型三要素/不适用标记） | §5.5 checkDiagnosticRecord 族 | TK-CTR |
| §8.1 表 3（ResultEnvelope 合法组合） | §5.5 checkEnvelopeCombination 谓词 | TK-CTR |
| TASK-03（五元组身份） | §5.5 checkTaskIdentity | TK-CTR |
| EVI-01 表 1（评估模式） | 不承接（词表归 core；测试经 core token 断言） | — |
| NFR-REL-01/02、PM-08（事务/崩溃恢复测试支撑） | §6.4 故障注入、§6.5 进程支撑（按需） | TK-FAULT |
| NFR-MNT-01/02（零 Qt/模型测试直调） | §3.5 隔离、§6.7 规则 | TK-BUILD |
| NFR-SEC-05/NFR-DEP（分发边界） | §2.4 T-1、§3.6 排除与扫描 | TK-BUILD＋CI 扫描 |
| AT-01/03/06~11/13/16/18/19/22/30/34~38（载体支撑） | §10.2 交接表 | 各域套件（本文不代验） |
| ARCH §3.1 testkit 行 / §11.2 架构验证清单 | 全文职责边界；§10.2 供各支柱契约测试消费 | — |

---

## 12. 设计决策、风险与待裁决项

### 12.1 设计决策登记（本文作出并说明理由的普通实现选择）

| ID | 决策 | 理由与备选 |
| --- | --- | --- |
| D-01 | 沿用捆绑 googletest，不引入第二套测试框架 | 仓库既有 `ADD_RW_GTEST`/`ADD_RWS_GTEST` 宏、`sdurw-gtest-main`、XML 报告与 `RW::gtest` 导出目标（实测）；任务约束"不擅自增加第二套"；备选（doctest/catch2）无必要性且违反约束 |
| D-02 | testkit 自带受限 JSON 解析器（JsonLite） | 无可用共享 JSON 库（实测 vcpkg installed）；依赖 io 违反 T-2 且 io 未产出；边界声明防"第二持久化实现"误判（§4.1）；备选：等 development-task-breakdown 统一后替换（登记 P-TK-6 附注） |
| D-03 | manifest/档案/报告为**测试侧**数据格式，独立 schemaVersion（`ird-golden-manifest/1` 等） | 与产品 `.rwdesign` 格式（project/schemaVersion，NFR-DEP-04）零耦合；测试格式演进不触发产品升级器义务 |
| D-04 | 集合等价＝两阶段（身份精确＋数值完美匹配），拒绝排序逐项与贪心最近邻 | 容差相近无传递性（§5.4.1）；完美匹配是一一配对语义的准确表达；规模护栏防性能基准滥用 |
| D-05 | 黄金数据根 `industrialrobot/testdata`＋env 覆写 | 源内默认开箱即用（CI 简单）；env 覆写支持外置大数据集；借鉴 RWS `testfiles/` 先例但独立目录避免基线耦合 |
| D-06 | IRD 宏＝gtest 宏薄包装＋结构化详情旁路；testkit 库不链 gtest | 保持单一框架（D-01）；库/宏分离使 testkit 可被非 gtest 工具（lint 工具）复用装载与校验 |
| D-07 | 测试报告双轨：gtest XML（既有）＋ird-test-report.json（listener） | CI 与开发者既有工具链不破坏；追溯字段（需求/AT/数据集/容差/种子）gtest XML 不承载 |
| D-08 | 数据集缺失容差/单位不匹配＝数据集非法级错误，非断言失败 | 数据资产缺陷与算法回归必须分流（§7.2 四类）；否则数据损坏会伪装成代码失败或反之 |
| D-09 | regression 数据集永不自动升级为独立依据 | 任务约束§四.1 明文；升级＝换源新建＋重新审核留痕 |
| D-10 | 故障接缝＝生产自有窄接口＋测试目标内 fake，生产代码零 testkit 头、零 `#ifdef TEST` | 依赖红线 T-1 的唯一安全形态；接缝本就是架构端口（ARCH §7.2） |
| D-11 | 默认种子固定（20260909）、默认线程 1 | 无随机性测试也要可复现；多线程显式声明（附录 D 第 8 项归约容差场景） |
| D-12 | 五类数据集 token/字段/路径命名全 ASCII 小写连字符 | 与 core D-12 同纪律（跨平台/编码安全） |

### 12.2 风险

| # | 风险 | 影响 | 缓解 |
| --- | --- | --- | --- |
| R-1 | core.md v0.1 未冻结，其契约（Tolerance/UnitToken/DiagData 签名）可能变化 | TK-T04~T07 返工 | §3.2 消费清单逐项锚定 core.md 章节；core 冻结后按 diff 增量同步（P-TK-2） |
| R-2 | 已消除（2026-09-10，CR-07）：上游两文档已产出，gtest 机制定稿（development-task-breakdown v0.2 §5.5） | （已消除）原 TK-T01 方案悬空 | 按 §5.5 执行；§3.2 失实记载已更正；安装+版本登记随 TK-T01 | P-TK-1/P-TK-3 |
| R-3 | 黄金数据集体积增长（点云/轨迹快照）导致仓库膨胀 | clone/CI 变慢 | 版本目录保留策略（§4.7）；LFS/制品通道裁决（P-TK-5） |
| R-4 | requirements-ids lint 字典随需求修订漂移 | 数据集 lint 误报/漏报 | 字典文件随 REQUIREMENTS 修订同步更新（任务流程登记于 TK-T03 完成条件） |
| R-5 | 集合断言被误用于大规模性能数据 | 测试超时 | 10 万元素护栏＋文档明示按索引/摘要比较（§5.4.2） |
| R-6 | 故障 fake 实现散落各域、口径不一 | 注入语义漂移 | faultPointId 命名规约＋§10.2 接缝最小集登记；机制单点在 Fault.hpp |
| R-7 | ARCHITECTURE 评审（A9+）调整单元/端口划分 | §6.4 接缝清单、§10.2 交接表返工 | 影响面集中两节；按增量修订同步 |

### 12.3 待裁决项（问题—依据—影响—建议—需要裁决者）

| # | 问题 | 依据 | 影响 | 建议 | 需要裁决者 |
| --- | --- | --- | --- | --- | --- |
| P-TK-1 | **已关闭（2026-09-10，CR-07）**：gtest 接入机制定稿——vcpkg＋`find_package(GTest CONFIG REQUIRED)` 唯一机制，失败即停不回落 | development-task-breakdown v0.2 §5.5（O-05 实测：USE_gtest=OFF、gtestTargets.cmake 从未生成）；core.md P-ENV-2 同源关闭 | （已消除）原 TK-T01 落地方式悬空 | 按 §5.5 执行；安装＋版本登记随 TK-T01；find_package 失败→TK-T01 转 blocked | 构建约定所有者（已定稿） |
| P-TK-2 | testkit 消费的 core 契约以 core.md v0.1（Draft）为基线，其冻结版可能调整签名/字段 | core.md 文档头状态行 | TK-T04~T07 与档案/断言形状返工 | core 冻结时出 diff 清单，testkit 按影响面增量修订并留痕；本文不私改 core 语义 | core 详设所有者 |
| P-TK-3 | 本文局部任务编号 TK-Txx 与未来 development-task-breakdown 的 WP 分配对齐方式 | 该文件缺失（ARCH §11.1"后续产出"） | 任务追踪双轨 | breakdown 产出后以映射表增量登记，不重排本文编号 | 任务分解所有者 |
| P-TK-4 | ARCHITECTURE §3.5 单元依赖表未列 testkit（其为测试单元）；建议补登记"testkit→core（仅测试目标可用）"有向边及 T-1/T-2 红线 | ARCH v0.11 §3.5 表；本文 §2.4 | 依赖门禁数据源不含测试侧红线时，T-1 仅靠本文执行 | 架构下次修订在 §3.5 或 §3.2 附注补登；本文先行自律（TK-BUILD） | 架构所有者 |
| P-TK-5 | 大体绩黄金数据（点云/大规模采样快照）的存储与 CI 获取策略（git 内 vs LFS vs 制品） | 仓库现状无 LFS 配置；CI 通道未定（gitlab-ci 仅有基线框架配置） | 数据集交付与 CI 时效 | 阶段 A 数据小（解析算例为主）源内即可；首个大数据集出现时裁决 | 构建负责人（WP-01）＋WP-02 |
| P-TK-6 | JsonLite 与未来统一测试侧 JSON 机制的归并 | D-02 边界声明 | 可能重复实现 | breakdown 若统一机制，JsonLite 退役换源，manifest schema 不变 | 构建约定所有者 |
| P-TK-7 | `requirements-ids.json` lint 字典的维护责任（随 REQUIREMENTS 修订同步） | §12.2 R-4 | lint 有效性 | 建议随需求变更流程一并更新该字典（WP-02 执行、需求所有者复核） | 需求所有者＋WP-02 |

---

## 13. 变更记录

| 版本 | 日期 | 说明 |
| --- | --- | --- |
| v0.1 | 2026-09-09 | 首版：基于 REQUIREMENTS v1.16（Accepted）与 ARCHITECTURE v0.11（Draft）、core.md v0.1（Draft，协作输入）完成 13 章详细设计；登记磁盘现状（DETAILED-DESIGN/development-task-breakdown/其余 units 卡缺失、old/ 缺失、gtest 框架实测）；冻结数据集清单/容差档案 schema、两阶段集合匹配语义、六类测试结果分类；实现任务 TK-T01～T10；待裁决 7 项（P-TK-1～7） |
| v0.2 | 2026-09-10 | FOUNDATION-CR-01 契约冻结审查修正（CR-06/CR-07）：§3.2 gtest 接入记载更正——`RW::gtest`"实测存在"系失实（CR-07 复核：gtestTargets.cmake 从未生成、USE_gtest=OFF），唯一定稿机制＝vcpkg＋`find_package(GTest CONFIG REQUIRED)`（development-task-breakdown §5.5），P-TK-1 关闭、R-2 消除；TK-T01 行更新（安装/版本登记随本任务、失败转 blocked）。CR-06 设计级 API diff 通过（core Compare ↔ Check 函数族），TK-T04/05 实现级门禁维持。差异记录见 traceability/foundation-api-diff.md |
| v0.3 | 2026-09-10 | TK-T01（≙WP-02-T01）实施偏差登记（DTB §5.4，实现细节级，接口语义与任务范围零修改）：①§5 错误类型 `TestKitError` 前置定义于 `TestPaths.hpp`——原设计随 Dataset.hpp（TK-T03）承载，但 TestPaths（TK-T01）的 env-unavailable 错误先需要该类型；Dataset.hpp 落地时包含 TestPaths.hpp 而非重复定义，五枚举语义与 §5 原文一致（非冻结变更）；②增设 `detail::resolveDataRoot` 纯函数接缝——§4.6 公共 API 的内部分解，服务三态可测性（编译默认的存在性无法在运行期改写，公共 API 层只能覆盖两态），不构成稳定契约；③`testdata/` 实体目录不随 TK-T01 建立（契约 allowedFiles 不含 testdata/**）：`SDURWS_IRD_TESTDATA_DIR` 变量与解析逻辑先落位，实体目录随 TK-T03 首个数据集建立，"默认缺失"态由纯函数层用例钉住（公共 API 用例按默认存在性双分支自适配，两分支均强断言） |
| v0.4 | 2026-09-11 | TK-T03（≙WP-02-T03）落位登记：①§4.2.2 manifest 装载（字段表逐行）＋§4.5 完整性（size＋SHA-256，core ContentDigester 复用）＋§5.1 GoldenDataset 按原文契约实现，接口零偏差；②落位偏差（TK-T03 范围）：§5.1 toleranceProfile() 完整装载归 TK-T04——本任务提供 toleranceProfileRef()（manifest 内 {id,version} 引用）；③lint 工具 sdurws_ird_testdata_lint（tools/testdata）＋requirements-ids.json 种子（192 ID＝v1.16 行首全集）；④TK-MAN 拒绝矩阵 datasetId 变体同步目录名——装载器校验顺序（目录一致先于深层校验）在测试设计中对齐 |
| v0.5 | 2026-09-11 | TK-T04（≙WP-02-T04）落位登记：①§4.3.1/§5.2 ToleranceProfile/ToleranceEntry/ToleranceSource 按原文契约实现（load 全量校验：schema 主版本/profileId 目录一致/文件名 version 一致/allowedMax 条件规则/quantityKind 与 unit 量纲一致性），接口零偏差；②示例档案 testdata/tolerance/kin-fk/v1.0.0.json（附录 D 4/9 项条目——fixed 与 dataset-declared 两类 source 示范）；③实现取舍：档案目录布局＝tolerance/<profileId>/v<version>.json（无版本子目录层）；resolve 模板匹配＝段数相同＋* 段前缀[索引]形态；CR-06 已由 DOC-T05 关闭（实现即 diff 通过后的合法消费） |

---

## 附录 A：完整示例（均为**设计示例**，非已执行的验证结果）

### A.1 core 单位/数值基础测试（testkit 接入示例；目标 `sdurws_ird_core_test`，随 core CORE-T04/T05 与 TK-T10 落地）

```cpp
// core/test/UnitsToleranceTest.cpp（示例骨架）
#include <gtest/gtest.h>
#include <sdurws/ird/core/Units.hpp>
#include <sdurws/ird/core/Compare.hpp>
#include <sdurws/ird/testkit/Check.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

TEST(UnitsTolerance, MmToDegRoundtrip_AppendixD_Item9) {
    IRD_TEST_INFO("KIN-12", /*at*/ {}, /*dataset*/ std::nullopt);   // 追溯登记（无数据集时省略）

    using sdurws::ird::core;
    const auto mm = core::UnitToken::find("mm").value();
    const auto m  = core::UnitToken::find("m").value();
    const double x = 1234.0;                       // mm
    const double y = core::convert(x, mm, m);      // 1.234 m（唯一换算入口）
    const double z = core::convert(y, m, mm);      // 往返
    // 容差＝附录 D 第 9 项（标量相对 1e-9；测试对照容差）＋ε_abs 由档案条目给出
    const auto t = core::Tolerance::make(1e-9, 1e-12);
    auto r = sdurws::ird::testkit::checkCloseWithin("units.roundtrip.mm", z, x, t, "appendixD#9");
    EXPECT_TRUE(r.passed);                          // 失败时 IRD 宏会逐点输出 actual/expected/容差/差值

    // 非有限反例：换算入口拒绝 NaN（NFR-COR-03）
    EXPECT_FALSE(core::tryConvert(std::numeric_limits<double>::quiet_NaN(), mm, m).has_value());
}
```

### A.2 解析算例数据集清单（`testdata/golden/kin-fk-planar-2r/manifest.json`，完整示例）

```json
{
  "schemaVersion": "ird-golden-manifest/1",
  "datasetId": "kin-fk-planar-2r",
  "version": "1.0.0",
  "kind": "analytic-case",
  "scenarioCategory": "kin/fk-forward",
  "coveredRequirements": ["KIN-01", "ARC-03"],
  "coveredAt": ["AT-16"],
  "toleranceProfile": { "id": "kin-fk", "version": "1.0.0" },
  "inputs":  ["inputs/planar-2r.json"],
  "expected": ["expected/fk-tables.csv", "expected/edge.json"],
  "parameters": { "link1": 0.5, "link2": 0.4, "q2-samples": 9 },
  "units":  { "position": "m", "angle": "rad" },
  "frames": { "base": "link0", "tcp": "flange" },
  "referenceSource": {
    "method": "closed-form",
    "scope": "平面 2R 正运动学，闭式三角解；适用于本算例全部样本",
    "independentOfProductionImpl": true,
    "description": "推导见 generate/derivation.md §2（余弦定理＋ atan2 分支）"
  },
  "producer": {
    "softwareVersion": "python-3.11+mpmath-1.3",
    "algorithmId": "fk-planar-2r-closed-form",
    "solverConfig": {},
    "seed": null, "threadCount": 1,
    "generatedAtUtc": "2026-09-09T00:00:00Z"
  },
  "edgeCases": {
    "zeroValue": true, "nearZero": true, "signCancellation": true,
    "sampleRefs": ["expected/edge.json#zero-pose", "expected/edge.json#near-zero-q2",
                   "expected/edge.json#sign-cancel-q1"]
  },
  "integrity": [
    { "path": "inputs/planar-2r.json",   "sha256": "<64-hex>", "sizeBytes": 412 },
    { "path": "expected/fk-tables.csv",  "sha256": "<64-hex>", "sizeBytes": 1830 },
    { "path": "expected/edge.json",      "sha256": "<64-hex>", "sizeBytes": 540 },
    { "path": "generate/make_fk.py",     "sha256": "<64-hex>", "sizeBytes": 1610 }
  ],
  "generator": { "script": "generate/make_fk.py", "invocation": "python make_fk.py", "committed": true },
  "history": [
    { "version": "1.0.0", "date": "2026-09-09", "change": "首版",
      "reReviewedBy": "kinematics-owner", "supersededBy": null }
  ]
}
```

容差档案引用（`testdata/tolerance/kin-fk/v1.0.0.json`，节选——第 4 项固定＋第 9 项允许更严声明）：

```json
{
  "schemaVersion": "ird-tolerance-profile/1",
  "profileId": "kin-fk", "version": "1.0.0",
  "basis": "appendixD#4 + #9",
  "entries": [
    { "fieldPath": "fk[*].tcp.position.x", "quantityKind": "length", "unit": "m",
      "tolerance": { "relative": 1e-9, "absolute": 1e-9 },
      "source": "appendixD-fixed", "allowedMax": { "relative": 1e-9, "absolute": 1e-9 } },
    { "fieldPath": "fk[*].tcp.position.y", "quantityKind": "length", "unit": "m",
      "tolerance": { "relative": 1e-9, "absolute": 1e-9 },
      "source": "appendixD-fixed", "allowedMax": { "relative": 1e-9, "absolute": 1e-9 } },
    { "fieldPath": "fk[*].timing.elapsed", "quantityKind": "time", "unit": "s",
      "tolerance": { "relative": 1e-9, "absolute": 1e-6 },
      "source": "dataset-declared",
      "allowedMax": { "relative": 1e-6, "absolute": 1e-3 } }
  ]
}
```

说明：前两条为附录 D 第 4 项（FK 等价验证 1×10⁻⁹ m，固定类——`tolerance` 不得大于 `allowedMax`）；第三条展示**导出量（时间）ε_abs 逐例声明**（C4：未声明即报错）与 `dataset-declared` 的上界登记。若第三条缺失而测试比较 `fk[*].timing.elapsed`，`resolve` 抛 `tolerance-undefined`，结果记 `datasetInvalid`。

### A.3 集合差异断言失败报告（ird-test-report.json 单条 TestRecord，示例数据为虚构演示）

```json
{
  "testId": "KinBatchTest.IkSolutionSet_MatchesAnalytic",
  "requirementIds": ["KIN-02"], "atIds": ["AT-03"],
  "dataset": { "id": "kin-ik-6r-poses", "version": "1.1.0" },
  "toleranceProfile": "kin-ik@1.0.0",
  "repro": { "seed": 20260909, "threadCount": 1, "softwareVersion": "0.1.0-dev",
             "gitCommit": "db31b6a", "dataset": { "id": "kin-ik-6r-poses", "version": "1.1.0" },
             "toleranceProfile": "kin-ik@1.0.0", "notes": "" },
  "environment": { "compiler": "MSVC 19.40", "arch": "x64", "os": "Windows 10.0.26200", "machineTag": "ci-win-07" },
  "outcome": "failed",
  "reason": "set-equivalence: 1 missing, 1 extra, 1 mismatched",
  "comparisons": [
    { "fieldPath": "ik[2].joints[*]", "elementIndex": 2, "hasElement": true,
      "actual": 0.44880, "expected": 0.44880, "diff": 1.9e-06, "unit": "rad",
      "tolerance": { "relative": 0.0, "absolute": 1e-06 },
      "toleranceSource": "appendixD-fixed#3", "reason": "exceeds-tolerance" }
  ],
  "failureLocation": { "path": "ik[2].joints[4]", "elementIndex": 4, "setRole": "mismatched" },
  "artifacts": [ { "path": "<tmp>/ird-test/12345-kinik/actual-solutions.csv", "kind": "tempdir-snapshot" },
                 { "path": "<tmp>/ird-test/12345-kinik/repro.json", "kind": "repro" } ],
  "durationMs": 184
}
```

配套的人类可读 gtest 输出（同一失败，IRD 宏展开产生）：

```
..\kinematics\test\KinBatchTest.cpp(88): error: set-equivalence failed
  missing  : expected[3] (id=ik-ref-0007, nearest actual dist 2.4e-06 rad)
  extra    : actual[5]  (id=ik-act-0011, nearest expected dist 1.9e-06 rad)
  mismatched: expected[2](id=ik-ref-0003) <-> actual[2](id=ik-act-0004)
      at ik[2].joints[4]: |0.448801900 - 0.448800000| = 1.9e-06 rad
      tolerance: abs 1e-06 rad (appendixD-fixed#3) -> exceeds-tolerance
```

（注意：此处身份匹配命中后数值字段仍逐项校验——身份相同不豁免数值一致性，§5.4.2 阶段 1。）

### A.4 固定种子的复现记录（`repro.json`，失败时随报告归档；独立可重放）

```json
{
  "seed": 20260909,
  "threadCount": 8,
  "softwareVersion": "0.1.0-dev",
  "gitCommit": "db31b6a",
  "dataset": { "id": "opt-static-morphology", "version": "2.3.1" },
  "toleranceProfile": "opt@1.2.0",
  "notes": "NFR-COR-02 replay: 与 2026-09-08 基线运行对照；两次运行集合等价+稳定排序一致；逐元素满足附录D#8（相对1e-12+ε_abs）；浮点文件允许非逐字节相同。"
}
```

重放流程（设计描述）：同 commit 构建 → 设 `SDURWS_IRD_TESTDATA_DIR` → 以 repro.json 内容配置被测求解入口（种子/线程经其显式参数传入）→ 运行对应测试 → `IRD_EXPECT_SET_EQ`＋`IRD_EXPECT_STABLE_ORDER`＋逐元素 CLOSE 断言全部通过即复现成立（AT-09 同种子确定性口径）。

### A.5 故障注入测试流程（建立→触发→验证→清理；AT-13 类保存事务场景的 testkit 切面示例，设计示例）

```cpp
// project 契约测试目标内（sdurws_ird_project_contract_test）——场景逻辑归 project，原语归 testkit
TEST(SaveTransaction, ChunkWriteFailure_OldRevisionIntact /*AT-13 场景之一*/) {
    IRD_TEST_INFO("NFR-REL-01", {"AT-13"}, DatasetRef{} /*无黄金数据*/);
    sdurws::ird::testkit::TempDir tmp("at13");                 // ① 建立：独立现场
    sdurws::ird::testkit::DeterministicEnv env({ .seed = 20260909, .threadCount = 1, ... });

    // ② 触发准备：fake 写入器实现 io 的文件写入接口（生产接口，测试侧实现）
    auto real  = io::makeFileWriter(tmp.path());
    sdurws::ird::testkit::FaultPlan plan{
        { {"io/file-writer/write-chunk", /*occurrence=*/2} } }; // 第 2 次写块失败（模拟写中途故障）
    sdurws::ird::testkit::FaultInterceptor<io::IFileWriter> fw(real, plan);

    auto store = project::openStore(tmp.path(), /*writerPort=*/fw.impl());  // 经端口注入（无 #ifdef TEST）
    auto rev1  = commitRevision(store, /*objects=*/{"a","b"});              // 先落一个健康修订
    bool saveFailed = false;
    try { commitRevision(store, /*objects=*/{"a","b","c"}); }               // 期望在写入中途失败
    catch (const project::StoreError&) { saveFailed = true; }

    // ③ 验证：失败发生＋旧修订字节不变＋启动忽略未提交版本并给恢复诊断
    EXPECT_TRUE(saveFailed);
    EXPECT_EQ(fw.log().hits, (std::vector<std::pair<std::string,std::uint64_t>>{{"io/file-writer/write-chunk",2}}));
    IRD_EXPECT_IDENTICAL("revisions[0].head", headHash(store), rev1.hash);  // 旧版本完整
    auto boot = project::reopenWithRecovery(tmp.path());
    IRD_EXPECT_IDENTICAL("recovery.ignoredVersions", boot.ignoredUncommitted, std::string{"v2"});
    //（恢复诊断字段用 ContractCheck::checkDiagnosticRecord 校验 ERR-01 完整性）

    // ④ 清理：TempDir 析构（失败保留现场于报告 artifacts；通过则删除）
}
```

流程要点：建立（TempDir/Env/端口注入 fake）→ 触发（FaultPlan occurrence）→ 验证（命中日志＋业务不变量＋诊断契约）→ 清理（RAII）。崩溃类场景（kill 进程）不走本原语，走 §6.5 TestProcessRunner（按需交付）。

---

## 附录 B：交付前自审记录（v0.1；自审≠实现测试≠正式验收）

| 检查项 | 结论 | 证据位置 |
| --- | --- | --- |
| 是否重复实现 core 能力 | ✔ 未重复：比较公式/单位换算/摘要全经 core（§3.2 消费清单）；JsonLite 边界声明（D-02） | §3.2、§4.1、§4.3.3 |
| 产品目标依赖 testkit | ✔ 禁止（T-1 红线＋TK-BUILD＋安装扫描）；testkit 不依赖产品单元（T-2） | §2.4、§3.6、§8 |
| 生产结果当独立黄金真值 | ✔ regression 类永不自动升级（D-09、§4.2.1） | §4.2.1 |
| 放宽/重定义上游容差 | ✔ 固定项 allowedMax 单向收紧（附录 D 第 9 项允许更严）；缺省即报错；无任何放宽通道 | §4.3.1/§4.3.2 |
| 跳过/未运行计为通过 | ✔ 六类 outcome 分列；skipped/notRun 不入通过统计；CI 不可判定状态 | §7.2、§7.5 |
| 集合断言重复匹配/顺序误判 | ✔ 一一配对（完美匹配）＋排序误判元测试反例＋身份重复拒绝 | §5.4、TK-SET |
| 业务算法/事务/调度塞入 testkit | ✔ §2.2 十一项排除；适配器归域（§5.4.5）；进程/场景逻辑归消费者（§6.5） | §2.2 |
| 阶段 A 可独立落地 | ✔ TK-T01~T10 依赖闭包完整（外部项 P-TK-1/P-TK-2 已给默认＋回落） | §9 |
| 后续单元责任明确 | ✔ §10.2 逐单元交接（数据集/适配器/接缝/套件/AT 载体） | §10 |
| 未参与开发者可直接实现 | ✔ 每数据类型字段表＋每接口签名/前置/错误＋五个完整示例 | §4、§5、附录 A |
| 上游冲突集中登记 | ✔ P-TK-1~7（问题—依据—影响—建议—裁决者） | §12.3 |

## AI 执行就绪补充

本单元进入 AI 实现前必须满足：

- 本文中的职责、非职责、公共接口、数据模型、错误语义和不变量不得与 ARCHITECTURE.md 冲突。
- 单元任务使用 $(testkit.ToUpper())-Txx 编号，并通过 doc/industrial-robot-design/tasks/*.json 声明前置任务、允许修改文件和验证命令。
- 跨单元协作只能使用本文列出的公共接口或架构端口；发现接口缺失、需求冲突或依赖未登记时，任务状态必须标记为 locked。
- 实现完成必须通过单元测试、依赖门禁和追踪矩阵校验，不能只以代码编译成功作为完成条件。

### 单元任务退出条件

| 条件 | 要求 |
|---|---|
| 设计 | 本文接口、状态、不变量和错误语义已冻结或明确登记开放问题 |
| 实现 | 只修改任务契约允许的文件 |
| 测试 | 单元测试覆盖本文列出的正例、反例和不变量 |
| 追踪 | 每个任务至少关联一个需求和一个验证目标 |
| 门禁 | alidate-docs.ps1、alidate-task.ps1 和单元验证命令通过 |
