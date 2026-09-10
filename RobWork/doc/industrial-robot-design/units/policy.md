# 工业机械臂设计软件 · policy 单元详细设计（阶段 A）

> 2026-09-10 同步：本单元详设编写完成（第一批 11/20）；保留本文 Draft/Draft-Structured 评审状态，不代表接口已冻结或实现通过。当前任务与准入结论见 [阶段一同步记录](../traceability/phase-one-readiness.md)；历史磁盘调查仅表示编写时事实，现状以该记录为准。

| 字段 | 值 |
| --- | --- |
| 文档版本 | v0.1（首版草案） |
| 日期 | 2026-09-10 |
| 状态 | **`Draft-Structured`**（本文只做详细设计；不自行宣布 Accepted，不视任何自审为实现测试或正式验收） |
| 文档代号 | UNIT-POLICY |
| 单元 | policy（平台内核，L2 计算内核层；ARCHITECTURE §2.3/§3.1：EngineeringPolicySet〔碰撞规则/判定阈值/行程上限阈值〕、碰撞评估唯一实现、RobWork 碰撞适配） |
| 上游 | `REQUIREMENTS.md` **v1.16（`Accepted`，2026-09-09 签署；签署后变更 C1～C8 已留痕）**；`ARCHITECTURE.md` **v0.11（`Draft`，待评审）** |
| 协作输入 | `units/core.md` **v0.1（`Draft`，未冻结）**、`units/evidence.md` **v0.1（`Draft`，未冻结）**、`units/testkit.md` **v0.1（`Draft`，未冻结）**、`units/project.md` **v0.1（`Draft`，当前正文 §1～§15 已齐备，联合接口复核待执行）**。本文消费的 core/evidence 公共契约以其 v0.1 签名为基线并逐项标注状态（§3.2）；协作输入如有变更，本文按影响面增量同步（P-POL-4） |
| 上游下游链位置 | ARCHITECTURE §11.1：`DETAILED-DESIGN.md`（已建立）→ `units/*.md`（单元任务卡）。本文即 `units/policy.md`，按任务卡深度编写（接口签名、数据类型在本文件冻结） |
| 构建落位 | `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/policy/`（骨架已建：目标 `sdurws_ird_policy`〔INTERFACE 占位〕＋别名 `RWS::ird::policy`＋公共头保留位 `include/sdurws/ird/policy/README.md`；见上级 `industrialrobot/CMakeLists.txt`） |
| 任务归属 | 详见 `development-task-breakdown.md` WP-B～WP-I 映射；本文只维护单元内部任务
| 适用 AGENTS.md | 仓库根 `AGENTS.md` 适用：产品代码详细中文注释、框架零源码修改、双模式构建与留痕、提交后推送。Windows Qt GUI 测试须在 VS x64 环境设 `QT_QPA_PLATFORM=windows`，逐个绝对路径启动。 |
| 实现口径 | 从头构建（REQUIREMENTS v1.9/v1.11、ARCHITECTURE 文档头）；`old/` 仅功能范围对照且**当前磁盘缺失**（core.md §1.2 R-5 同源登记）；历史实现不作为语义来源，不恢复 `old/` 代码 |

---

## 1. 文档信息、上游基线与设计目标

### 1.1 文档定位

本文是 policy 单元的唯一详细设计：依据 `ARCHITECTURE.md` 分配给 policy 的职责（§3.1 单元总表行；§7.5 策略权威支柱五、SA-06；§7.2 ④策略端口；§5.1"RobWork 碰撞接口只经 runtime/policy 适配层接触"），把 `EngineeringPolicySet` 数据模型、策略解析与校验、`CollisionEvaluator` 唯一实现、碰撞作用域与过滤规则、阈值与数值语义、版本与内容身份、兼容判定、与 evidence/runtime/业务单元的交接、公共接口、验证方案与阶段 A 任务拆分写到可直接实现的深度。

需求语义与验收标准一律以 `REQUIREMENTS.md` 条目为自足定义，本文不重定义、不收窄、不扩大；本文引用的需求 ID 与章节号均指 v1.16 当前正文（重点：ARC-05、CON-04/05/06、ERR-01、EVI-01 与 §8.1 表 1～4〔含 C2/C5/C6/C8 修订口径〕、MDL-04/06④/15/22、KIN-02/04/05/09/13、TRJ-04、OPT-03〔OPT-B 静态子集〕、SEL-05、NFR-COR-02/03/05、NFR-MNT-01/03/07、NFR-DEP-05、UX-08、附录 D〔P-01 冻结，第 11/12 项与 C4/C7〕、AT-01/03/19/27/37）。架构归属以 `ARCHITECTURE.md` v0.11 为准（SA-02/SA-06/SA-10/SA-15 直接约束本单元）；本文对其含混或未登记事项的解释集中登记于 §15.3（P-POL-1～9），不私自修改上游。

**两条贯穿全文的所有权声明**：

1. **策略权威（ARC-05/SA-06）**：`EngineeringPolicySet` 是唯一权威策略对象；碰撞等共享规则只有一个权威策略对象和一个共享评估实现。policy 之外任何单元不得持有影响计算的策略副本、私有开关、默认值或第二套碰撞算法（NFR-MNT-07 静态检查承载）。
2. **工程结论归 evidence（§8.1/EVI-01）**：policy 产出**构型级/路径级**碰撞事实与比较型阈值结果；碰撞判定的固有作用域是构型/路径级（C8）——某 IK 解碰撞＝该解被硬过滤、某候选路径碰撞＝该路径淘汰并触发重规划，policy **不产生**任务级可行性/不可行性结论，不汇总证据，不发布正式工程判定（归 evidence 的五级汇总）。

### 1.2 上游与磁盘现状登记（2026-09-10 实测）

| 项 | 状态 | 说明与对本文的影响 |
| --- | --- | --- |
| `REQUIREMENTS.md` | 存在，v1.16，`Accepted` | 唯一需求权威源。本文承接：ARC-05、CON-04/05/06、TASK-02/03（结果形态约束，仅经 evidence 契约间接消费）、ERR-01、EVI-01/§8.1 全表、MDL-04/06④/15/22、KIN-02/04/05/09/13、TRJ-04、OPT-03（OPT-B 碰撞静态子集）、SEL-05（阈值归属待裁决 P-POL-3）、UX-08、NFR-COR-02/03/05、NFR-MNT-01/03/07、NFR-DEP-05、附录 D（第 11 项行程上限 4π＝唯一已冻结工程策略默认；第 12 项名称精确等值；C4/C7 比较与 ε_abs 量纲）、AT-01/03/19/27/37 |
| `ARCHITECTURE.md` | 存在，v0.11，`Draft`（待评审） | 唯一架构权威源。§3.1 policy 行、§3.5 依赖表（policy→core 为唯一 industrialrobot 编译边）、§7.5 策略权威、§7.2 ④端口、§7.3 编译链（碰撞场景供 policy 适配）、§5.1 RobWork 碰撞接口经 runtime/policy 适配、SA-02/06/10/15 直接约束本文。若评审产生 A9+ 处置按影响面同步（P-POL-4） |
| `units/core.md` | 存在，v0.1，`Draft`（未冻结） | 协作输入。本文消费其身份/摘要/单位/比较/诊断/词表契约（§3.2 逐项登记）；其 §10.3 已向 policy 交接"EngineeringPolicySet 序列化与阈值数值（附录 D 第 11 项等）"——本文 §4/§5 即该交接的承接答复；其 §2.2 明确 core 不拥有 EngineeringPolicySet/CollisionEvaluator/阈值数值（本文所有权与其互补） |
| `units/evidence.md` | 存在，v0.1，`Draft`（未冻结） | 协作输入。其 §13 要求 policy 侧提供："策略与 RuntimeNameMap 的 canonical 序列化与内容身份计算（各自任务卡登记）"与"复现块的碰撞后端版本字段"——本文 §5.3/§8.1 承接；其 Policy 依赖条目、`DeterministicInfeasibilityProof.MandatoryStateCollision`（必经状态碰撞证明需对象 ID 对＋判定）、`SearchExhaustedRecord.filteredSolutions`（过滤原因含 Collision）、`ReproductionBlock.collisionBackendVersion` 均为本文输出契约的对端（§8.4 对齐） |
| `units/testkit.md` | 存在，v0.1，`Draft`（未冻结） | 协作输入。本文 §11 验证方案消费其 `IRD_TEST_INFO`/`IRD_EXPECT_*`/`checkDiagnosticRecord`/`checkComparativeFields`/`checkTaskIdentity`/`checkSetEquivalent`/`checkStableOrder`/`DeterministicEnv`/`FaultInterceptor`；其容差档案 `source=engineering-policy-default` 类别即附录 D 第 11 项（§4.4 承接）；其 T-1/T-2 测试侧红线本文测试目标一并遵守 |
| `units/project.md` | 存在，v0.1，Draft，§1～§15 已齐备 | 2026-09-10 同步：历史磁盘不完整问题已消除；跨单元接口仍按 CR-03 最小契约复核，不因正文齐备而自动 frozen。 |
| `units/runtime.md` | 已存在（v0.2，2026-09-10 产出并经 CR-04 修订） | policy 消费的运行时碰撞场景、Frame/Geometry/Device 对象与名称解析来自 runtime（ARCH §7.3/§7.4）；其详设已产出且 §7.3/§8.3/§9.1/§10.5 承接本文 §3.3/§6.1 全部期待——P-POL-1 交叉核对已完成（CR-04，记录见 traceability/foundation-api-diff.md）：名称解析经 `IRuntimeNameResolver` 适配、WorkCell 只读经 `WorkCellConstView`（共享所有权以快照别名构造等价）、场景内容身份取 `workCellCompileIdentity`；本文最小注入接口维持不变、零 runtime 编译依赖维持 |
| `units/policy.md` | **不存在**（本文新建） | 其余 15 个单元任务卡未产出 |
| `DETAILED-DESIGN.md` | 已建立 | 20 个单元详设总目录，本文是单元详设正文 |
| `development-task-breakdown.md` | 已建立 | WP-A～WP-I 主 WP 计划，本文只维护单元任务 |
| 构建骨架 | 存在：`industrialrobot/CMakeLists.txt`＋20 单元目录＋`patches/`，共 23 文件（实测一致） | `sdurws_ird_policy` 为 INTERFACE 占位，无源码；`policy/include/sdurws/ird/policy/README.md` 原引本文"§9"，已同日修正为"§12"（任务拆分实际章节，见变更记录） |
| RobWork 基线（碰撞侧） | 存在：`RobWork/src/rw/proximity/`（`sdurw_proximity` 子系统，随 `sdurw` 聚合导出） | 实测可用类型：`CollisionDetector`/`CollisionStrategy`/`CollisionToleranceStrategy`/`DistanceCalculator`/`DistanceStrategy`/`DistanceMultiStrategy`/`ProximitySetup`/`ProximitySetupRule`（INCLUDE/EXCLUDE 规则、按 Frame 名模式）/`ProximityFilter`/`BasicFilterStrategy`；内置策略 `rwstrategy/ProximityStrategyRW`（OBV 树＋`BVTreeToleranceCollider`）；**无** Yaobi/PQP 外部策略目录（实测 `rwstrategy/` 无此二者）。后端基线冻结登记归 NFR-DEP-05（P-POL-5） |
| `old/` | **不存在于磁盘**（git 亦未跟踪） | 与 REQUIREMENTS v1.10 声明不符（core.md §1.2 R-5 已登记）；对本文无输入。附录 B 裁决排除项与 policy 相关者：哈希/指纹校验链（本文内容身份只用于 CON-05/06 授权的切片/缓存/追溯）、旧"碰撞开关"散布式实现（ARC-05 禁止，不恢复） |

### 1.3 设计目标

1. **统一契约**（ARC-05/CON-06）：版本化的 `EngineeringPolicySet`、其内容身份、碰撞作用域与过滤规则、关节限位/行程上限阈值契约、`CollisionEvaluator` 唯一实现在本文单点冻结；evidence、runtime、kinematics、trajectory、dynamics、selection、optimization 依据同一契约读取策略与执行碰撞校验，无需再协商。
2. **唯一实现**（SA-06/NFR-COR-05/AT-19）：运动学、轨迹、优化、选型各入口调用同一 `ICollisionEvaluator` 实例（主进程 1 个、每 worker 各 1 个，同策略＋同场景→等价行为）；业务单元不自行实现碰撞、不自行定位碰撞对象、不使用隐式阈值、不把 RobWork 默认碰撞设置当作产品策略。
3. **身份可追溯**（CON-05/06/SA-07）：策略变化必然产生新内容身份；策略身份进入依赖切片与缓存键；过滤关系全部进入策略身份与评估诊断；"策略配置存在"≠"策略已验证"，未通过校验的策略不得发布、不得进入正式证据链。
4. **作用域纪律**（§8.1 表 2/C8）：碰撞判定只到构型/路径级；评估失败、取消、缺检测器绝不伪装为"无碰撞"或"碰撞"；任务级不可行证明素材（必经状态碰撞）按 evidence 契约供给，判定归 evidence。
5. **数值纪律**（附录 D/C4/C7、NFR-COR-03）：硬约束、策略阈值、数值容差三分不混；阈值唯一来源是策略；比较方向与边界包含关系冻结；浮点近似相等严禁作为身份键；非有限数拒绝。
6. **零 Qt、零横向依赖**（NFR-MNT-01、ARCH §2.3 L2 行、§3.5 依赖表）：policy 编译依赖仅 core＋RobWork 基线（rw::math/models/kinematics/proximity）＋标准库；evidence/runtime/project/execution/业务单元的能力一律经**值传递＋最小注入接口**获得（§3.3，与 evidence.md D-07、project.md IModelCompilePort 同一模式）。
7. **不预建空业务**：阶段 A 交付策略模型/解析/校验/身份、碰撞评估唯一实现（经 RobWork 适配）、关节限位与行程阈值契约、兼容判定、测试替身与契约测试；不实现 FK/IK/轨迹/动力学/选型/优化算法（阶段 B/C/D 经③端口接入），不伪造工程结论。

### 1.4 语言、标准库与构建约束（按仓库实测确认，与 core.md/evidence.md §1.4 同源）

| 项 | 实测事实 | 本文决定 |
| --- | --- | --- |
| 编译器/生成器 | 构建缓存 `build/CMakeCache.txt`：Visual Studio 17 2022（MSVC x64） | 按 MSVC 设计，不做平台特定扩展 |
| Qt | Qt 6.11.1（msvc2022_64） | **policy 目标禁止包含任何 Qt 头**（构建红线 R-3，ARCH §3.2；L2 计算内核零 Qt） |
| C++ 标准 | 基线 RobWork `CMAKE_CXX_STANDARD 11`；industrialrobot 目标显式 C++17（core.md D-01） | 同口径：显式 `cxx_std_17`；`std::optional/variant/string_view/shared_ptr` 允许；不用 C++20；不引入 Boost 等第三方运行库 |
| RobWork 基线类型 | `rw::math`（`sdurw_math`）、`rw::kinematics`（State，`sdurw_kinematics`）、`rw::models`（WorkCell/Device，`sdurw_models`）、`rw::proximity`（`sdurw_proximity`） | policy **直接消费、不包装**（NFR-MNT-04）：`rw::math::Q`、`rw::kinematics::State`、`rw::models::WorkCell`/`Device`、`rw::proximity::CollisionDetector`/`DistanceCalculator`/`ProximitySetup` 族。目标接线（`sdurw_math`/`sdurw_kinematics`/`sdurw_models`/`sdurw_proximity`）随 POL-T01 落位核对 |
| 异常 | RobWork 惯例异常（`rw::common::Exception`） | policy 用自有异常类型 `PolicyError`（§3.1）；**评估实现必须捕获 RobWork 异常**并转为 `Failed`＋诊断（§6.3，不吞、不崩、不跨进程抛出） |
| JSON/序列化 | 无共享 JSON 库（vcpkg installed 实测无，testkit.md §1.4） | 策略 canonical 编码为**自有二进制编码**（`PolicyCodec`，§5.3）——与 evidence.md D-02 同判据；项目侧字节对 project 不透明 |
| 碰撞后端 | 内置 `rw::proximity::rwstrategy::ProximityStrategyRW`（含容差碰撞器）为默认且唯一注册后端 | 后端身份经 `CollisionBackendDescriptor` 冻结进策略会话与复现块（NFR-DEP-05 基线登记后锁定取值，P-POL-5）；不引入第二碰撞算法 |

---

## 2. 需求承接与职责边界

### 2.1 拥有／消费／不拥有总表（本节先行给出，全文以此为准）

**policy 拥有（实现责任方）**：

| # | 能力 | 上游依据 | 设计落点 |
| --- | --- | --- | --- |
| O-1 | `EngineeringPolicySet` 数据模型（碰撞规则、作用域、过滤/必检规则、关节限位与行程上限阈值、适用范围、Must/Should 级别、来源/校验状态/诊断、兼容与迁移信息） | ARC-05、MDL-06④（M-10）、KIN-13、TRJ-04③、ARCH §7.5 | §4 |
| O-2 | 策略解析管线（原始输入→规范化/单位校验→默认解析→冲突检查→适用范围验证→内容身份→发布只读对象）与 `IPolicyValidator` | ARC-05、NFR-COR-03、CON-06 | §5.1/§5.2/§9.2 |
| O-3 | 策略 canonical 序列化（`PolicyCodec`）与内容身份计算（SHA-256，经 core `ContentDigester`） | CON-05/06、core.md §4.2 责任边界、evidence.md §13 交接 | §5.3 |
| O-4 | `CollisionEvaluator` 唯一实现（`RobWorkCollisionEvaluator`＋评估会话）、RobWork 碰撞适配（内置后端） | ARC-05、NFR-COR-05、KIN-05、ARCH §5.1/§7.5 | §6 |
| O-5 | 碰撞作用域、过滤与必检规则的可执行语义（作用域矩阵、过滤可追溯性、"过滤不得隐藏必检"结构保证） | MDL-04/15、KIN-02/05、TRJ-04、ARCH §7.5 | §7.1/§7.2 |
| O-6 | 阈值/容差/数值语义的产品侧承载（边界值决策表、比较方向、单位、非有限拒绝）；行程上限 4π 默认（附录 D 第 11 项——唯一已冻结工程策略默认） | 附录 D 第 11 项、C4/C7、MDL-06④、TRJ-04③ | §4.4/§7.3/§7.4/§7.5 |
| O-7 | 策略版本、内容身份与兼容性纯判定（`checkPolicyCompatibility`） | CON-04/05/06、NFR-DEP-05 | §8.1/§8.2/§9.5 |
| O-8 | ④策略端口接口（`IPolicyProvider`：策略只读解析＋共享碰撞评估器＋后端描述符） | ARCH §7.2④ | §9.1 |
| O-9 | 关节限位与行程上限评估契约（`IJointLimitEvaluator`：近限位比、行程上限比较型结果——供 project 处理器构造 ConfirmableFinding） | MDL-06④/M-10、SA-15、KIN-13 | §9.4 |
| O-10 | policy 侧稳定诊断码建议表与诊断构造（`IPolicyDiagnostics`；码值权威归 diagnostics 注册表） | ERR-01、NFR-MNT-03 | §9.6 |
| O-11 | 策略/碰撞测试替身（header-only `ScriptedCollisionEvaluator`/`StubPolicyProvider`，仅测试目标可用）与契约测试夹具 | 任务约束§五.10、ARCH §11.2 | §11/§12 |

**policy 消费（经依赖白名单与注入）**：

| 消费方 → 被消费 | 形态 | 消费内容 | 上游依据 | 状态 |
| --- | --- | --- | --- | --- |
| policy → core | 接口依赖（编译链接，**唯一 industrialrobot 边**） | ObjectId/ContentVersion/ContentIdentity/ContentDigester、Tolerance 值形状、UnitToken/convert、SourcedValue、DiagnosticRecord/ComparativeFields、EvaluationMode、CoreError | ARCH §3.5 表 | core.md v0.1 Draft 未冻结（P-POL-4） |
| policy →（注入）runtime 运行时对象 | **值传递＋共享只读句柄**（零编译依赖） | 编译产物 WorkCell/Frame/Geometry/Device（不可变共享）、场景内容身份、运动学相邻对事实——经 `CollisionScene` 值结构（§3.3/§6.1） | ARCH §3.5（表未登记 policy→runtime 边）、§7.3 | runtime.md 已产出，期待经其 §8.3/§10.5 承接（P-POL-1 已核对关闭，CR-04） |
| policy →（注入）runtime 名称解析 | **运行时注入**（零编译依赖） | ObjectId↔设备作用域全名 的只读解析与 RuntimeNameMap 内容身份——经本文最小接口 `IPolicyNameContext`（适配器归 L5 装配，§3.3） | ARC-04、CON-06、R-4 红线 | 同上（P-POL-1 已核对关闭：IRuntimeNameResolver 适配，CR-04） |
| policy →（注入）project 对象字节 | 运行时注入 | 策略对象字节读取，经 `IPolicyBytesSource`（适配 project ②端口 `tryObject`） | ARCH §7.2②④ | project.md §5.2 已有（P-POL-6：其 §8 后缺失） |
| policy →（消费）evidence 契约 | **文档级对齐**（零编译依赖） | Policy 依赖条目语义、`collisionBackendVersion` 复现要素、必经状态证明 `collisionPairs` 字段形状、证据项状态词表——本文输出契约与之对齐（§8） | CON-05/06、EVI-01 表 2/4 | evidence.md v0.1 Draft（P-POL-4） |
| policy → RobWork 基线（L1） | 编译链接 | `rw::math/kinematics/models/proximity` 碰撞基础类型（唯一 RobWork 接触点之一，ARCH §5.1） | SA-02、NFR-DEP-05 | 基线零源码修改；后端版本冻结登记 P-POL-5 |

**policy 不拥有（其他单元所有，本文不实现、不预建桩）**：

| # | 不拥有内容 | 所有者 | 上游依据 |
| --- | --- | --- | --- |
| N-1 | 项目文件读写、修订/分支/锁/事务、对象编址与身份分配、策略对象的持久化与升级器、命令服务 | project | §17 表注、ARCH §6；policy 只提供纯策略数据（对象字节 schema 归 policy，磁盘归 project） |
| N-2 | AnalysisSnapshot、InputSlice、ResultEnvelope、ResultCurrentness、证据 Profile 与门禁、任务级不可行汇总、结果包络 | evidence | CON-02/05、EVI-01/02、SA-07/SA-13 |
| N-3 | CanonicalModel、确定性编译、RuntimeNameMap 生成/反解实现、基座—世界变换的拥有与计算 | runtime | ARC-03/04、MDL-22、SA-04/SA-05 |
| N-4 | RobotDesign 编辑、物理合法性硬断言执行（断言①～④硬部分）、双编译触发、URDF/模板建模 | modeling＋project 处理器 | MDL-01~06/12~14；policy 只供给行程上限比较型结果 |
| N-5 | FK、IK、轨迹规划/复检协议、动力学、选型筛选、优化搜索与 Pareto | kinematics/trajectory/dynamics/selection/optimization | KIN/TRJ/DYN/SEL/OPT 家族；TRJ-04 复检采样协议归 trajectory（policy 只按给定样本评估） |
| N-6 | RunRegistry、任务调度、取消状态机、工作进程池、缓存/检查点存储治理、迟到结果接纳 | execution | TASK-01~03、ARCH §4；policy 只接受注入的取消查询与调用上下文 |
| N-7 | 诊断码注册表实现、稳定码分配、文案、两级日志、脱敏 | diagnostics | ERR-01、ARCH §7.8；policy 提供建议码值与构造数据 |
| N-8 | 报告渲染、界面呈现、工程策略编辑界面与确认对话 | reporting/ui | UX-08（统一入口与摘要呈现归 ui；编辑命令经①端口归 project 编排） |
| N-9 | 可确认放行的执行（收集待确认集、凭据校验、留痕、阻止/放行决策） | project（SA-15） | ARCH §7.1；policy 供给比较型数据（O-9） |
| N-10 | 工程判定阈值之外的业务数值（IK 容差＝分析配置默认、黄金数据集容差＝测试对照） | kinematics（KIN-13）/testkit | 附录 D 类别说明；policy 只持有"工程策略默认"类别条目 |

### 2.2 直接承接的需求（policy 为实现责任方或数据契约责任方之一）

| 需求 | policy 承接的部分 | 不可越界的部分 |
| --- | --- | --- |
| ARC-05（统一策略契约；唯一权威策略对象＋唯一共享评估实现；无私有开关/默认值/重复算法） | `EngineeringPolicySet` 唯一定义、`CollisionEvaluator` 唯一实现、④端口供给、"策略副本/私有开关"的静态检查例外登记（policy 为合法所有者） | 跨单元禁止清单的执法归构建门禁（WP-01）；其他单元的合规由其详设承接 |
| CON-04（缓存/检查点按契约判断兼容） | 兼容判定的纯函数（§8.2）与版本要素供给（策略/后端/数值契约版本） | 缓存/检查点存储与命中执行归 execution（消费 evidence `judgeCacheHit`） |
| CON-05（切片内容身份驱动失效） | 策略内容身份计算与"策略变化→新身份"保证；失效矩阵 policy 行的语义 | 切片构建/失效计算归 evidence；policy 行外的传播归各域声明 |
| CON-06（快照保存已解析策略身份；策略进依赖切片与缓存键；跨入口一致） | 已解析策略的内容身份；名称消费全部经 `IPolicyNameContext`（不自行拼接）；同一策略＋同一场景→同一判定（确定性会话） | 快照字段与门禁归 evidence；名称反解执行归 runtime/execution 接纳流程 |
| ERR-01（三轴正交；稳定诊断绑定对象；比较型三要素；不适用显式标记） | policy 诊断码建议表、比较型构造（实际值/期望值/单位）、`NotApplicable` 输出（如策略未启用该检查时） | 码值分配与文案归 diagnostics；显示归 ui |
| EVI-01/§8.1 表 1/2/4（模式效力；碰撞作用域 C8；搜索未果口径；证据明细） | 碰撞证据明细（对象 ID 对＋判定＋采样位置＋过滤记录）与必经状态碰撞素材；评估器**无**模式输入、**无**任务级结论输出 | 汇总优先级、正式通过判定、Profile 注册与消费归 evidence |
| MDL-04（碰撞网格与自碰撞配置输入） | 策略消费碰撞几何（经场景清单）；导入侧自碰撞配置进入策略草稿的解析入口（RawPolicyInput 来源之一） | 导入/编辑/编译归 modeling |
| MDL-06④（行程上限＝可配置工程策略校验，默认 4π，比较型诊断＋显式确认放行） | 阈值持有（默认 4π——附录 D 第 11 项）、比较型结果供给（`IJointLimitEvaluator`）、continuous 豁免与工程工作范围有限性的检查语义 | 断言执行、确认放行流、命令摘要留痕归 project（SA-15）；硬断言（qmin＜qmax）归 modeling/project 处理器 |
| MDL-15（环境对象显式引用参与碰撞） | 环境域作用域与对象引用语义（经场景清单＋策略规则） | 场景对象编辑归 modeling |
| MDL-22/AT-37（基座—世界变换单一消费） | 碰撞评估消费场景内同一编译变换（禁止二次旋转——会话直接使用编译产物状态） | 变换的拥有与编译归 runtime；四消费方一致测试为 runtime 牵头的契约测试（policy 侧观测点 §11） |
| KIN-02（解集硬过滤含碰撞） | 过滤用碰撞判定的唯一来源（构型级输出） | 过滤与排序逻辑归 kinematics |
| KIN-04/KIN-09（覆盖/采样碰撞证据按统一策略） | 采样碰撞评估（SampleSet 查询）与 coverage 输出；碰撞启用状态只读引用已解析策略 | 覆盖率算法、采样计划归 kinematics；KIN-09 R2 |
| KIN-05（缺碰撞检测器→数据不足，不得视为无碰撞） | 检测器不可用→`Failed`＋`POLICY-CLL-DETECTOR-UNAVAILABLE`；几何缺失→coverage 标记（不输出"无碰撞"结论字段） | DataInsufficient 判定归 evidence/kinematics 消费本文输出 |
| KIN-13（求解配置不得覆盖策略；判定阈值归策略） | 阈值持有（近限位比/条件数警告/行程上限）；评估 API **无阈值参数**（结构上不可覆盖）；配置与策略的身份分立 | AnalysisConfiguration schema 与持久化归 kinematics |
| TRJ-04（复检协议消费工程策略；无私有线宽/阈值） | 安全间距阈值唯一来源；路径序列评估（PathSequence）输出采样位置供复检证据；细分协议与预算归 trajectory（P-06） | 复检执行、段级 DataInsufficient 判定归 trajectory |
| OPT-03（OPT-B 碰撞静态硬约束） | 静态构型/采样碰撞评估（同一实现） | 约束编排、阶段锁归 optimization |
| SEL-05（惯量比阈值为可配置工程规则） | **归属待裁决**（P-POL-3）：本文按"可经策略阈值条目扩展承载"预留 schema 通道，不预填数值、不默认归属 | selection 域配置或策略扩展，待上游裁决 |
| NFR-COR-02（确定性） | 会话不可变＋纯评估＋稳定排序＋版本/后端要素进身份；随机性：碰撞评估无随机源（显式声明）；并行：查询间无共享可变状态、无归约 | 端到端确定性（种子/线程入运行身份）归 evidence/execution |
| NFR-COR-03（非有限/非法单位/引用缺失不得静默转 0/通过） | 解析期拒绝（§5.2）；评估期非有限距离→诊断（§7.5）；名称不可解析→拒绝（不猜对象） | 导入层数据纪律归 io |
| NFR-COR-05（三入口对象 ID 对/判定/原因完全一致） | 同一实现＋同一会话身份＋稳定排序＋原因码（含过滤原因）输出 | 跨进程一致性由 execution 装配 manifest 保障（evidence §9.4） |
| NFR-MNT-01/03/07（零 Qt；单一权威定义；静态检查禁止副本） | 本单元零 Qt；策略/阈值/碰撞语义唯一定义点；policy 为 NFR-MNT-07 例外登记的"策略所有者实现"之一 | 执法归构建门禁与静态扫描（WP-01） |
| NFR-DEP-05（冻结版本基线含碰撞后端及版本） | `CollisionBackendDescriptor` 供给（后端 id/版本/容差模型） | 基线登记与打包归 WP-24 |
| UX-08（统一工程策略入口；计算开关与显示开关分组异名） | 策略只读摘要数据（供 ui 呈现）与"显示开关不入策略身份"保证 | 入口 UI、摘要呈现归 ui |
| 附录 D 第 11/12 项、C4/C7 | 4π 默认持有（唯一）；身份比较精确等值（第 12 项）；比较语义经 core（不复制公式）；策略侧数值校验的 ε_abs 按 C7 量纲取用 | 数值容差权威归附录 D/core；修改走需求变更 |

### 2.3 明确不做（非目标，与 §2.1 不拥有表互为补充）

不实现第二套碰撞算法、第二套阈值来源或第二套策略解析路径（ARC-05）；不把 RobWork 默认碰撞设置（文件内嵌 CollisionSetup、策略默认容差、默认过滤）当作产品策略（§6.5 规则 R-POL-3）；不复制 core 的诊断、单位与身份类型（NFR-MNT-03）；不自行新增证据等级、不修改需求阈值、不改变工程判定优先级（附录 D/§8.1 为准）；不把"策略配置存在"当作"策略已验证"（ValidationState 区分，§4.5）；不把局部碰撞、搜索失败或评估失败升级为任务级不可行（C8/C5——policy 无该输出）；不按工况覆盖策略阈值（P-POL-7 登记解释）；不创建空页面/占位模块（TRJ-08 精神同源）；不代写 runtime/evidence/project 详设（缺者以最小契约＋待裁决登记）。

---

## 3. 单元组成、依赖与公共头文件布局

### 3.1 组成（公共头模块）

policy 由 11 个公共头模块＋1 个编译实现组成（实现文件随 §12 任务落地，头为契约权威）：

| 头（`policy/include/sdurws/ird/policy/`） | 内容 | 详见 |
| --- | --- | --- |
| `Errors.hpp` | PolicyErrorCode（稳定 token）、PolicyError | §3.1 尾、§5.2 |
| `PolicySet.hpp` | PolicyValueOrigin、PolicyRuleLevel、CollisionDomain、ScopeTarget、PairRule、CollisionRules、JointThresholds、PolicyApplicability、PolicyValidationState、PolicyOrigin、EngineeringPolicySet | §4 |
| `PolicyInput.hpp` | RawPolicyInput、PolicySchema 常量、PolicyCodec（encode/decode/contentIdentity） | §5.1/§5.3 |
| `PolicyParsing.hpp` | PolicyParseResult、resolvePolicy（解析管线纯函数）、IPolicyValidationContext、IPolicyValidator | §5.1/§9.2 |
| `Contexts.hpp` | IPolicyNameContext、IPolicyCallContext、IPolicyBytesSource（注入用最小接口） | §3.3/§9.7 |
| `PolicyPort.hpp` | PolicyResolutionRequest/PolicyResolution、CollisionBackendDescriptor、IPolicyProvider（④端口） | §9.1 |
| `CollisionQuery.hpp` | CollisionQueryKind、CollisionQuery、CollisionFindingKind、CollisionFinding、PairCoverageRecord、AppliedFilterRecord、SampleDistanceSummary、CollisionEvaluationStatus、ScopeApplicability、CollisionEvaluation | §6.2 |
| `CollisionEvaluator.hpp` | ICollisionEvaluator、CollisionEvaluationSession、CollisionScene、SceneObjectEntry/Role、makeRobWorkCollisionEvaluator（唯一实现工厂） | §6.1/§9.3 |
| `JointLimits.hpp` | JointLimitSpec/Query/Finding/Evaluation、IJointLimitEvaluator | §9.4 |
| `Compatibility.hpp` | PolicyConsumerRequirements、PolicyIncompatibilityReason、PolicyCompatibility、checkPolicyCompatibility、IPolicyCompatibilityChecker | §8.1/§9.5 |
| `Diagnostics.hpp` | policyDiagCodes（建议码表）、IPolicyDiagnostics、makeComparative 辅助 | §9.6 |
| `README.md` | 既有保留位说明（不参与编译；指向本文 §12——骨架现引"§9"，随 POL-T12 修正） | — |
| `src/`（实现，随 POL-T01 建） | 解析器、codec、RobWork 适配（评估器/会话）、限位评估、兼容判定、provider 实现 | §4～§9 |
| `test/sdurws/ird/policy/testdouble/`（**不参与产品编译**，仅测试目标 include） | `ScriptedCollisionEvaluator.hpp`、`StubPolicyProvider.hpp`（header-only 可控替身，§11） | §11/§12 |

### 3.2 依赖（含 core 契约消费状态登记）

```
sdurws_ird_policy ──► RWS::ird::core（PUBLIC：身份/摘要/单位/比较/诊断数据/评估词表）
                  ──► RobWork 基线（PUBLIC：sdurw_math / sdurw_kinematics / sdurw_models / sdurw_proximity）
                  ──► C++17 标准库
                  ──✖ 零 Qt（R-3）、零 Eigen 直接包含（经 rw 头传递）、
                     零 evidence/runtime/project/execution/业务单元链接（ARCH §3.5：L2 内核不横向互链）
运行时注入（零编译依赖）：IPolicyNameContext / IPolicyBytesSource（L5 装配适配 runtime⑥端口与
                     project②端口）；IPolicyCallContext（execution 注入取消/上下文存活）
```

消费的 core 契约清单（全部来自 core.md v0.1 §4/§5；**状态：Draft 未冻结**，P-POL-4）：

| core 契约 | policy 用途 | 状态锚点 |
| --- | --- | --- |
| ObjectId / ContentVersion / ContentIdentity / ContentDigester | 策略对象引用、策略内容身份（对策略语义内容做摘要——core §4.2 责任边界的 policy 侧承接） | v0.1 §4.1/§4.2/§5.2 |
| UnitToken / convert / QuantityKind | 策略输入单位归一化（SI）、诊断比较型单位标注 | v0.1 §4.4/§5.4 |
| Tolerance（值形状） | 策略侧数值校验的承载（数值不在此定义）；policy 不复制比较公式（closeWithin 归 core） | v0.1 §4.5 |
| DiagnosticRecord / ComparativeFields / SourcedValue | 校验/评估诊断构造、比较型三要素、输入保留原文 | v0.1 §4.8/§5.7 |
| EvaluationMode | 适用范围模式核对（兼容判定） | v0.1 §4.7/§5.6 |
| CoreError | core 抛错捕获与转发 | v0.1 §4.10 |

消费的 evidence 契约（**零编译依赖，文档级对齐**，P-POL-4）：

| evidence 契约 | 对齐点 |
| --- | --- |
| `DependencyKind::Policy`（policyContentIdentity 进入 sliceId） | §5.3 内容身份即该条目取值；§8.6 失效矩阵 |
| `ReproductionBlock.collisionBackendVersion`（策略启用碰撞的评估必填） | §8.1 `CollisionBackendDescriptor` 版本串为其取值来源 |
| `DeterministicInfeasibilityProof.MandatoryStateCollision.collisionPairs`（对象 ID 对＋判定） | §6.2 `CollisionFinding` 字段兼容映射（§8.4） |
| `SearchExhaustedRecord.filteredSolutions`（filterReason 含 Collision） | §6.2 构型级发现即其素材（过滤动作归 kinematics） |

### 3.3 对 runtime / project 能力的注入边界（依赖白名单的实施形态）

ARCH §3.5 依赖表**未登记** policy→runtime / policy→project 边（三者同属 L2/L3 平台层，ARCH §3.2 明文"平台内核间彼此不横向互链"），而碰撞评估客观上需要 runtime 编译产物与 project 对象字节。解决方案＝**值传递＋最小注入接口**，与 evidence.md §3.3（D-07）、project.md `IModelCompilePort`（P-PR-7）同一模式：

```cpp
// Contexts.hpp —— policy 定义的最小只读接口（适配器归 L5 装配或请求方，不归 policy/runtime/project）
class IPolicyNameContext {            // 适配 runtime ⑥端口 RuntimeNameMap（只读）
public:
    virtual ~IPolicyNameContext() = default;
    virtual std::optional<core::ObjectId> tryObjectId(const std::string& runtimeName) const = 0;
    virtual std::optional<std::string>   tryRuntimeName(core::ObjectId) const = 0;
    virtual core::ContentIdentity nameMapContentIdentity() const = 0;   // CON-06
};
class IPolicyBytesSource {            // 适配 project ②端口 tryObject(object, contentVersion)
public:
    virtual ~IPolicyBytesSource() = default;
    virtual std::optional<std::vector<std::uint8_t>>
        tryObjectBytes(core::ObjectId, core::ContentVersion) const = 0;
};
class IPolicyCallContext {            // 评估调用上下文；实现由 execution/宿主注入
public:
    virtual ~IPolicyCallContext() = default;
    virtual bool cancellationRequested() const = 0;   // 协作取消查询（NFR-PERF-02 精神）
    virtual bool alive() const = 0;                   // 运行/快照上下文存活（迟到调用拒绝，§6.1）
};
```

- **适配器位置**：L5 应用壳装配期提供共享适配器（推荐，NFR-MNT-04——避免每个请求方重复适配）；适配器约十行转发，价值＝跨层依赖方向隔离（ARC-02 端口精神的实施件），非"仅转发调用无边界价值的包装器"。
- **场景消费形态**：runtime 编译产物（WorkCell 实例）以 `std::shared_ptr<const rw::models::WorkCell>` **共享只读**进入 `CollisionScene`（§6.1）；policy 不复制、不修改、不重建编译产物（ARC-03：一切计算只消费 CanonicalModel 编译链产物）。基座—世界变换随编译产物内置，policy 禁止二次旋转（MDL-22/M-11）。
- **R-4 红线合规**：policy 全部名称消费经 `IPolicyNameContext` 或场景内 Frame 对象自带的完整名（编译产物事实，非拼接产物）；policy 实现内**不得出现**前缀拼接/剥离逻辑（静态检查例外登记仅 runtime 名称解析器与 policy 策略消费点——消费≠拼接，登记细则见 P-POL-8）。
- **worker 进程**：工作进程内的 `IPolicyBytesSource` 实现为"随请求物化的快照载荷存储"（execution 序列化装配）；`IPolicyNameContext` 由 runtime 在 worker 侧按同一编译产物构建（装配侧保证与主进程同一 nameMapContentIdentity）。

### 3.4 命名空间、目标与 CMake 集成

- 命名空间 `sdurws::ird::policy`；目标 `sdurws_ird_policy`（骨架 INTERFACE → POL-T01 升级 STATIC），别名 `RWS::ird::policy`；`target_compile_features(... cxx_std_17)`；`target_link_libraries(sdurws_ird_policy PUBLIC RWS::ird::core sdurw_math sdurw_kinematics sdurw_models sdurw_proximity)`（目标名按构建树实测核对，POL-T01 完成条件之一）。
- 测试目标：`sdurws_ird_policy_test`（单元内，含 §11 用例体）、`sdurws_ird_policy_contract_test`（跨单元：与 core 值类型、与 testkit 谓词、与 evidence 证明字段形状的对齐断言）；gtest 接入按 development-task-breakdown.md §5.5 定稿——vcpkg＋`find_package(GTest CONFIG REQUIRED)`，失败即停（CR-07）；产品目标不链 testkit（T-1 红线）。
- 头包含形式 `#include <sdurws/ird/policy/PolicySet.hpp>`；私有实现头不入 `include/`（R-2 纪律）；`test/double` 头仅供 `_test`/`_contract_test` 目标（§3.1 表）。
- **policy 侧红线扩展建议（并入 WP-01 门禁，P-POL 登记请求）**：业务目标（L4 计算库/插件）与 evidence/execution/reporting 等不得直接链接 `sdurw_proximity` 或包含 `rw/proximity/*`（例外：policy 产品实现；各 `_test`/`_contract_test` 目标经 testkit/policy 替身消费）——ARC-05"不得有重复算法"与 NFR-MNT-07"共享工程规则副本禁止"的机械化落点。runtime 场景装配若需 proximity 类型（仅限构造 WorkCell 几何挂载）另行登记例外。

---

## 4. EngineeringPolicySet 数据模型（`PolicySet.hpp`）

### 4.1 六个"策略/身份"概念的区别（先于字段表冻结）

| 概念 | 类型/载体 | 回答的问题 | 谁产生 | 与其他概念的边界 |
| --- | --- | --- | --- | --- |
| 策略对象身份 | `core::ObjectId`（policyObject） | "这是哪一个（逻辑）策略对象" | project（对象创建时分配） | 跨修订稳定；与内容无关（同内容不同对象可共存——CON-01 精神） |
| 策略内容版本 | `core::ContentVersion` | "该策略对象内容的哪一版不可变字节" | project（对象写入时，按对象字节编址） | 磁盘侧身份；policy 只读引用 |
| 策略内容身份 | `core::ContentIdentity` | "该策略**工程语义**的规范摘要是什么" | policy（解析发布时对语义内容计算，§5.3） | 与存储字节无关（同语义不同编码→同身份；显示单位/字段排序/来源标注不影响）；进入依赖切片与缓存键（CON-05/06） |
| 策略 schema 版本 | `std::uint32_t` | "策略编码格式的哪一代" | policy（本文件冻结，当前 1） | 兼容性判定维度之一（§8.1）；未知/未来版本拒绝 |
| 策略会话身份 | `CollisionEvaluationSession::sessionIdentity()` | "哪个（策略×场景×名称映射×后端）评估上下文" | policy（会话构建时计算） | 评估侧复合身份（§6.4）；不含运行/尝试身份 |
| 评估运行身份 | `core::TaskIdentity`（五元组） | "哪一次派发/尝试" | execution（派发时） | policy 不分配、不存储；评估输出可被调用方绑定到运行（§8.4） |

**策略配置 ≠ 策略内容身份 ≠ 策略已验证**：配置（RawPolicyInput）是待解析输入；内容身份只对**通过校验并发布**的策略计算；`PolicyValidationState`（§4.5）独立记录校验结论——三者互不推导，"配置存在"不推出"身份有效"，更不推出"已验证可用"。

### 4.2 字段表（全部字段构造后不可变，无 setter；值语义）

| 字段 | 类型 | 必填 | 默认 | 约束与语义 |
| --- | --- | --- | --- | --- |
| `policyObject` | core::ObjectId | 是 | — | 项目对象身份（project 分配）；**不参与内容身份**（§5.3） |
| `schemaVersion` | std::uint32_t | 是 | — | PolicyCodec schema 代号；当前 `1`；解析期校验（§5.2） |
| `contentIdentity` | core::ContentIdentity | 是 | — | 语义内容身份（§5.3，发布时由 policy 计算，调用方不可申报）；**参与依赖切片与缓存键**（CON-06） |
| `collision` | CollisionRules | 是 | — | 碰撞规则子模型（§4.3） |
| `jointThresholds` | JointThresholds | 是 | — | 关节限位/行程阈值子模型（§4.4） |
| `applicability` | PolicyApplicability | 是 | 全部 | 适用模型/任务/工况/评估模式（§4.2.1） |
| `origin` | PolicyOrigin | 是 | — | 创建来源：{kind: Template/Imported/UserEdited/SystemDefault, sourceObject?, note?}；**不参与内容身份**（审计字段） |
| `validationState` | PolicyValidationState | 是 | — | NotValidated/Valid/Invalid（§4.5） |
| `validationDiagnostics` | std::vector\<core::DiagnosticRecord\> | 否 | {} | 解析/校验诊断（含已修复项的告知性诊断）；**不参与内容身份** |
| `compatibilityNotes` | std::optional\<std::string\> | 否 | — | 向后兼容与迁移说明（自由文本，登记迁移注意事项；**不参与内容身份**——迁移执行归 project 升级器） |
| `numericContractAnchor` | std::string | 是 | `"appendixD@v1.16"` | 数值契约基线锚（本策略默认值所依据的附录 D 版本）；参与内容身份与兼容判定（§8.1） |

参与内容身份的字段集合＝{schemaVersion, collision, jointThresholds, applicability, numericContractAnchor}（语义闭包）；不参与＝{policyObject, origin, validationState, validationDiagnostics, compatibilityNotes}（管理与审计）。**显示设置（显示单位、渲染分组、高亮开关）不存在于本类型任何字段**——UX-08"计算开关与显示开关异名分组"的数据层保证；显示变化不可能改变策略身份（POL-ID-3 用例钉住）。

#### 4.2.1 PolicyApplicability（适用范围）

| 字段 | 类型 | 默认（空集语义） | 约束 |
| --- | --- | --- | --- |
| `modes` | std::vector\<core::EvaluationMode\> | 空＝全部模式适用 | 非空时为 Preview/Quick/Verified 子集；兼容判定消费（§8.1） |
| `modelObjects` | std::vector\<core::ObjectId\> | 空＝不限模型 | 非空时策略仅适用于所列 RobotDesign 对象的快照（解析期经 IPolicyValidationContext 校验存在性） |
| `taskObjects` | std::vector\<core::ObjectId\> | 空＝不限任务 | 同上 |
| `caseObjects` | std::vector\<core::ObjectId\> | 空＝不限工况 | **仅限定适用工况范围，不支持按工况差异化阈值**（P-POL-7：工况差异经场景对象集〔负载/工件对象不同〕表达，不经理论阈值覆盖——ARC-05 单一权威） |

### 4.3 碰撞规则子模型（CollisionRules）

```cpp
enum class PolicyRuleLevel { Must, Should };            // token: must / should
enum class CollisionDomain { Self, Environment, Tool, Scene };  // token: self/environment/tool/scene
enum class ScopeTargetKind { Object, Role, Group };     // 目标三类：对象 ID / 角色类 / 显式组

struct ScopeTarget {
    ScopeTargetKind kind;
    core::ObjectId object;        // kind==Object 时有效
    std::string roleToken;        // kind==Role 时有效：RobotLink|Tool|Payload|EnvironmentObject|Workpiece
                                  //   （场景对象角色词表归建模语义，本文消费——§6.1 SceneObjectRole 同源）
    std::string groupName;        // kind==Group 时有效：策略内定义的显式对象组（§7.2）
};

struct PairRule {
    ScopeTarget first, second;    // 无序对语义（解析时规范化为字典序存储）
    PolicyRuleLevel level;        // Must/Should
    std::string reason;           // 非空必填：过滤/必检理由（可追溯性——进入策略身份与评估诊断）
};

struct CollisionRules {
    bool enabled = true;                          // 碰撞域总开关（Quick 预览缺碰撞证据 ≠ enabled=false——KIN-13）
    std::vector<CollisionDomain> enabledDomains;  // 启用域；默认解析 {Self, Environment, Tool}（Scene 默认不启用）
    std::optional<PolicyThreshold> safetyClearance; // 安全间距阈值，SI m，[0, +∞)；**enabled==true 时必须有值**
                                                  //   （解析校验强制——无冻结默认，缺省即非法，P-POL-2）
    bool excludeAdjacentLinksByDefault = true;    // 相邻连杆默认过滤（消费场景的**运动学相邻事实**——模型事实非策略发明）
    std::vector<PairRule> mandatoryPairs;         // 必须检测对（不可被过滤覆盖——结构保证，§7.2）
    std::vector<PairRule> excludedPairs;          // 允许忽略对（可过滤；理由必填）
};
```

语义冻结：

- 域划分：**Self**＝机器人连杆间；**Tool**＝工具/负载与机器人及工件间；**Environment**＝机器人侧（连杆/工具/负载）与环境对象（障碍物/夹具）间；**Scene**＝环境对象间（静态场景默认不检，显式 mandatoryPairs 可启用）。
- `enabled=false` 时其余碰撞字段仍须完整合法（策略可整体停用后恢复）；`enabled=false` 的策略进入切片后，碰撞证据项按"策略禁用"显式不适用（evidence 条件依赖——evidence.md §4.2.3），**不得**解读为"无碰撞"。
- `safetyClearance==0` 合法（退化为仅碰撞检测，无间距检查——边界语义见 §7.4）。
- 工具碰撞域包含"工具与工件"（工件角色 Workpiece 属环境侧对象）——夹持接触等必须检测的接触对经 mandatoryPairs 显式登记（不允许静默排除工具—工件全类）。

### 4.4 关节限位与行程阈值子模型（JointThresholds）

```cpp
enum class PolicyValueOrigin { Explicit, DefaultAppendixD, DefaultTemplate, Inherited };
struct PolicyThreshold {          // 策略阈值统一承载：SI 真值 + 来源（默认/显式/继承三分的载体）
    double siValue;               // 单位由字段位置固定（本表逐字段标注），构造时校验有限性与符号
    PolicyValueOrigin origin;
};

struct JointThresholds {
    std::optional<PolicyThreshold> nearLimitRatio;        // 近限位比警告阈值，无量纲，(0,1]；
                                                          //   nullopt＝该警告检查显式不适用（无冻结默认，P-POL-2）
    std::optional<PolicyThreshold> conditionNumberWarning;// 条件数警告阈值，无量纲，[1,+∞)；同上
    PolicyThreshold finiteRotationTravelLimit;            // 有限限位旋转关节行程上限，SI rad，(0,+∞)；
                                                          //   默认 4π（DefaultAppendixD——附录 D 第 11 项，
                                                          //   唯一已冻结工程策略默认）；仅适用有限限位旋转关节
    bool travelLimitCheckEnabled = true;                  // 校验开关（true=默认执行 MDL-06④ 策略校验）
};
```

- **默认值/显式值/继承值**：`PolicyValueOrigin` 三分＋`DefaultAppendixD`——4π 是唯一由上游冻结的默认（附录 D 第 11 项，`工程策略默认`类别：EngineeringPolicySet 持有、进入依赖切片与缓存键）；其余阈值**无冻结默认**：输入未提供 → `nullopt`（检查显式不适用），**不得**由 policy 发明数值（P-POL-2 登记模板策略数值裁决）。继承值＝新方案分支沿用基线修订的策略对象（project 修订引用语义），解析后 origin 标 `Inherited`。
- 惯量比阈值（SEL-05）**未列入本结构**——归属待裁决（P-POL-3）；裁决归策略时以 schema 次版本追加字段（向后兼容，§5.3 版本策略）。
- 阈值比较的边界语义（含 4π 超限判定：行程 |qmax−qmin| **＞** 阈值才超限，等于阈值不超限）冻结于 §7.4。

### 4.5 未设置、无效、冲突、不适用四态（贯穿区分）

| 状态 | 载体 | 语义 | 后续处置 |
| --- | --- | --- | --- |
| 未设置（NotProvided） | `std::optional` 空 / RawPolicyInput 字段缺失 | 输入未提供该值 | 有冻结默认→默认解析填入（origin=DefaultAppendixD）；无默认→对应检查显式 NotApplicable（不伪造数值——ERR-01） |
| 无效（Invalid） | 解析/校验诊断（PolicyError→DiagnosticRecord） | 已提供但非法（非有限/超域/语法错） | **阻止发布**该策略对象（§5.2）；原文经 core `SourcedValue::invalid` 语义保留于 RawPolicyInput（NFR-COR-03：不静默转 0/默认） |
| 冲突（Conflicting） | 校验诊断（POLICY-RULE-CONFLICT 等） | 规则集内部矛盾（排除覆盖必检、重复、循环引用） | **阻止发布**；逐条定位冲突规则对 |
| 不适用（NotApplicable） | `PolicyValidationState` 之外的字段级显式标记 | 该检查/域对当前输入不适用（如 nearLimitRatio 未设置、Scene 域未启用） | 正常状态，进入策略身份（空 optional 是语义内容）；评估输出显式标记不伪造结论 |

`PolicyValidationState`：`NotValidated`（草稿期中间态，仅解析器内部，不对外发布）→ `Valid`（通过 §5.1 全部校验，唯一可发布态）→ `Invalid`（校验失败，终态；诊断随附）。发布对象**只存在 Valid 态**——`EngineeringPolicySet` 工厂只产出 Valid 实例；Invalid 以 `PolicyParseResult{diagnostics}` 返回，**不产生**可消费的策略对象（"策略配置存在"≠"策略已验证"的机械化）。

### 4.6 可变性、版本与修订引用

- **不可变发布**：`EngineeringPolicySet` 构造后无任何修改途径（值语义＋无 setter）；"修改策略"＝新 RawPolicyInput → 重新解析 → 新内容身份 → 经①命令端口产生新策略对象内容版本与新修订（project 持久化；SA-03/PA-2）。
- **同一项目不同修订引用不同策略**：修订各自引用其闭包内的 (policyObject, ContentVersion) 对象版本；不同修订可引用不同内容身份的策略——历史结果绑定其快照的策略身份，不随 HEAD 变化（CON-02；§8.6）。
- **策略变化对 evidence 与当前性的影响**：策略内容身份变化 → 声明 Policy 依赖条目的切片身份变化 → 相关结果 Superseded（原因＝PolicyChanged，evidence computeCurrentness）；HEAD 前进本身不使任何结果失效（CON-05——仅当其造成被消费条目内容变化）。project 侧确认放行记录绑定 `policyContentId`（project.md §6.7 四元组），策略变化后历史确认不自动迁移（project 语义）。
- **版本策略（schema）**：`schemaVersion` 单调整数；未知/未来版本→只读拒绝＋诊断＋升级指引（PM-06 精神同源）；次版本追加可选字段＝向后兼容（旧编码可解析，新字段按未设置处理）；主版本变更＝破坏性（走设计变更评审＋project 升级器责任评估＋全量切片身份重算登记——因 schemaVersion 参与内容身份）。

**合法/非法实例**：

- 合法：六轴机型默认策略——碰撞启用 {Self,Environment,Tool}，safetyClearance=0.02 m（Explicit），相邻默认过滤，无显式排除对，行程上限 4π（DefaultAppendixD），nearLimitRatio=0.05（Explicit），applicability 全空（全部适用）；内容身份非零。
- 合法：多圈关节机型——finiteRotationTravelLimit=6π rad（Explicit，"±1080° 多圈关节调阈值后正常放行"——AT-01 反例素材）。
- 非法（解析拒绝）：safetyClearance=NaN/−0.01；finiteRotationTravelLimit=0/负/Inf；excludedPairs 与 mandatoryPairs 覆盖同一对象对（冲突）；同一对重复登记；Group 引用未定义组名或组引用组（循环）；mandatoryPairs 引用不存在对象（经 IPolicyValidationContext）；enabledDomains 含未知 token；schemaVersion=2（未来版本）。
- 非法（用法层，编译/运行期拒绝）：对已发布对象调用任何 setter（无此 API）；以 RawPolicyInput 直接充当快照策略身份（未解析对象无内容身份——类型层不可表达）。

---

## 5. 策略解析、校验与版本化（`PolicyInput.hpp` / `PolicyParsing.hpp`）

### 5.1 解析与发布管线（纯函数，确定性，无 I/O）

```cpp
struct PolicyParseResult {
    std::optional<EngineeringPolicySet> policy;   // 仅当全部校验通过（Valid）非空
    std::vector<core::DiagnosticRecord> diagnostics; // 全量（不因首个错误短路——表 2④ 精神）
    std::optional<core::ContentVersion> sourceVersion; // 来源对象内容版本（诊断定位用）
};
PolicyParseResult resolvePolicy(const RawPolicyInput&, const IPolicyValidationContext&);
```

```
原始策略输入（RawPolicyInput：PolicyCodec 解码自项目对象字节；可携带显示单位与未设置字段）
  │
  ▼ ①语法与 schema 校验───── 未知字段拒绝（不静默忽略）；schemaVersion 未知/未来拒绝；
  │                          字段类型/语法（token 表、[0,1] 值域句法）校验
  ▼ ②规范化与单位校验────── 全部数值经 core 唯一换算入口归一 SI；NaN/±Inf 拒绝；
  │                          负值/越域拒绝（NFR-COR-03）；成对规则规范化为字典序（消除顺序歧义）
  ▼ ③默认值解析──────────── 附录 D 第 11 项 4π（唯一冻结默认）填入未设置项（origin=DefaultAppendixD）；
  │                          无默认项保持 nullopt（显式不适用，不发明数值——P-POL-2）
  ▼ ④规则集检查──────────── 重复规则；排除∩必检冲突（含 Role/Group 展开后等价对）；Group 定义循环；
  │                          空集语义（空 enabledDomains 且 enabled=true→非法：无域可检）
  ▼ ⑤适用范围验证────────── modes 合法子集；引用对象存在性（经 IPolicyValidationContext）；范围语法
  ▼ ⑥内容身份生成────────── PolicyCodec 语义闭包 canonical 编码 → SHA-256 → core::ContentIdentity
  ▼ ⑦发布只读策略对象────── EngineeringPolicySet（validationState=Valid；诊断只含告知性条目）
```

`IPolicyValidationContext`（注入，零编译依赖）：

```cpp
class IPolicyValidationContext {
public:
    virtual ~IPolicyValidationContext() = default;
    virtual bool objectExists(core::ObjectId) const = 0;          // 修订闭包内存在性（适配②端口修订闭包）
    virtual std::optional<std::string> objectRole(core::ObjectId) const = 0; // 角色词表核对（建模语义）
    virtual bool groupDefined(std::string_view groupName) const = 0;        // 组名已在本策略定义（内部自洽性二次校验）
};
```

谁在何时解析：请求方（域插件/execution 准备段/project 处理器）经④端口 `IPolicyProvider::resolvePolicy`；解析器为纯函数——同 (RawPolicyInput 字节, 校验上下文) → 同结果（POL-ID-1）；`PolicyProvider` 实现按 (policyObject, ContentVersion) 记忆化缓存（同对象版本重复解析零重算，且身份必然一致）。

### 5.2 校验规则与错误分类表（诊断码/严重级/可确认性）

| 输入异常 | 诊断码（建议值） | 严重级 | 处置 | 可确认放行？ |
| --- | --- | --- | --- | --- |
| 未知字段（schema 未定义的键） | `POLICY-SCHEMA-UNKNOWN-FIELD` | Error | 拒绝发布（不静默忽略——防拼写错误降级为默认值） | 否 |
| 未知/未来 schemaVersion | `POLICY-SCHEMA-VERSION-FUTURE`（＞当前）/ `…-UNKNOWN`（＜已知最低） | Error | 只读拒绝＋升级指引（PM-06 同源）；未来版本不前向猜测解析 | 否 |
| 阈值 NaN/±Inf | `POLICY-THRESHOLD-NON-FINITE` | Error | 拒绝发布；原文保留于 RawPolicyInput | 否 |
| 阈值负值/越域（如 nearLimitRatio∉(0,1]、safetyClearance＜0） | `POLICY-THRESHOLD-NON-POSITIVE` / `…-OUT-OF-RANGE` | Error | 拒绝发布；比较型诊断（实际/期望域/单位——ERR-01） | 否 |
| 必填阈值缺省（enabled==true 而 safetyClearance 未设置——无冻结默认项不得静默省略） | `POLICY-THRESHOLD-REQUIRED-MISSING` | Error | 拒绝发布；定位字段 | 否（如需空缺语义走 P-POL-2 裁决） |
| 单位不一致（异量纲/未注册 token） | `POLICY-UNIT-MISMATCH` | Error | 拒绝发布（core convert 抛错转发） | 否 |
| 重复规则（同对同类型重复） | `POLICY-RULE-DUPLICATE` | Error | 拒绝发布；列出重复条目 | 否 |
| 排除覆盖必检（excludedPairs 展开后与 mandatoryPairs 相交） | `POLICY-RULE-CONFLICT` | Error | 拒绝发布——**结构上保证过滤不得隐藏必检**（§7.2） | 否 |
| 组引用循环/引用未定义组 | `POLICY-RULE-CYCLE` | Error | 拒绝发布（组仅可引用对象，不可引用组——循环在 schema 层不可能，检查为纵深防御） | 否 |
| 规则适用对象不存在（闭包内查无） | `POLICY-SCOPE-OBJECT-MISSING` | Error | 拒绝发布（对象已删除/跨修订混入——定位到对象 ID） | 否 |
| 适用范围无效（未知模式 token/空域启用） | `POLICY-APPLICABILITY-INVALID` | Error | 拒绝发布 | 否 |
| 告知性（默认值已填入/继承标记/向后兼容注记缺失） | `POLICY-INFO-DEFAULT-APPLIED` 等 | Info | 随发布对象附带（validationDiagnostics；不入错误路径） | — |

**校验失败是否阻止编译/评估**：阻止。策略对象无效 → ④端口 `resolvePolicy` 返回空 policy＋诊断 → 快照组装 CON-06 非空校验失败 → 正式评估不派发（evidence ②级通用证据门禁——快照身份缺失/无效 → DataInsufficient）；Preview 模式调用方可继续无碰撞预览（经 EVI-01 模式表达，**不**经覆盖策略——KIN-13）。策略编辑命令（①端口）在处理器 prepare 阶段调用同一解析器预校验，无效输入就地阻止、不产生修订（SA-15 边界内）。

### 5.3 canonical 编码与内容身份（PolicyCodec）

| 规则 | 内容 | 依据 |
| --- | --- | --- |
| 编码形态 | 确定性二进制：magic `IRDPOL1`＋字段按规范序（非输入序）；长度前缀；大端；无填充 | 跨进程一致（worker/main 同身份，NFR-COR-02）——evidence D-02 同判据 |
| 可选值 | presence 字节显式编码（缺失≠空值≠零） | NFR-COR-03 |
| 浮点 | IEEE754 双精度 8 字节**位模式**承载（round-trip 精确）；NaN/±Inf 在编码入口拒绝 | 位模式保证同值同字节；**严禁浮点近似相等作为身份键**（无传递性；POL-ID-5 用例钉住；evidence D-06 同源） |
| 单位 | 编码内为 SI 真值；RawPolicyInput 的输入单位在解析期归一（显示单位不入身份——POL-ID-3） | SA-12（换算唯一权威在 core） |
| 字段顺序无关性 | 编码按规范序重排（对象对按字典序、集合按稳定序）——**输入字段排序变化不改变语义身份**（POL-ID-2） | 身份＝语义闭包的函数 |
| 身份计算对象 | 仅语义闭包字段（§4.2）：{schemaVersion, collision, jointThresholds, applicability, numericContractAnchor}；排除 policyObject/origin/校验状态/诊断/兼容注记 | 显示与审计信息不影响工程语义（CON-05 精神） |
| 版本化 | schemaVersion 写入编码且参与身份——**编码升版＝全体策略身份变化**（破坏性，走设计变更评审；次版本追加可选字段除外） | 身份可演化且留痕 |
| 实现 | 纯函数；同输入同字节；线程安全（可重入）；无 I/O | NFR-COR-02 |
| 与 project 分工 | PolicyCodec 产出/消费**对象字节**（磁盘编址、事务、升级器归 project；字节对 project 不透明）；策略草稿导入（MDL-04 自碰撞配置等）在 modeling/io 侧装配为 RawPolicyInput 后进入本管线 | §2.1 N-1、core.md §6.3 三层分工 |

---

## 6. CollisionEvaluator 唯一实现（`CollisionEvaluator.hpp`）

### 6.1 输入模型：CollisionScene 与只读生命周期

**输入是运行时快照（编译产物），不是可变 WorkCell**：评估只消费不可变共享的编译产物，杜绝"评估期间场景被改"的一致性风险（PA-3 一切计算经快照）。

```cpp
enum class SceneObjectRole { RobotLink, Tool, Payload, EnvironmentObject, Workpiece };  // 建模语义词表
struct SceneObjectEntry {
    core::ObjectId objectId;          // 对象身份（输出用——NFR-COR-05）
    std::string localName;            // 显示辅助（经⑥端口语；不作为身份）
    SceneObjectRole role;
    bool hasCollisionGeometry;        // 场景事实（无几何→coverage 记录，不伪装已检——KIN-05）
};
struct CollisionScene {
    std::shared_ptr<const rw::models::WorkCell> workcell;   // runtime 编译产物（共享只读；含 R_world_base——MDL-22）
    core::ObjectId primaryDevice;                           // 主链设备对象（经 IPolicyNameContext 解析定位——禁止字符串猜名）
    std::vector<SceneObjectEntry> objects;                  // 参与碰撞域的对象清单（调用方从模型/快照装配）
    std::vector<std::pair<core::ObjectId, core::ObjectId>> adjacentLinkPairs; // 运动学相邻对（模型事实，供默认相邻过滤）
    core::ContentIdentity sceneContentIdentity;             // 编译产物内容身份（runtime 计算，调用方传入——policy 不重算）
};
```

- **谁装配**：请求方（域评估器/execution 准备段，L3/L4 可依赖 runtime）或 L5 装配适配器；policy 不读取模型对象、不构造 WorkCell（N-3/N-4 边界）。
- **只读生命周期保证**：`shared_ptr<const>` 共享所有权——会话存续期间编译产物不可变且不可被释放（内存安全由所有权保证）；"迟到调用拒绝"由 `IPolicyCallContext::alive()` 承担（运行/快照上下文结束时 execution 置失效；此后 `evaluate` 返回 `Failed`＋`POLICY-CLL-CONTEXT-EXPIRED`，绝不产生可入正式证据的输出）——两层防护（所有权＋上下文存活）共同覆盖"快照释放后继续评估"（POL-LATE-1）。
- **场景校验（会话构建期）**：objects 的 objectId 唯一；策略规则引用的对象 ⊆ objects（缺失→`POLICY-CLL-SCENE-INVALID`＋定位对象，会话构建失败）；primaryDevice 可经名称上下文解析到 workcell 内设备（不可解析→`POLICY-CLL-NAME-UNRESOLVED`）；sceneContentIdentity 非空。

### 6.2 查询与输出类型（`CollisionQuery.hpp`）

```cpp
enum class CollisionQueryKind { SingleState, PathSequence, SampleSet };
// SingleState＝单构型（IK 过滤、任务点构型检查）；
// PathSequence＝有序路径采样（TRJ-04 复检——样本由调用方按其细分协议生成；policy 不生成采样）；
// SampleSet＝无序/独立样本集（KIN-04 覆盖、KIN-09 工作空间、OPT-B 静态约束）

struct CollisionQuery {
    CollisionQueryKind kind;
    std::vector<rw::math::Q> configurations;     // SingleState=1 个；序列按调用方顺序保留（含路径连续性语义）
    std::optional<rw::kinematics::State> baseState; // 场景基准状态（附件/负载/工具姿态）；缺省=场景基准状态
    std::vector<double> pathParameters;          // PathSequence：各样本路径参数（复检证据用；与 configurations 等长）
    bool requestMinDistance = false;             // 是否计算最小距离（间距检查需要时）
    bool stopAtFirstFinding = false;             // 提前终止（筛选/淘汰场景；输出仍带 coverage）
    // 注意：本类型【不携带任何阈值/开关】——阈值唯一来源是会话绑定的 EngineeringPolicySet；
    //       评估模式（Preview/Quick/Verified）不是本类型字段——模式不得隐式改变工程策略（KIN-13/EVI-01）
};

enum class CollisionFindingKind { Collision, SafetyMarginViolation };
struct CollisionFinding {
    core::ObjectId objectA, objectB;             // 有序对（canonical 字典序 A<B）——NFR-COR-05 对象 ID 对
    std::string runtimeNameA, runtimeNameB;      // 辅助显示（编译产物整名；不作为身份——CON-06）
    std::size_t sampleIndex;                     // 采样位置（序列下标）
    std::optional<double> pathParameter;         // PathSequence 时必填（段内定位——TRJ-04）
    CollisionFindingKind kind;                   // Collision=相交/接触（后端二值语义）；MarginViolation=间距不足
    std::optional<double> measuredClearance;     // MarginViolation：实测最小距离（SI m）
    std::optional<double> penetrationDepth;      // Collision：后端可提供则给；不可提供=显式空（不伪造）
    PolicyRuleLevel level;                       // 命中规则的 Must/Should（无规则命中=域默认 Must）
};

struct AppliedFilterRecord {                     // 过滤可追溯性：本次评估实际生效的过滤关系全量输出
    core::ObjectId objectA, objectB;             // 或 Role 对展开（含 roleToken）
    std::string ruleOrigin;                      // "policy.excludedPairs[i].reason" / "default.adjacent-links"
};
struct PairCoverageRecord {                      // KIN-05 口径的覆盖事实（防"没检＝无碰撞"）
    std::uint64_t pairsInScope, pairsWithGeometry, pairsEvaluated, pairsExcludedByRule;
    std::vector<AppliedFilterRecord> excluded;   // 全量列出（不因首个短路）
};
struct SampleDistanceSummary { std::size_t sampleIndex; std::optional<double> minDistance; std::optional<core::ObjectId> nearestToA, nearestToB; };

enum class CollisionEvaluationStatus { Completed, Canceled, Failed };
enum class ScopeApplicability { Applicable, EmptyScope, CollisionDisabledByPolicy };
struct CollisionEvaluation {
    CollisionEvaluationStatus status;
    ScopeApplicability applicability;            // EmptyScope=作用域解析为空（≠"无碰撞"）；DisabledByPolicy=策略禁用被误调用
    core::ContentIdentity policyContentIdentity; // 判定依据绑定（供证据/追溯）
    core::ContentIdentity sceneIdentity, nameMapIdentity;
    std::vector<CollisionFinding> findings;      // 稳定排序（§6.4）；Canceled/Failed 时为已产生的非终态部分
    std::optional<std::vector<SampleDistanceSummary>> minDistances;
    PairCoverageRecord coverage;
    std::vector<AppliedFilterRecord> appliedFilters;
    std::vector<core::DiagnosticRecord> diagnostics;
    bool finalized;                              // 仅 Completed=true；Canceled/Failed 恒 false——
                                                // 非终态输出不得进入正式证据/可行集（TASK-02/CON-04 口径）
};
```

**单状态/路径/区域/工作空间的区别**（同一评估器、同一输出类型，差异经 kind 与元数据表达）：SingleState 无路径语义；PathSequence 输出携带 pathParameter、保留样本顺序（连续性信息＝调用方协议：相邻样本间距由 trajectory 保证——policy 不声称连续路径验证，离散采样与连续路径的证据区别由此保持：coverage 只承诺"已检样本"，段间未验证属调用方 DataInsufficient 口径——TRJ-04/R9）；SampleSet 输出按样本索引独立归档（KIN-04 分母统计素材）。

### 6.3 评估状态机："失败 / 取消 / 中断 / 检测到碰撞"四分

| 状态 | 触发 | findings | finalized | 后续处置（调用方） |
| --- | --- | --- | --- | --- |
| Completed（无发现） | 全部样本评估完成、无碰撞 | 空 | true | "该构型/路径未检出碰撞"——**仅**指已检样本与作用域（coverage 为凭） |
| Completed（有发现） | 检出碰撞/间距不足 | 非空（构型/路径级事实） | true | IK 解→硬过滤（KIN-02）；路径→淘汰＋重规划（TRJ-03）；**绝不**自动上升为任务不可行（C8） |
| Failed | 后端/策略异常：RobWork 异常、几何资源错误、检测器不可用、名称不可解析、上下文失效 | 已产生部分（非终态） | false | 碰撞证据缺失 → DataInsufficient（KIN-05：缺检测器≠无碰撞）；`POLICY-CLL-*` 诊断定位 |
| Canceled | `IPolicyCallContext::cancellationRequested()` 命中（样本间协作检查点） | 已产生部分（非终态） | false | 取消结果不入正式报告/可行集/正式缓存（TASK-02/CON-04）；正常取消不产生错误诊断（UX-03） |
| （Interrupted） | 进程/会话中断 | **无返回值**（进程边界） | — | execution 标记"已中断"（NFR-REL-03）；policy 不参与该状态的表达 |

**失败≠碰撞、碰撞≠不可行、取消≠失败**三组区分由类型与状态机强制：Failed/Canceled 输出恒 `finalized=false` 且 evidence 侧不得采信为 Satisfied（evidence.md §6.2——Invalid/Missing 口径）；`POLICY-CLL-DETECTOR-UNAVAILABLE`（KIN-05）为 Failed 的子诊断。RobWork 异常（`rw::common::Exception` 及其他）在评估实现内**必须捕获**并转为 Failed＋诊断（不吞、不崩、异常不跨进程边界——进程模型边界同 core §6.2/evidence D-15）。

### 6.4 确定性与稳定排序

- **会话不可变**：`createSession` 一次性解析作用域（策略规则×场景清单→具体 Frame 对几何对集）、构建检测器（内置后端实例与 ProximitySetup——由策略规则生成，规则经名称上下文以**完整名精确匹配**转为 RobWork 规则，不做模式拼接）、计算 `sessionIdentity = f(policyContentIdentity, sceneContentIdentity, nameMapIdentity, backendDescriptor)`。会话构造后只读、可跨线程共享。
- **evaluate 纯度**：不改会话状态；同 (session, query, 上下文存活) → 逐字段等价输出（NFR-COR-02）；无随机源（显式声明：碰撞评估无随机性——采样由调用方生成并冻结；无归约——多线程并行由调用方对独立查询分派，查询间无共享可变状态）。
- **稳定排序**：findings 按 (sampleIndex 升序, objectA/objectB 对字典序, kind) 排序；minDistances 按 sampleIndex；appliedFilters 按对象对字典序——三入口跨进程逐字节一致（NFR-COR-05/AT-19）。
- **无隐式阈值**：RobWork 默认碰撞容差/默认过滤不进入产品判定——检测器以策略阈值显式初始化；后端固有数值分辨率（内部容差模型）记录于 `CollisionBackendDescriptor.toleranceModel`（复现要素），但**工程阈值**唯一来自策略。

### 6.5 唯一实现的装配、共享与防旁路

```
                    ┌──────────────────────────────── 装配期（L5 / worker 宿主） ────────────────────────────────┐
                    │  makeRobWorkCollisionEvaluator(BackendConfig{builtin ProximityStrategyRW})                │
                    │  → 唯一 ICollisionEvaluator 实现（RobWorkCollisionEvaluator，构造入口唯一）                │
                    │  PolicyProvider(bytesSource, nameContext 适配器, evaluator)  ── 注册为④端口唯一实现        │
                    └───────────────────────────────────────────────────────────────────────────────────────────┘
 kinematics 批量IK ──┐                                                                                       │
 trajectory 复检  ───┤   ┌──► IPolicyProvider::resolvePolicy(oid,cv) ──► EngineeringPolicySet(内容身份 P)      │
 optimization 静态 ──┼──►│                                                                            │
 selection(按需)  ───┤   └──► IPolicyProvider::collisionEvaluator() ──► 同一实现实例 ──► createSession        │
 KIN-09 采样(R2)  ──┘                                        （主进程 1 实例；每 worker 各 1 实例，            │
                                                               同 P＋同场景＋同名称映射 ⇒ 等价会话/等价输出）    │
 旁路禁止（构建门禁＋评审）：
   R-POL-1 业务单元不得自行实现碰撞/间距算法（ARC-05；NFR-MNT-07 静态检查）
   R-POL-2 业务单元不得直接链接 sdurw_proximity / 包含 rw/proximity（例外：policy；测试经替身——§3.4 红线扩展建议）
   R-POL-3 不得将 RobWork 默认碰撞设置（文件内嵌 CollisionSetup、策略默认容差、默认过滤器）当产品策略
           ——检测器仅以策略生成的 setup 显式初始化（POL-EVAL-8 用例钉住）
   R-POL-4 不得经字符串名称自行定位碰撞对象——对象定位仅经 ObjectId→名称上下文（R-4；API 不收名称字符串）
   R-POL-5 评估 API 无阈值/模式参数——结构上不可覆盖策略（KIN-13；POL-EVAL-8）
```

### 6.6 调用时序图（以 trajectory 复检为例；kinematics/optimization 同构）

```
 调用方(trajectory评估器,worker内)   IPolicyProvider(④)      RobWorkCollisionEvaluator        IPolicyCallContext(execution)
   │ resolvePolicy(oid,cv) ─────────►│
   │                                 │ bytesSource→字节→解析管线→发布(P)   [记忆化缓存:同(oid,cv)直接命中]
   │ ◄─── EngineeringPolicySet ──────│
   │ collisionEvaluator() ──────────►│────────► (返回同一实现实例)
   │ createSession(P, scene, nameCtx) ──────────────►│
   │        （作用域解析：规则×场景清单→Frame 几何对集；检测器构建：策略 setup＋内置后端；sessionIdentity 计算）
   │ ◄─── shared_ptr<CollisionEvaluationSession> ────│
   │ evaluate(query{PathSequence,细分样本,pathParams}, ctx) ─►│
   │        循环：setState(设备Q,baseState克隆) → inCollision / distance           │
   │              每样本边界：ctx.cancellationRequested()? ────────────────────────►│
   │        异常捕获(RobWork)→Failed+诊断；命中→（stopAtFirstFinding? 提前终止）    │
   │ ◄─── CollisionEvaluation{findings[稳定排序], coverage, appliedFilters, finalized} │
   │ （调用方组装证据：TRJ-04 复检证据/段内碰撞判定/预算占用——policy 到此为止，
   │    工程结论汇总归 evidence——§8.3）
```

---

## 7. 碰撞作用域、过滤和数值阈值

### 7.1 作用域矩阵（可执行规则；会话构建时展开为具体对象对集）

| 评估对象对 | 所属域 | 必须检测 | 可过滤 | 不适用条件 | 输出资格 |
| --- | --- | --- | --- | --- | --- |
| 机器人**非相邻**连杆 | Self | 域启用即默认必检 | 经 excludedPairs 逐对（理由必填、进身份与诊断） | 域禁用；无碰撞几何（coverage 记录 pairsWithGeometry） | Collision / MarginViolation 发现 |
| 机器人**相邻**连杆 | Self | mandatoryPairs 声明时必检（不可过滤） | 默认过滤（excludeAdjacentLinksByDefault，消费模型邻接事实） | 同上 | 必检时同上 |
| 工具/负载 vs **非相邻**连杆 | Tool | 域启用即默认必检 | 同 Self | 工具域禁用/对象无几何 | 同上 |
| 工具 vs 其安装侧**相邻**连杆 | Tool | mandatoryPairs 声明时必检 | 默认过滤（安装连接＝邻接事实延伸——adjacentLinkPairs 由建模侧含工具安装对） | 同上 | 同上 |
| 机器人侧（连杆/工具/负载） vs 环境对象 | Environment | 域启用即默认必检 | 逐对排除（理由必填——如工艺让位槽） | 环境域禁用/对象无几何/工况未启用该对象（场景清单决定） | 同上 |
| 工具/负载 vs **工件** | Tool（工件角色 Workpiece 归环境侧对象） | 域启用即默认必检 | 仅逐对显式排除（如夹持接触对——理由必填） | 工件不在场景清单/无几何 | 同上 |
| 环境 vs 环境 | Scene | **默认不检**（静态场景无相对运动）；显式 mandatoryPairs 可启用 | — | 域禁用或双方静态（默认） | 显式必检时同上 |
| 空对象集合 / 空作用域 | — | — | — | 作用域解析为空（如场景无环境对象且域全禁） | `EmptyScope`（**非**"无碰撞"结论字段——KIN-05 口径由 coverage 表达） |

矩阵规则与 MDL-15（环境对象显式引用参与碰撞）、MDL-04（自碰撞配置）、KIN-02/05、TRJ-04③ 对齐；工况差异（负载/工件存在性）经**场景清单**表达（不同工况→不同 scene objects），不经阈值覆盖（P-POL-7）。

### 7.2 过滤规则与可追溯性（结构保证"过滤不得隐藏实际必检碰撞"）

```
策略规则层（对象 ID/角色/组；理由必填；进入策略内容身份）
   │  会话构建：与场景清单求交 → 具体对象对集
   ▼
┌────────────────────────── 冲突检查（解析期拒绝，§5.2） ──────────────────────────┐
│ excludedPairs 展开集 ∩ mandatoryPairs 展开集 = ∅ 必须成立                        │
│ （Role/Group 展开为对象集后比对——"排除工具类"不得暗中吞掉"必检工具—工件对"）      │
│ 违反 → POLICY-RULE-CONFLICT → 策略不发布（结构上不存在"可隐藏必检的合法策略"）    │
└──────────────────────────────────────────────────────────────────────────────────┘
   │  运行期（纵深防御）
   ▼
┌────────────────────────── 会话评估顺序 ─────────────────────────────────────────┐
│ ① 必检对集先评估（mandatoryPairs ＋ 域默认必检且未被合法排除的对）                │
│ ② 排除对跳过并记录 AppliedFilterRecord{对象对, ruleOrigin=规则引用+理由}          │
│ ③ appliedFilters 全量随 CollisionEvaluation 输出（进证据与诊断——过滤可追溯）      │
│ ④ 空集/无几何：coverage 显式计数，不输出"无碰撞"性结论字段                        │
└──────────────────────────────────────────────────────────────────────────────────┘
```

- 组（Group）：策略内定义的显式对象集合（扁平——组只含对象 ID，禁止嵌套组，循环在 schema 层不可能，解析器仍做图遍历校验作纵深防御——§5.2）；组名与成员进入策略身份。
- 所有过滤关系（默认相邻过滤与显式排除对）**双重入账**：策略内容身份（规则本身）＋每次评估的 appliedFilters（实际生效）——满足"所有过滤关系必须进入策略身份和评估诊断"。

### 7.3 硬约束、策略阈值、数值容差三分（禁止混同）

| 类别 | 定义 | 归属 | 例子 | 修改通道 |
| --- | --- | --- | --- | --- |
| 硬约束 | 需求定义的不可配置结构合法性 | 需求（执行归 modeling/project 处理器） | qmin＜qmax 区间有效性；物性断言①～③；continuous 工程工作范围有限性 | 需求变更 |
| 策略阈值 | EngineeringPolicySet 持有的可配置工程判定界限（附录 D"工程策略默认"类别＋显式值） | **policy（本文）** | 行程上限 4π（默认）/安全间距/近限位比/条件数警告 | 策略编辑命令→新内容身份→新修订（UX-08 入口） |
| 数值容差 | 比较与校验的数值分辨率（附录 D"固定/分析配置默认"类别；C4 公式/C7 ε_abs） | core（公式与转写）/kinematics（IK 容差）/testkit（对照） | closeWithin 的 ε_rel·\|ref\|+ε_abs；角度 ε_abs=1×10⁻¹² rad | 需求变更（附录 D） |

规则：策略阈值的**判定**不使用容差放宽/收紧（阈值比较是单向不等式，非等值判定——§7.4）；数值容差不进入策略内容身份（属 core/各域）；评估器不得覆盖策略阈值（API 结构保证——R-POL-5）；不同评估模式不得隐式改变策略（无模式输入——§6.2）。

### 7.4 距离语义与边界值决策表（d＝最小距离〔SI m〕，m＝safetyClearance）

| 情形 | 判定 | 输出 | 依据/说明 |
| --- | --- | --- | --- |
| d ＞ m | 清晰 | 无发现；minDistance 报告值保留 | — |
| d ＝ m | **清晰（边界含于安全侧）** | 无发现 | "安全间距"语义＝d ≥ m 即满足（TRJ-04③ 措辞口径）；边界归属为设计冻结（D-08），非阈值数值变更 |
| 0 ≤ d ＜ m | 间距不足（**非碰撞**） | kind=MarginViolation，measuredClearance=d | 间距不足与碰撞是两类发现；Must/Should 由规则级别 |
| 后端报告相交（含接触，其二值语义） | 碰撞 | kind=Collision；penetrationDepth 可得则附，否则显式空 | 接触（零距离）计入碰撞（后端语义）；后端内部数值分辨率经 CollisionBackendDescriptor.toleranceModel 登记，**非**工程阈值 |
| d 为 NaN/±Inf | 该对评估失败 | 诊断（`POLICY-CLL-EVALUATION-FAILED`，非有限实测值以原文保留） | NFR-COR-03：不静默转 0/通过 |
| m ＝ 0 | 仅碰撞检测（间距检查退化为空） | 同上各行（m=0 时 d ≥ 0 恒清晰） | §4.3 |
| 行程上限判定：T＝\|qmax−qmin\|，L＝finiteRotationTravelLimit | T ＞ L → 超限（比较型：实际 T/阈值 L/rad）；T ＝ L → 不超限 | JointLimitFinding（供 ConfirmableFinding 构造——SA-15） | 边界含于合规侧（同上 D-08 一致）；continuous 关节免除（MDL-06④），其工程工作范围有限性另行检查 |
| 近限位比 r（距限位余量/区间半宽） | r ＜ threshold → NearLimit 警告（Should 级默认） | JointLimitFinding | 阈值未设置（nullopt）→ 该检查 NotApplicable（显式标记，不伪造） |
| 限位违例：q ∉ [qmin,qmax] | 限位违例（硬过滤素材） | JointLimitFinding（LimitViolated） | 过滤动作归 kinematics（KIN-02）；qmin≥qmax 区间非法→诊断（硬断言执行归 modeling/project——N-4） |

比较方向与单位冻结：全部阈值比较以 SI（m/rad/无量纲）执行；输入单位在解析期归一（§5.3）；比较一律对**标量**（无向量求和比较——C4 逐元素精神的标量场景）；身份与名称比较精确等值（附录 D 第 12 项）。

### 7.5 数值异常处理规则

| 异常 | 解析期（§5.2） | 评估期 |
| --- | --- | --- |
| NaN/±Inf 阈值 | 拒绝发布＋原文保留 | （阈值不可能非有限——发布保证） |
| NaN/±Inf 实测值（距离/深度） | — | 该对评估 Failed＋诊断（不伪造 0，不入 findings） |
| 溢出（换算/计算） | 拒绝（core convert 抛错转发） | Failed＋诊断 |
| 后端返回非有限 | — | 同实测值非有限处理 |
| 几何缺失（策略作用域内对象无碰撞几何） | — | coverage.pairsWithGeometry 计数＋`POLICY-CLL-GEOMETRY-MISSING` 告知性诊断——**不**输出"无碰撞"结论字段（KIN-05：调用方判数据不足） |
| 名称不可解析（场景 Frame↔对象 ID 断链） | — | 会话构建失败（`POLICY-CLL-NAME-UNRESOLVED`）——不猜测对象（ARC-04） |
| 上下文失效（快照/运行已结束） | — | Failed＋`POLICY-CLL-CONTEXT-EXPIRED`（迟到调用拒绝——POL-LATE-1） |

---

## 8. 策略兼容性与证据协作

### 8.1 版本要素与兼容判定（`Compatibility.hpp`）

策略会话与结果的完整版本要素（进入 `sessionIdentity`、evidence 复现块与兼容判定）：

| 要素 | 载体 | 取值来源 |
| --- | --- | --- |
| 策略 schema 版本 | EngineeringPolicySet.schemaVersion | policy（当前 1） |
| 策略内容身份 | contentIdentity | policy（§5.3） |
| 模型编译器契约版本 | sceneContentIdentity 的组成（runtime 侧）＋ evidence ReproductionBlock.compilerContractVersion | runtime（P-POL-1 交接） |
| RobWork 版本 | CollisionBackendDescriptor{backendId="rw.proximity.builtin-rw", backendVersion=RobWork 基线版本, toleranceModel} | policy（构建期冻结；NFR-DEP-05 基线登记后锁定——P-POL-5） |
| 碰撞算法版本 | 同上（后端标识＋容差模型） | 同上 |
| 数值契约版本 | numericContractAnchor（如 "appendixD@v1.16"） | policy（引用附录 D 基线，不重定义） |
| 评估模式 | PolicyApplicability.modes（适用性核对用）；**模式不是评估输入** | policy/请求方 |
| 随机性与并行配置 | 碰撞评估**无随机源**（§6.4 显式声明）；查询间无共享状态、无归约——并行配置不影响输出 | policy（设计声明；调用方并行属 execution） |

```cpp
struct PolicyConsumerRequirements {          // 消费方（域评估器/缓存判定）的兼容要求
    std::uint32_t minSchemaVersion = 1, maxSchemaVersion = 1;
    bool requiresCollision = false;          // 消费方需要碰撞能力（策略 enabled=false → 不兼容）
    std::optional<CollisionBackendDescriptor> requiresBackend;   // 缺省=任意已注册后端
    std::optional<std::string> requiresNumericContract;          // 附录 D 基线锚核对
    std::optional<core::EvaluationMode> mode;                    // 目标模式适用性核对
};
struct PolicyCompatibility {
    enum Verdict { Compatible, Incompatible } verdict;
    std::vector<PolicyIncompatibilityReason> reasons;   // schema-future/schema-obsolete/collision-required-disabled/
                                                        // backend-mismatch/numeric-contract-mismatch/
                                                        // mode-not-applicable/policy-invalid
    std::vector<core::DiagnosticRecord> diagnostics;    // POLICY-VERSION-INCOMPATIBLE 等（比较型：实际/期望/单位或版本串）
};
PolicyCompatibility checkPolicyCompatibility(const EngineeringPolicySet&, const PolicyConsumerRequirements&);  // 纯函数
class IPolicyCompatibilityChecker {         // 注入形态（无状态；自由函数即可满足，接口供宿主装配）
public: virtual ~IPolicyCompatibilityChecker() = default;
        virtual PolicyCompatibility check(const EngineeringPolicySet&, const PolicyConsumerRequirements&) const = 0; };
```

### 8.2 变化分类：拒绝缓存复用 / 仅影响诊断

| 变化 | 对缓存/复用 | 对诊断/追溯 | 说明 |
| --- | --- | --- | --- |
| 策略内容身份变化（任何语义字段） | **拒绝**：Policy 条目变化→sliceId 变化→不命中（evidence judgeCacheHit 机制）；历史结果转 Superseded(PolicyChanged) | 失效原因清单含 PolicyChanged＋具体字段差异（经 RawPolicyInput 对比，调用方可得） | CON-05/06 |
| schema 主版本变化 | **拒绝**（schema-future/obsolete）＋升级指引诊断 | POLICY-SCHEMA-VERSION-* 留痕 | PM-06 精神 |
| schema 次版本追加（可选字段） | 兼容（新字段未设置时语义不变→内容身份不变；设置后→新身份自然拒绝） | 告知性诊断 | §4.6 版本策略 |
| 碰撞后端版本变化 | **拒绝**（backend-mismatch；sceneIdentity/sessionIdentity 变化） | 复现块差异留痕 | NFR-DEP-05/CON-04 |
| 数值契约锚变化（附录 D 修订） | **拒绝**（numeric-contract-mismatch） | 需求变更留痕 | 附录 D 修订走需求变更 |
| origin/校验诊断/兼容注记变化（语义不变） | **不影响**（不入内容身份——命中保持） | 审计信息更新 | §5.3 |
| 显示设置变化（显示单位/高亮/渲染分组） | **不影响**（不存在于策略） | 无 | UX-08/AT-27 |
| HEAD 前进（策略对象版本未变） | **不自动失效**——内容身份未变，切片不变，结果 Current | 无失效原因 | CON-05：失效按切片内容，不按修订号（S7 电机成本正例同源） |
| Quick 结果 vs Verified 请求 | **拒绝**（evidence judgeCacheHit：mode-mismatch→Incompatible；policy 侧无模式升降级通道——R-POL-5） | Unverified 证据状态（evidence 词表） | EVI-01 表 1/OPT-06；Quick 不得伪装 Verified |

"策略版本不兼容时应返回什么诊断"：`PolicyCompatibility{Incompatible, reasons[], diagnostics[]}`——不抛异常（判定是查询非违约）；diagnostics 含 `POLICY-VERSION-INCOMPATIBLE`（比较型：实际 schemaVersion/后端/锚 vs 期望，ERR-01 三要素）＋建议动作（升级策略对象或调整消费方要求）。历史结果保留：不兼容只影响**新复用**，历史 envelope 与其证据按原快照永久保留（CON-02——Superseded 不改写 payload）。

### 8.3 与 evidence 的分工（policy 提供 / evidence 负责）

| policy 提供（本文） | evidence 负责（evidence.md，本文不越界） |
| --- | --- |
| 已解析策略对象与内容身份（④端口） | 证据 Profile 与 RequiredEvidenceProfile 注册/消费（表 4 承载） |
| 碰撞评估结果（CollisionEvaluation：状态/findings/coverage/appliedFilters） | 通用证据门禁（快照身份/工况覆盖/模式标识——②级） |
| 碰撞证据明细素材（对象 ID 对＋判定＋采样位置＋过滤记录——构型/路径级） | 必需证据门禁与缺失全量列出（④级） |
| 必经状态碰撞证明的**素材字段**（§8.4） | 任务级不可行判定（③级：三类证明的字段级校验与采信） |
| 适用范围（applicability）与不适用状态（EmptyScope/DisabledByPolicy/nullopt 阈值） | 多工况覆盖判定（EVI-02）与包络合并呈现（DYN-07） |
| 失败/取消/非终态输出（finalized=false） | 结果包络（ResultEnvelope 构造校验 SA-13）与正式通过判定（五条件） |
| 兼容判定纯函数（§8.1） | 当前性计算（ResultCurrentness）与缓存命中判定（judgeCacheHit 消费切片身份） |

**policy 不得自行发布正式工程结论**：本文全部输出类型不含 Feasible/EngineeringInfeasible 语义字段；`CollisionEvaluation` 只陈述"检出/未检出/未能评估"及其作用域（构型/路径级），任务级判定由 evidence 按 §8.1 表 2 五级汇总（C8 作用域规则的对端执行者）。

### 8.4 CollisionFinding 与快照/策略/运行身份的绑定（必经状态证明素材）

`CollisionFinding` 自身不携带快照/运行身份（保持纯事实记录）；绑定由**调用方在组装证据时**完成（evidence 契约）：

```
evidence.DeterministicInfeasibilityProof.MandatoryStateCollision.collisionPairs[i]
   ⇦ 映射自 CollisionFinding{objectA/objectB（对象 ID 对）, kind, sampleIndex/pathParameter}
proof.snapshotId/sliceId ⇦ 调用方从 AnalysisSnapshot/InputSlice 取（其中 policyRef=本策略内容身份——CON-06）
proof.producer/producerContractVersion ⇦ 调用方（域评估器）注册身份
运行绑定：ResultEnvelope.task（五元组，execution 分配）——policy 不参与
```

四类状态的区分（policy 侧语义供给）：**证据缺失**（coverage 几何缺失/检测器不可用→Failed＋诊断——KIN-05）；**评估失败**（同 Failed，永不解读为碰撞或无碰撞）；**碰撞发现**（Completed＋findings——构型/路径级事实）；**确定性不可行证明**（不存在于 policy 输出——必经状态的"必经性"认定〔任务强制要求且不可选择——如任务点构型/起点/终点〕属需求/任务语义，由域评估器声明 mandatoryState 后以 policy 碰撞发现佐证，evidence validateProof 校验采信）。**不可行替代证据的适用边界**：仅"因不可行而无法生成的成功产物"可替代（C6）；通用门禁类（快照/工况/模式身份）不豁免——policy 输出的绑定字段（策略身份/场景身份/名称映射身份）即为此门禁供给素材。

### 8.5 评估状态 × 碰撞状态 × 证据状态正交关系表

| 评估状态（policy） | 碰撞状态（findings/coverage） | 证据状态（evidence 消费后） | 合法性说明 |
| --- | --- | --- | --- |
| Completed | 无发现、coverage 完备（pairsWithGeometry==pairsInScope−excluded） | Satisfied（"该构型/路径未检出碰撞"——限已检样本） | 正常通过路径 |
| Completed | 有发现（构型级） | Satisfied（碰撞证据存在）→ 解被过滤/路径淘汰（KIN-02/TRJ-03）——**非**任务不可行 | C8 作用域 |
| Completed | 有发现（必经状态构型，调用方声明 mandatoryState） | ③级采信→EngineeringInfeasible（evidence 判定） | 证明字段齐备时 |
| Completed | coverage 缺几何（pairsWithGeometry＜应检数） | Missing/Invalid→DataInsufficient | KIN-05：不得视为无碰撞 |
| Completed | EmptyScope（作用域解析为空） | NotApplicable 或 DataInsufficient（按证据项适用条件，evidence 判） | 非结论性输出 |
| Failed | 无结论（部分 findings 非终态） | Missing→DataInsufficient（附 POLICY-CLL-* 诊断） | 失败≠碰撞≠无碰撞 |
| Canceled | 部分（finalized=false） | 不得进入正式证据/可行集/正式缓存（TASK-02/CON-04） | 取消≠失败（UX-03） |
| （Interrupted） | 无返回值 | （execution 标"已中断"，NFR-REL-03） | 进程边界外 |

三轴互不推导：评估状态不决定证据状态（evidence 按门禁判定）；碰撞发现不决定工程判定（构型级）；证据状态不改变评估输出（不可变）。

### 8.6 策略变化导致输入失效的矩阵（与 evidence.md §5.3"工程策略"行对齐展开）

| 策略变化 | 运动学（碰撞证据启用时） | 轨迹复检（TRJ-04） | 优化 OPT-B 静态约束 | 建模行程上限校验/放行（MDL-06④） | 选型 | 缓存（声明 Policy 条目者） |
| --- | --- | --- | --- | --- | --- | --- |
| 碰撞域开关/规则/间距/过滤变化 | ✓（Policy 条目身份变→sliceId 变→Superseded） | ✓（安全间距/过滤消费） | ✓（碰撞静态子集） | ✗（行程阈值不变时） | ◐（阶段 C 后按实际消费声明） | 不命中 |
| 行程上限阈值变化 | ✗ | ✗ | ◐（关节范围变量涉及限位约束时按域声明） | ✓（新校验阈值；project 侧确认绑定 policyContentId 失效——历史放行不自动迁移） | ✗ | 不命中（消费该阈值者） |
| 近限位比/条件数阈值变化 | ✓（消费该警告的评估声明 Policy 条目时） | ✐（OPT-D 前/按域声明） | ◐ | ✗ | ◐（SEL-05 裁决后） | 不命中（同上） |
| applicability/模式适用变化 | ✓（属语义闭包——身份变） | ✓ | ✓ | ✓ | ✓ | 不命中 |
| 显示设置变化（不属于策略字段） | ✗ | ✗ | ✗ | ✗ | ✗ | 命中（不变） |
| HEAD 前进而策略对象版本未变 | ✗（内容身份未变） | ✗ | ✗ | ✗ | ✗ | 命中（CON-05/S7） |

（✓＝失效〔Superseded＋重算提示〕；✗＝不失效；◐＝条件失效〔按消费方依赖声明〕；✐＝阶段语义——阶段 B 优化切片不含动力学/选型条目。）

### 8.7 策略版本—快照—结果绑定关系图

```
EngineeringPolicySet(内容身份 P, schema vN, 后端 B, 数值锚 A)
        │ ④端口 resolvePolicy（解析管线，记忆化）
        ▼
AnalysisSnapshot.policyRef = P（CON-06：非空校验；与 RuntimeNameMap 内容身份 M 并存）
        │ evidence 依赖分析（域 descriptor 声明 Policy 条目）
        ▼
InputSlice[Policy→P] ⊆ sliceId（缓存键与失效判据，CON-05）
        │ 评估运行（execution 派发；worker 内同一实现同一会话身份 f(P,场景,M,B)）
        ▼
CollisionEvaluation{policyContentIdentity=P, sceneIdentity, nameMapIdentity=M}
        │ 调用方组装证据 → ResultEnvelope{snapshotId, sliceId, evidence[碰撞明细].binding}
        ▼
归档 results/<run-id>/（绑定原修订；CON-02 payload 不可变）
        │ 策略编辑命令 → 新策略对象版本 → 新内容身份 P'
        ▼
computeCurrentness：重建切片[Policy→P'] ≠ 原 sliceId → Superseded{PolicyChanged＋字段差异}
（历史 envelope/证据按原快照保留；project 侧确认绑定 policyContentId=P 的放行记录失效）
```

---

## 9. 公共接口详细设计

通用契约（适用本节全部接口）：实现为纯计算——不触 I/O（注入源除外）、不写项目、不产生修订、不自我注册；线程安全＝并发只读可重入（个别注明除外）；确定性＝同输入同输出（§6.4）；错误＝`PolicyError`（稳定 token，§3.1）或非抛出 `try*`/optional 双轨。

### 9.1 IPolicyProvider（④策略端口；ARCH §7.2④）

```cpp
struct PolicyResolutionRequest { core::ObjectId policyObject;
                                 std::optional<core::ContentVersion> expectedVersion; };
struct PolicyResolution { std::optional<EngineeringPolicySet> policy;   // 无效/缺失时空＋诊断
                          std::vector<core::DiagnosticRecord> diagnostics; };

class IPolicyProvider {
public:
    virtual ~IPolicyProvider() = default;
    virtual PolicyResolution resolvePolicy(const PolicyResolutionRequest&) const = 0;
    virtual ICollisionEvaluator& collisionEvaluator() const = 0;        // 共享唯一实现实例
    virtual CollisionBackendDescriptor collisionBackend() const = 0;    // 复现要素供给
};
```

| 项 | 契约 |
| --- | --- |
| 前置条件 | 实例由 L5/worker 宿主装配注入（bytesSource＋nameContext 适配器就绪）；`resolvePolicy` 的 expectedVersion（若给）须为修订闭包内版本 |
| 后置条件 | 同 (policyObject, ContentVersion) 重复调用返回逐字段相等策略（记忆化；身份必然一致——POL-ID-1）；`collisionEvaluator()` 每次返回同一实例引用 |
| 错误类型 | 对象缺失/版本不符→空 policy＋`POLICY-SCHEMA-*`/存储侧诊断转发（不抛）；实现内部异常→PolicyError |
| 线程约束 | 全部方法并发只读安全（缓存内部同步） |
| 确定性 | 是（解析纯函数＋记忆化） |
| 副作用 | 无外部副作用；内部缓存 |
| 生命周期/所有权 | 端口实例进程级单例（主进程 1、每 worker 1）；消费者持引用、不拥有；装配期注册、运行期不变 |
| 调用示例（合法） | `auto res = provider.resolvePolicy({policyOid, cv}); if (res.policy) { … }`（域插件/处理器/execution 准备段） |
| 非法调用 | 以 RawPolicyInput 冒充已解析策略传入快照组装（类型层不可表达）；运行期替换端口实例（无此 API） |

### 9.2 IPolicyValidator（策略校验）

```cpp
class IPolicyValidator {
public:
    virtual ~IPolicyValidator() = default;
    // 只校验不发布：返回全量诊断（不短路）；合法 ⇔ 诊断中无 Error 级条目。
    // 编辑命令 prepare 预校验（SA-15 边界）与导入装配校验共用本入口。
    virtual std::vector<core::DiagnosticRecord>
        validate(const RawPolicyInput&, const IPolicyValidationContext&) const = 0;
};
```

前置：RawPolicyInput 已通过 codec 解码（字节损坏在解码期报错）；上下文适配修订闭包。后置：诊断**全量**返回（不短路）；无发布副作用（编辑命令 prepare 阶段预校验用——SA-15 边界）。错误/确定性/线程/副作用：同 §5.1 纯函数契约。生命周期：无状态，随 provider 共享。合法调用：编辑命令预校验、导入装配后校验。非法调用：以 validate 代替 resolvePolicy 取得策略对象（其返回无 policy 值——类型层防误用）。

### 9.3 ICollisionEvaluator（唯一实现入口）

```cpp
class ICollisionEvaluator {
public:
    virtual ~ICollisionEvaluator() = default;
    virtual CollisionBackendDescriptor backend() const = 0;
    virtual std::shared_ptr<const CollisionEvaluationSession>
        createSession(const EngineeringPolicySet& policy, const CollisionScene& scene,
                      const IPolicyNameContext& names) const = 0;
    // 契约：会话不可变；sessionIdentity=f(policy,scene,names,backend)；
    //       同参数重复构建→等价会话（ POL-SHARE-1 的一致性基础）
};

class CollisionEvaluationSession {               // RobWorkCollisionEvaluator 产出；跨线程共享
public:
    const EngineeringPolicySet& policy() const noexcept;
    core::ContentIdentity sessionIdentity() const noexcept;
    CollisionEvaluation evaluate(const CollisionQuery& q, const IPolicyCallContext& ctx) const;
    // 契约：const——并发只读可重入；长序列在样本边界查询 ctx.cancellationRequested()；
    //       不抛 RobWork 异常（内部捕获转 Failed）；PolicyError 仅在调用方违约（如 kind 与样本数不符）
};

std::unique_ptr<ICollisionEvaluator> makeRobWorkCollisionEvaluator(const BackendConfig& = {});
// 唯一实现的唯一构造入口（默认 BackendConfig=内置 ProximityStrategyRW；NFR-DEP-05 冻结后锁定）
```

| 项 | 契约 |
| --- | --- |
| 前置条件 | policy 为已发布（Valid）对象；scene 校验通过（§6.1）；names 与 scene 同源（同一编译产物）；ctx 由宿主注入且存活 |
| 后置条件 | 输出 finalized 语义正确（§6.3）；findings 稳定排序；coverage/appliedFilters 全量；策略身份/场景身份/名称映射身份回填输出 |
| 错误类型 | 调用方违约（样本数/kind 不符、场景校验失败）→PolicyError；评估内部异常→status=Failed＋诊断（不抛出） |
| 线程约束 | 会话并发只读安全（ConcurrentReadOnly）；evaluate 无共享可变状态 |
| 确定性 | 是（§6.4；无随机源；无归约） |
| 副作用 | 无（不改场景/策略；内部检测器缓存随会话只读） |
| 生命周期/所有权 | evaluator 进程级共享；session 由调用方 shared_ptr 持有（可跨任务复用同会话）；会话保活场景（shared_ptr const WorkCell） |
| 调用示例（合法） | IK 过滤：`session.evaluate({SingleState, {q}}, ctx)`；复检：`{PathSequence, 样本, pathParams}`＋requestMinDistance |
| 非法调用 | query 携带阈值/模式（类型层不存在）；以字符串名定位对象（API 不收）；对 Failed/Canceled 输出按正式证据采信（finalized=false——消费方契约） |

### 9.4 IJointLimitEvaluator（关节限位与行程上限契约）

```cpp
struct JointLimitSpec {
    core::ObjectId jointObject;  std::string localName;
    std::optional<double> qMin, qMax;          // SI rad；有限限位（二者须同时有/无）
    bool isContinuous;                          // 类型保留（MDL-12）
    std::optional<std::pair<double,double>> engineeringRange;  // continuous 必填有限区间（工程工作范围）
};
struct JointLimitFinding {
    core::ObjectId jointObject;  std::string localName, runtimeName;
    enum class Kind { IntervalInvalid, EngineeringRangeInvalid, TravelLimitExceeded,
                      NearLimit, LimitViolated } kind;
    double actualValue, thresholdValue;         // 比较型三要素数据（UX-03/ERR-01；单位随 kind 固定 rad/无量纲）
    PolicyRuleLevel level;
};
struct JointLimitEvaluation { CollisionEvaluationStatus status;            // 复用状态词表（Completed/Failed）
                              std::vector<JointLimitFinding> findings;    // 稳定排序（对象字典序×kind）
                              std::vector<JointMarginRecord> margins;     // 逐构型逐关节裕量（KIN-01 关节裕量素材）
                              std::vector<core::DiagnosticRecord> diagnostics; bool finalized; };
class IJointLimitEvaluator {
public:
    virtual ~IJointLimitEvaluator() = default;
    virtual JointLimitEvaluation evaluate(const JointLimitQuery& q, const EngineeringPolicySet& policy,
                                          const IPolicyNameContext& names) const = 0;
};
```

边界：区间有效性（qmin＜qmax）与工程工作范围有限性为**硬断言**（执行归 modeling/project 处理器，MDL-06/12）；本评估器将其检出为 `IntervalInvalid`/`EngineeringRangeInvalid` **诊断级**发现并跳过该关节（不伪造裕量），供处理器就地阻断。行程上限超限→`TravelLimitExceeded`（比较型：实际行程/阈值/rad）——project 处理器据此构造 `ConfirmableFinding`（SA-15；policy 不执行放行）。`NearLimit` 按 §7.4 边界语义；阈值 nullopt→该检查输出显式 NotApplicable 标记（不伪造数值）。合法调用：处理器 prepare（AT-01）、kinematics 裕量计算（KIN-01）。非法调用：以本评估器结果直接放行/阻止命令（决策归 project 处理器）。

### 9.5 IPolicyCompatibilityChecker（兼容判定）

见 §8.1 签名。前置：policy 为已发布对象。后置：判定纯函数结果（Compatible/Incompatible＋reasons＋诊断全量）。错误：不抛（判定是查询）。线程/确定性/副作用：纯函数、可重入、无副作用。生命周期：无状态。合法调用：execution 缓存接纳前、worker 握手核对、域评估器预检。非法调用：以 Incompatible 结果删除历史结果（历史保留——CON-02；仅拒绝**复用**）。

### 9.6 IPolicyDiagnostics（诊断构造与码表）

```cpp
// 建议码表（码值分配与文案权威＝diagnostics StableCodeRegistry——NFR-MNT-03；本文仅建议）：
//   POLICY-SCHEMA-UNKNOWN-FIELD / -VERSION-FUTURE / -VERSION-UNKNOWN
//   POLICY-THRESHOLD-NON-FINITE / -NON-POSITIVE / -OUT-OF-RANGE
//   POLICY-UNIT-MISMATCH / POLICY-RULE-DUPLICATE / -CONFLICT / -CYCLE
//   POLICY-SCOPE-OBJECT-MISSING / POLICY-APPLICABILITY-INVALID
//   POLICY-VERSION-INCOMPATIBLE / POLICY-CONTENT-IDENTITY-MISMATCH
//   POLICY-CLL-DETECTOR-UNAVAILABLE（KIN-05）/ -SCENE-INVALID / -NAME-UNRESOLVED /
//     -CONTEXT-EXPIRED / -EVALUATION-FAILED / -GEOMETRY-MISSING
//   POLICY-JNT-TABLE-INVALID / -ENGINEERING-RANGE-INVALID / POLICY-INFO-DEFAULT-APPLIED
class IPolicyDiagnostics {
public:
    virtual ~IPolicyDiagnostics() = default;
    virtual std::vector<std::string> registeredCodes() const = 0;     // 本单元建议码清单（供注册表核对）
    virtual core::DiagnosticRecord make(std::string_view code, std::optional<core::ObjectId> subject,
        std::string context, std::string cause, std::string recommendedAction,
        std::optional<core::ComparativeFields> comparison = std::nullopt) const = 0;
};
```

构造遵循 core DiagData 不变量（C-1～C-3）；比较型诊断三要素实际/期望/单位必填（ERR-01/UX-03）；`NotApplicable` 输出经 SourcedValue::notApplicable 语义（不伪造数值）。合法调用：本单元内部构造 + 测试断言（testkit checkDiagnosticRecord）。非法调用：绕过本接口以裸字符串拼诊断码入正式结果（码值权威在注册表——静态检查拦截）。

### 9.7 注入用最小接口（`Contexts.hpp`；适配器归 L5/请求方）

| 接口 | 前置 | 后置 | 错误 | 线程 | 所有权/生命周期 | 示例/非法 |
| --- | --- | --- | --- | --- | --- | --- |
| `IPolicyNameContext`（§3.3） | 适配 runtime ⑥端口；与场景同源 | 解析只读、幂等；不修改映射 | 不可解析→nullopt（不猜测——ARC-04） | 并发只读 | L5 共享；进程级 | 合法：会话构建解析 device/frames；非法：实现内做前缀拼接（R-4——适配器不得含拼接逻辑，仅转发 runtime 结果） |
| `IPolicyBytesSource`（§3.3） | 适配 project ②端口 | 字节只读 | 缺失→nullopt | 并发只读 | 同上 | 合法：resolvePolicy 取策略对象字节；worker 为物化载荷存储 |
| `IPolicyCallContext`（§3.3） | 宿主（execution/主进程）注入 | cancellationRequested/alive 反映宿主状态 | 无（查询） | 并发只读 | 每运行/调用一个；运行结束置失效 | 合法：evaluate 传入；非法：失效后仍期待正式输出（§6.1 迟到拒绝） |
| `IPolicyValidationContext`（§5.1） | 适配修订闭包查询 | 存在性/角色只读 | 无 | 并发只读 | 每次解析一个 | 合法：resolvePolicy/validate 传入 |

---

## 10. 跨单元调用流程

### 10.1 主流程：策略解析→快照→评估→证据（S2/S6 的 policy 切面）

```
 域插件(kinematics/…)      policy(④端口)                execution/worker            evidence
    │ ②端口取策略对象引用(oid,cv)
    │ resolvePolicy ─────────►│ bytesSource→解析→发布(P)   │                         │
    │ ◄── EngineeringPolicySet│                            │                         │
    │ 快照组装：policyRef=P ───────────────────────────────────────────────────────►│ SnapshotBuilder
    │                            │                            │ 派发（物化快照）        │ 切片[Policy→P]→sliceId
    │                            │ ◄── createSession(P,scene,names) ── worker 内同一实现│
    │                            │ ──► CollisionEvaluation ──►│ 评估器按 KIN-02 过滤；  │
    │                            │                            │ 组装证据(findings/coverage)
    │                            │                            │ aggregateVerdict（五级）─►│
```

### 10.2 与 runtime 的交接（P-POL-1 已核对关闭：2026-09-10 CR-04）

运行时对象取得：请求方（可依赖 runtime 者）从编译产物构造 `CollisionScene`（shared_ptr const WorkCell＋对象清单＋相邻对事实＋场景内容身份）。名称解析：仅经 `IPolicyNameContext`（L5 适配⑥端口）；名称映射内容身份随会话输出（CON-06）。只读生命周期：共享所有权＋调用上下文存活双保险（§6.1）。policy 对 runtime 的全部期待汇总为 §3.3 两接口＋场景值结构——**核对已完成（CR-04，traceability/foundation-api-diff.md）**：期待全部被 runtime.md §7.3/§8.3/§9.1/§10.5 满足，无接口扩大；适配映射＝`IPolicyNameContext`↔`IRuntimeNameResolver`（Expected 错误→nullopt）、`CollisionScene.workcell` 经快照别名构造共享只读、`sceneContentIdentity`←`workCellCompileIdentity`。

### 10.3 与 execution 和业务单元的交接

- 评估器可重入/并发隔离：会话 ConcurrentReadOnly；并行由调用方对独立查询分派（无共享可变状态、无归约——NFR-COR-02 兼容）。
- 取消传递：execution 实现 `IPolicyCallContext`；评估在样本边界协作检查（NFR-PERF-02 精神）；Canceled 输出 finalized=false。
- 失败返回：Failed＋诊断（不抛跨边界）；execution 将其纳入结果流（错误码化归通道层——进程模型边界）。
- 仅 execution 可做的调用：无（policy 不限制调用者身份，但**正式评估运行**必须经 execution 派发以获得五元组绑定与归档——TASK 家族约束，非 policy 强制）。
- 不得直接访问策略内部实现的单元：全部（内部类型不入 `include/`——R-2）；业务单元经④端口与替身（测试）消费。

### 10.4 行程上限与可确认放行走查（AT-01/MDL-06④/SA-15）

```
modeling 应用命令 → project 处理器 prepare：
  ④端口 resolvePolicy → IJointLimitEvaluator.evaluate(jointTable, policy)
  → TravelLimitExceeded（实际 6π? 阈值 4π, rad——比较型三要素）
  → 处理器构造 ConfirmableFinding（core DiagData；码值 diagnostics 注册表）
  → 命令上下文回调 → ui 确认对话 → 用户显式确认 → 放行；确认记录（含 policyContentId）入命令摘要
  → 未确认/拒绝 → 阻止应用（无修订）
  （多圈机型：经 UX-08 工程策略入口改阈值为 6π → 新内容身份 → 新修订 → 复检通过正常放行——AT-01 反例）
```

policy 职责止于比较型结果供给；project 不持有 4π（project.md §6.5 同口径）。

### 10.5 倒挂机型一致消费走查（AT-37/MDL-22/M-11）

```
RobotDesign 安装姿态=倒挂 180° → runtime 编译进 CanonicalModel 的 R_world_base（单一权威）
→ 碰撞场景（供 policy 适配）内含同一变换 → 会话直接消费编译产物状态（policy 禁止叠加二次旋转——R-POL 规则）
→ 环境几何（世界系固连）与机器人几何的相对位姿正确 → 碰撞几何与重力方向不错位
（契约测试：倒挂场景碰撞对象对与手算一致——POL-AT-4 观测点；四消费方一致性总测归 runtime 牵头）
```

### 10.6 与 diagnostics / ui / project 的交接

diagnostics：建议码表注册（§9.6）；文案与用户可见表述归其所有。ui：工程策略摘要数据（只读投影：域启用/阈值/过滤对计数——显示单位经 core 换算投影，不改身份）；确认对话数据（经 project 命令上下文）。project：策略对象持久化（字节对 project 不透明）；策略编辑命令处理器适配器（L5 装配宿主——P-POL-9 登记）；确认绑定 policyContentId（§8.6）。

---

## 11. 验证方案及反例矩阵

测试目标 `sdurws_ird_policy_test`（单元内）与 `sdurws_ird_policy_contract_test`（跨单元）。**全部为设计用例，未实现未运行；实现状态随 §12 任务登记；任何未执行测试不得标注通过**。测试设施：testkit 断言/夹具（`IRD_TEST_INFO`/`IRD_EXPECT_CLOSE|AT_MOST|IDENTICAL`/`checkDiagnosticRecord`/`checkComparativeFields`/`checkSetEquivalent`/`checkStableOrder`/`DeterministicEnv`）；替身（header-only，`policy/test/sdurws/ird/policy/testdouble/`）：`ScriptedCollisionEvaluator`（按脚本返回预设 findings/Failed/Canceled/异常）、`StubPolicyProvider`——**替身只验证契约与消费方逻辑，其输出不构成任何碰撞算法正确性证明**（边界声明随测试文档留痕，同 evidence EV-REG-3 精神）。真实碰撞数值正确性经内置后端对构造场景（已知相交/分离几何）的解析算例验证（analytic-case 类黄金数据集，WP-02 协同）。

| 组 | 用例（需求/AT 依据｜前置｜操作｜预期｜观测点） |
| --- | --- |
| POL-ID-1 重复解析同身份 | CON-06/NFR-COR-02｜同一策略对象字节｜resolvePolicy 两次｜内容身份逐字节相等、对象逐字段相等｜cid 相等；codec 往返 parse(encode(x))==x |
| POL-ID-2 字段排序不变身份 | CON-05｜同一策略、规则/对象清单输入顺序打乱｜两次解析｜内容身份相等｜编码按规范序重排的断言（codec 层单测） |
| POL-ID-3 显示单位不变身份 | KIN-12/AT-27/UX-08｜安全间距以 m 与 mm（等值）两种输入单位｜解析比较｜内容身份相等；SI 真值相等｜归一化只经 core convert |
| POL-ID-4 阈值变化产生新身份 | CON-05/CON-06｜安全间距 0.02→0.03｜两次解析｜身份不等｜cid 差异；失效矩阵 policy 行联动（evidence 侧） |
| POL-ID-5 浮点近似相等非身份键 | 附录 D 第 12 项精神/evidence D-06｜阈值相差 1e-15｜两次解析｜身份**不等**（位模式编码）｜文档断言：容差不入身份 |
| POL-PARSE-1 未来版本拒绝 | PM-06 精神/§5.2｜schemaVersion=2｜resolvePolicy｜空 policy＋POLICY-SCHEMA-VERSION-FUTURE＋升级指引｜诊断含实际/期望版本（比较型） |
| POL-PARSE-2 非法阈值 | NFR-COR-03｜NaN/±Inf/负值/越域（nearLimitRatio=1.5）逐项｜解析｜逐项拒绝＋原文保留｜诊断码逐一对应；实际值原文可见 |
| POL-PARSE-3 单位不一致 | NFR-COR-03｜safetyClearance 携带 `N`（力）单位｜解析｜拒绝＋POLICY-UNIT-MISMATCH｜core convert 错误转发 |
| POL-PARSE-4 规则冲突 | ARC-05/§7.2｜excludedPairs 覆盖 mandatoryPairs（含经 Role/Group 展开的等价对）｜解析｜拒绝＋POLICY-RULE-CONFLICT，定位冲突对｜展开后集合求交断言 |
| POL-PARSE-5 未知字段/重复/循环 | §5.2｜未知键；同对重复登记；组引用未定义组/组引用组｜解析｜逐项拒绝＋对应诊断码｜诊断全量（不短路） |
| POL-PARSE-6 对象不存在 | §5.2/CON-01｜规则引用闭包外对象｜解析（经上下文）｜拒绝＋POLICY-SCOPE-OBJECT-MISSING（定位对象）｜subject 绑定对象 ID |
| POL-SCOPE-1 域区分 | MDL-04/15/§7.1｜场景含连杆/工具/环境/工件；策略仅启用 Self｜会话评估｜仅连杆对进入 pairsInScope；Environment 对零输出资格｜coverage 分域计数 |
| POL-SCOPE-2 过滤不隐藏必检 | §7.2｜①解析期：排除∩必检（策略级拒绝）；②运行期：默认相邻过滤场景中相邻对碰撞几何构造相交｜①解析②评估｜①拒绝发布；②相邻对被过滤（appliedFilters 记录）、显式必检的相邻对发现被输出｜appliedFilters 含 ruleOrigin；findings 与排除集无交集 |
| POL-EVAL-1 构型级输出 | KIN-02/C8/AT-03｜两组 IK 构型：一组碰撞、一组清晰｜两次 SingleState 评估｜分别检出/无发现；输出无任务级结论字段（类型断言）｜findings 样本索引与对象对正确 |
| POL-EVAL-2 全候选碰撞 | C5/C8/AT-03｜全部候选构型碰撞｜SampleSet 评估＋替身 provider 消费｜policy 仅输出发现集合；"搜索未果/DataInsufficient"由消费方（kinematics→evidence）表达——评估器无该输出通道｜类型层无不可行字段 |
| POL-EVAL-3 必经状态证明素材 | §8.1 表 2③/C8｜任务点构型（必经）碰撞发现｜映射为 evidence proof.collisionPairs（契约夹具）｜对象 ID 对＋判定＋采样位置字段齐备可被 validateProof 采信｜契约测试字段对齐断言 |
| POL-EVAL-4 路径 vs 构型输出差异 | TRJ-04｜同一几何对在 PathSequence 与 SampleSet 查询｜比较输出｜PathSequence 发现含 pathParameter 且保序；SampleSet 按索引独立｜pathParameters 等长校验 |
| POL-EVAL-5 失败≠碰撞 | KIN-05/EVI｜替身后端抛 rw 异常｜评估｜status=Failed＋POLICY-CLL-EVALUATION-FAILED，findings 不含该对，finalized=false｜无"无碰撞"字段产生 |
| POL-EVAL-6 取消不产正式证据 | TASK-02/UX-03｜第 N 样本后 cancellationRequested=true｜评估｜status=Canceled＋部分 findings＋finalized=false；无错误诊断｜finalized 断言 |
| POL-EVAL-7 稳定排序 | NFR-COR-05/AT-19｜同会话同查询评估 3 次＋打乱内部计算顺序的多线程分派｜比较输出｜findings/minDistances/appliedFilters 逐字段一致｜checkStableOrder |
| POL-EVAL-8 无隐式阈值/无模式影响 | KIN-13/R-POL-3/5｜①构造 RobWork 默认容差不同的场景；②Preview/Quick/Verified 语境下同查询｜评估｜①输出与策略阈值一致（默认容差不改变判定——边界用例按 §7.4）；②输出逐字段相等（模式不是输入）｜API 类型断言（query 无阈值/模式成员） |
| POL-EVAL-9 几何缺失 | KIN-05｜作用域对象无碰撞几何｜评估＋coverage 检查｜Completed 但 pairsWithGeometry 缺口＋POLICY-CLL-GEOMETRY-MISSING；无"无碰撞"结论字段｜coverage 计数；消费方判数据不足的接口就绪 |
| POL-COMPAT-1 版本不兼容 | CON-04/§8.2｜schema/后端/数值锚失配的请求｜checkPolicyCompatibility｜Incompatible＋对应 reason＋POLICY-VERSION-INCOMPATIBLE（比较型）｜诊断三要素完整 |
| POL-COMPAT-2 内容身份变化拒复用 | CON-05/06｜策略阈值变化后旧切片请求｜（evidence 联动）切片不命中｜本侧供身份＋原因素材｜与 evidence judgeCacheHit 契约测试联动 |
| POL-SHARE-1 同一实现跨入口 | ARC-05/NFR-COR-05/AT-19｜kinematics/trajectory/optimization 替身消费者经同一 provider｜会话身份与输出比较｜三入口同一 evaluator 实例（指针相等）＋等价输出＋同一原因码｜实例同一性断言；checkSetEquivalent |
| POL-CONC-1 并发只读 | NFR-COR-02｜多线程对同一会话并发 evaluate｜执行｜无数据竞争（TSAN 或等价评审证据）、输出一致｜并发结果逐字段相等 |
| POL-LATE-1 迟到调用拒绝 | §6.1/任务约束｜上下文 alive()=false 后 evaluate｜调用｜Failed＋POLICY-CLL-CONTEXT-EXPIRED，无正式输出｜finalized=false；诊断码 |
| POL-EXC-1 RobWork 异常/资源错误 | NFR-COR-03/§6.3｜后端抛 rw::common::Exception；空场景/空构型序列（后者为违约）｜评估/调用｜异常捕获转 Failed；空场景→EmptyScope（非"无碰撞"）；违约→PolicyError｜不崩溃、不吞 |
| POL-JNT-1 行程上限比较 | MDL-06④/AT-01/附录 D 第 11 项｜行程=4π（边界）/＞4π/continuous 关节｜IJointLimitEvaluator｜边界不超限；超限输出比较型三要素；continuous 豁免（工程工作范围另检）｜actual/threshold/rad 齐备（checkComparativeFields） |
| POL-JNT-2 近限位/区间 | KIN-13/MDL-06｜阈值 nullopt；qmin≥qmax；近限位边界｜评估｜nullopt→NotApplicable 标记；区间非法→诊断级发现＋跳过；裕量按 §7.4 边界｜不伪造数值断言 |
| POL-AT-1 AT-01 观测点 | AT-01｜走查 §10.4 全链（替身 project 处理器）｜执行｜比较型诊断→确认放行→摘要含 policyContentId；未确认阻止｜confirmations 绑定四元组（project 契约夹具） |
| POL-AT-2 AT-19 观测点 | AT-19｜三入口（替身）同规范状态同策略｜执行｜对象 ID 对/判定/原因码完全一致｜原因码=过滤 ruleOrigin＋域默认 |
| POL-AT-3 AT-27 观测点 | AT-27/UX-08｜①显示单位切换；②分析配置修改（替身配置对象）｜①重解析②评估｜①策略身份与输出不变；②策略无覆盖通道（API 断言）——配置差异经 evidence Configuration 条目表达｜身份相等断言 |
| POL-AT-4 AT-37 观测点 | MDL-22/AT-37｜倒挂编译产物场景（构造）｜评估｜对象对与手算一致（环境几何世界系固连、机器人倒置）；无二次旋转（实现评审＋数值断言）｜解析算例对照（analytic-case 数据集） |
| POL-TD-1 替身边界 | 任务约束§八｜全部替身用例｜评审检查｜测试文档显式声明替身输出不构成碰撞算法正确性证明｜测试注释/文档留痕 |

每条用例经 `IRD_TEST_INFO` 登记需求/AT 追溯；解析/编码契约夹具数据集按 testkit manifest schema 登记（contract-fixture 类）；碰撞解析算例（已知相交/分离/边界距离几何）登记 analytic-case 类（producer.solverConfig 记录策略阈值快照——testkit §4.3.2 工程策略默认类别的消费方式）。

---

## 12. 阶段 A 实现任务拆分

局部编号 `POL-Txx`（policy＝**WP-07**，REQUIREMENTS §3/ARCH §3.1 既有登记；不重排上游 WP 编号；DETAILED-DESIGN/development-task-breakdown 产出后如需对齐，以增量修订处理）。依赖顺序自上而下。**阶段 A 交付**：策略模型/解析/校验/身份、碰撞评估唯一实现（内置后端适配）、关节限位与行程阈值契约、兼容判定、④端口与注入接口、可控测试替身、契约测试与故障注入；**不含**：真实 FK/IK/轨迹/动力学/选型/优化评估器（阶段 B/C/D 经③端口接入）、外部碰撞策略（Yaobi/PQP 等——无需求）、策略编辑 UI（ui）、缓存/检查点存储（execution）。

| 任务 | 输入 | 产物 | 依赖 | 涉及文件 | 验证方式 | 完成条件 |
| --- | --- | --- | --- | --- | --- | --- |
| POL-T01 构建落位 | 骨架 CMakeLists、§3.4 | `sdurws_ird_policy` 升级 STATIC（C++17、链 core＋sdurw_math/kinematics/models/proximity，目标名实测核对）；注册 `_test`/`_contract_test`（gtest 按 development-task-breakdown §5.5 定稿） | 无（与 CORE-T01/EV-T01 平行） | `industrialrobot/CMakeLists.txt`、`policy/CMakeLists.txt`（新）、`policy/src/*`（空起步） | 独立冒烟＋集成构建配置成功；UT-BUILD 等价扫描（无 Qt、无私有头出 include/、依赖图仅 core＋RobWork 边） | 两模式构建零错误；红线扫描零命中；§3.4 红线扩展建议已提交 WP-01（P-POL 登记） |
| POL-T02 错误与策略类型 | §3.1/§4、core 契约 | `Errors.hpp/.cpp`、`PolicySet.hpp`（含 PolicyThreshold/四态承载） | POL-T01 | 同名文件 | POL-PARSE-2 前置（字段校验）单测 | 字段约束与不可变性用例通过 |
| POL-T03 编码与身份 | §5.3 | `PolicyInput.hpp/.cpp`（RawPolicyInput/PolicyCodec/contentIdentity） | POL-T02 | 同名文件 | POL-ID-1~5 | canonical 往返＋位模式＋排序无关性用例通过 |
| POL-T04 解析器与校验器 | §5.1/§5.2/§9.2 | `PolicyParsing.hpp/.cpp`（resolvePolicy/IPolicyValidator/IPolicyValidationContext） | POL-T03 | 同名文件 | POL-PARSE-1~6 | 错误分类表全量反例通过；诊断全量不短路 |
| POL-T05 上下文与端口 | §3.3/§9.1/§9.7 | `Contexts.hpp`、`PolicyPort.hpp/.cpp`（PolicyProvider 实现＋记忆化） | POL-T04 | 同名文件 | POL-ID-1（缓存一致性） | 注入接口与端口契约用例通过 |
| POL-T06 场景与会话 | §6.1/§6.4 | `CollisionEvaluator.hpp/.cpp` 之 CollisionScene/会话构建/作用域展开/检测器初始化 | POL-T02/T05 | 同名文件 | POL-SCOPE-1/2 构建期部分 | 作用域矩阵展开与冲突拒绝用例通过 |
| POL-T07 评估实现 | §6.2/§6.3/§6.5/§6.6 | `CollisionQuery.hpp/.cpp`＋RobWork 适配评估（SingleState/PathSequence/SampleSet、最小距离、取消、异常捕获） | POL-T06 | 同名文件 | POL-EVAL-1~9、POL-EXC-1、POL-CONC-1 | 状态机/稳定排序/边界值决策表用例通过 |
| POL-T08 关节限位评估 | §7.4/§9.4 | `JointLimits.hpp/.cpp` | POL-T04 | 同名文件 | POL-JNT-1/2 | 行程上限边界（4π）与比较型三要素用例通过 |
| POL-T09 兼容判定 | §8.1/§8.2/§9.5 | `Compatibility.hpp/.cpp`（checkPolicyCompatibility） | POL-T04 | 同名文件 | POL-COMPAT-1/2 | 版本/后端/锚失配全量 reason 用例通过 |
| POL-T10 诊断构造 | §9.6 | `Diagnostics.hpp/.cpp`（建议码表＋IPolicyDiagnostics） | POL-T02 | 同名文件 | checkDiagnosticRecord 族（testkit） | 码表与构造不变量用例通过 |
| POL-T11 替身与契约套件 | §11、testkit.md §10 | header-only 替身（`test/testdouble/`）＋POL-* 全部用例体＋契约夹具（evidence 证明字段形状对齐） | POL-T05~T10、testkit 可用 | `policy/test/*`、`testdata/golden/pol-*` | §11 矩阵逐条 | 全部用例通过并留痕；替身边界声明在案（POL-TD-1） |
| POL-T12 文档与门禁同步 | 全文 | 骨架 README 任务卡指向修正（§9→§12）；§15.3 待裁决项状态更新；UT-BUILD 建议并入 CI（WP-01 侧） | POL-T01~T11 | 本文、`policy/include/.../README.md` | 评审 | 本文与实现零偏差登记；未决项最新状态 |

每任务完成条件均含"测试通过并留痕"；任何未执行测试不得标注通过。

---

## 13. 后续阶段承接及接口交接清单

| 阶段/单元 | 从 policy 接收 | 须自行提供（责任） | 典型 AT 载体 |
| --- | --- | --- | --- |
| A·runtime（并行交付，接口交接） | `IPolicyNameContext`/`CollisionScene` 最小契约（§3.3/§6.1——本文单侧冻结）；场景内容身份与相邻对事实的供给义务 | RuntimeNameMap 只读适配；编译产物场景装配（含 R_world_base 消费一致性）；nameMapContentIdentity 计算；编译器契约版本（复现块） | AT-18/37、P-POL-1 |
| A·execution（并行交付） | `IPolicyCallContext` 契约（取消/存活）；④端口在 worker 侧的装配形态；异常→Failed 的通道错误码化 | 取消实现、上下文生命周期（运行结束置失效）、worker bytesSource 物化、缓存/检查点存储（消费 evidence 判定＋本文身份） | TASK-01~03、NFR-PERF-02 |
| A·evidence（协作） | 策略内容身份计算登记（§5.3——跨单元身份可比前提，core R-2 同源）；`CollisionBackendDescriptor` 版本串（复现块 collisionBackendVersion 取值）；findings→证明字段映射（§8.4） | Policy 依赖条目语义（已冻结）；证明校验对本文素材的采信 | CON-04~06、AT-19 |
| A·diagnostics（协作） | 建议码表（§9.6）与诊断构造 | 码值分配、文案、注册表 | ERR-01 |
| A·project（协作） | 行程上限比较型结果（§10.4）；策略字节 schema（PolicyCodec——磁盘归 project、字节不透明） | 处理器放行流（SA-15）；确认绑定 policyContentId；策略编辑命令处理器适配器宿主（P-POL-9）；对象编址/升级器 | AT-01、MDL-06④ |
| B·kinematics | ④端口/会话/SingleState·SampleSet 评估；关节裕量素材（§9.4） | IK 硬过滤（KIN-02）、覆盖率算法（KIN-04）、AnalysisConfiguration（不得覆盖策略——KIN-13） | AT-03/19/27 |
| B·optimization | 同上（OPT-B 静态子集）；兼容判定（预检消费） | 候选编译与静态约束编排、缓存与种子 | AT-09 |
| C·trajectory | PathSequence 评估＋pathParameters 约定；安全间距唯一来源 | TRJ-04 复检协议（P-06 数值）、细分预算、段级 DataInsufficient | AT-06 |
| C·dynamics/selection | （dynamics 不直接消费碰撞）；selection 待 SEL-05 归属裁决（P-POL-3） | DYN 证据；SEL 阈值归属与域登记 | AT-07/08 |
| B~E·ui/workflow | 策略只读摘要数据；显示开关不入身份的保证 | UX-08 统一入口与摘要呈现、确认对话、编辑表单（经①端口提交） | UX-08、AT-27 |
| R2·KIN-09~11 | SampleSet 大样本评估（会话复用） | 采样计划与可视化 | AT-25/26 |
| testkit/WP-02 | 解析算例登记格式；策略阈值快照在 manifest producer.solverConfig 的记录方式（§4.3.2 工程策略默认类别） | 黄金数据集内容与容差档案 | NFR-COR-01 |

阶段接入顺序约束：任何业务评估器接入前④端口与共享实现须先在对应进程装配完成（AT-19 的装配侧前提）；替身数据不得冒充真实证据（POL-TD-1）。

---

## 14. 需求—设计—验证追踪矩阵

| 需求/上游条款 | 设计落点 | 验证（§11 组） |
| --- | --- | --- |
| ARC-05（唯一策略对象＋唯一共享实现；无私有开关/默认值/重复算法） | §2.1 O-1/O-4、§6.5、§3.4 红线扩展 | POL-SHARE-1、POL-EVAL-8 |
| CON-04（缓存/检查点按契约兼容） | §8.1/§8.2 | POL-COMPAT-1/2 |
| CON-05（切片内容身份失效） | §5.3、§8.2/§8.6 | POL-ID-2~5、POL-COMPAT-2 |
| CON-06（快照存已解析策略身份；跨入口一致） | §5.1/§5.3、§6.4、§8.7 | POL-ID-1、POL-SHARE-1、POL-EVAL-7 |
| ERR-01（三轴正交；稳定诊断绑定对象；比较型三要素） | §5.2、§9.6、§7.4 | POL-PARSE-2/6、POL-JNT-1（checkComparativeFields） |
| EVI-01/§8.1 表 1（模式效力；Quick≠Verified） | §6.2（无模式输入）、§8.2 | POL-EVAL-8、POL-COMPAT 联动 |
| EVI-01/§8.1 表 2③④（C8 作用域；搜索未果；证据缺失） | §6.3、§8.3/§8.4/§8.5 | POL-EVAL-1~5、POL-EVAL-9 |
| MDL-04（碰撞网格/自碰撞配置输入） | §4.3、§7.1（Self 域）、RawPolicyInput 来源 | POL-SCOPE-1 |
| MDL-06④（行程上限策略校验；默认 4π；确认放行） | §4.4、§7.4、§9.4、§10.4 | POL-JNT-1、POL-AT-1 |
| MDL-12（continuous 工程工作范围） | §9.4（engineeringRange 检查语义） | POL-JNT-2 |
| MDL-15（环境对象显式引用参与碰撞） | §4.3、§7.1（Environment 域） | POL-SCOPE-1 |
| MDL-22/AT-37（基座—世界变换单一消费） | §6.1、§10.5 | POL-AT-4 |
| KIN-02（碰撞硬过滤） | §6.2/§6.3（构型级输出） | POL-EVAL-1/2 |
| KIN-04/KIN-09（覆盖/采样按统一策略） | §6.2（SampleSet） | POL-EVAL-4 |
| KIN-05（缺检测器→数据不足，≠无碰撞） | §6.3（Failed 子诊断）、§7.5、coverage | POL-EVAL-5/9、POL-EXC-1 |
| KIN-13（配置不得覆盖策略；阈值归策略） | §4.4、§6.2（无阈值/模式参数）、§7.3 | POL-EVAL-8、POL-AT-3 |
| TRJ-04（复检消费策略；无私有线宽） | §4.3（safetyClearance）、§6.2（PathSequence） | POL-EVAL-4、POL-SCOPE-2 |
| OPT-03（OPT-B 碰撞静态子集） | §6.2（SampleSet）、§6.5 | POL-SHARE-1 |
| SEL-05（惯量比阈值——归属待裁决） | §4.4（预留，不预填） | P-POL-3 登记 |
| UX-08（统一工程策略入口；开关分组异名） | §4.2（显示设置不在策略）、§10.6 | POL-ID-3、POL-AT-3 |
| NFR-COR-02（确定性） | §5.3、§6.4 | POL-ID-1、POL-EVAL-7、POL-CONC-1 |
| NFR-COR-03（非有限/非法单位/缺失不得静默） | §5.2、§7.5 | POL-PARSE-2/3、POL-EXC-1 |
| NFR-COR-05（三入口完全一致） | §6.4/§6.5 | POL-SHARE-1、POL-EVAL-7 |
| NFR-MNT-01（零 Qt） | §1.4/§3.2 | POL-T01 红线扫描 |
| NFR-MNT-03（单一权威定义） | §2.1/§2.2、§7.3 | POL-T01/T10＋静态检查 |
| NFR-MNT-07（静态检查禁副本；例外登记） | §3.3（R-4 消费≠拼接）、§3.4、§6.5 R-POL-1~5 | POL-T01（门禁联动） |
| NFR-DEP-05（碰撞后端基线） | §8.1（CollisionBackendDescriptor） | P-POL-5 登记 |
| 附录 D 第 11 项（行程上限 4π 工程策略默认） | §4.4、§5.1③、§7.4 | POL-JNT-1 |
| 附录 D 第 12 项（身份精确等值） | §5.3（位模式/精确比较） | POL-ID-5 |
| 附录 D C4/C7（比较公式与 ε_abs 归 core） | §7.3（不复制公式；策略侧数值校验经 core） | POL-T04/T05 单测 |
| SA-02（框架零修改） | §1.4（基线直接消费） | POL-T01（无 patch） |
| SA-06（策略权威唯一） | 全文（§2.1 O 表） | POL-SHARE-1 |
| SA-10（依赖红线门禁） | §3.2/§3.4 | POL-T01 红线扫描 |
| SA-15（可确认诊断放行） | §9.4/§10.4 | POL-AT-1 |
| AT-01/03/19/27/37（观测点） | §10.4/§11 | POL-AT-1~4、POL-EVAL-1/2 |

---

## 15. 设计决策、风险、待裁决项与变更记录

### 15.1 设计决策登记（本文作出并说明理由的普通实现选择）

| ID | 决策 | 理由与备选 |
| --- | --- | --- |
| D-01 | policy 编译依赖＝core＋RobWork 基线（math/kinematics/models/proximity）；runtime/project/evidence 一律注入或值传递 | ARCH §3.5 依赖表未登记横向边；与 evidence D-07、project IModelCompilePort 同一模式；备选（policy→runtime 直链）违反 §3.2"内核不横向互链" |
| D-02 | 场景输入＝`shared_ptr<const WorkCell>` 共享只读＋`CollisionScene` 值清单；迟到拒绝由 `IPolicyCallContext::alive()` 承担 | 内存安全（所有权）与运行有效性（上下文）分层；备选（weak_ptr 探测）将生命周期事实分散到两处，且无法表达"运行已归档"语义 |
| D-03 | 策略内容身份只对语义闭包计算；origin/诊断/兼容注记/policyObject 排除 | 身份＝工程语义函数（CON-05 精神）；审计信息不改变语义；显示设置天然出局（POL-ID-3） |
| D-04 | 阈值来源三分 `PolicyValueOrigin{Explicit, DefaultAppendixD, DefaultTemplate, Inherited}`（不新增 core ProvenanceKind） | core 五类来源面向模型数据（ARCH §6.2），策略默认语义不合其类；policy 局部词表避免污染全局契约 |
| D-05 | 无冻结默认的阈值（安全间距/近限位比/条件数）不设默认值——nullopt＝检查显式不适用 | 附录 D 仅第 11 项冻结；发明默认＝私自新增阈值（任务约束）；模板数值走 P-POL-2 裁决 |
| D-06 | 过滤不隐藏必检＝**解析期结构拒绝**（排除∩必检=∅ 强制）＋运行期必检先评＋appliedFilters 全量输出 | 结构保证优于运行期补偿；纵深防御覆盖"等价展开"绕过（Role/Group 展开后求交） |
| D-07 | 评估会话不可变＋evaluate const＋查询间无共享状态/无归约 | NFR-COR-02 确定性与并发只读的最简实现；备选（每次评估重建检测器）性能不可接受 |
| D-08 | 边界归属冻结：间距 d=m 属安全侧（d ≥ m 即满足）；行程 T=L 属合规侧；接触（后端二值）属碰撞 | 与需求措辞同向（"安全间距"/"超阈值才超限"）；边界统一在安全/合规侧，杜绝"接近阈值判为碰撞"；非新增阈值 |
| D-09 | 评估 API 无阈值/模式参数（结构防覆盖） | KIN-13"分析配置不得覆盖策略"的机械化；文案/评审约束弱于类型约束 |
| D-10 | 内置 ProximityStrategyRW 为默认且唯一注册后端；后端身份经描述符冻结 | 实测树内无第二策略；NFR-DEP-05 冻结后锁定；引入第二算法须需求变更＋基线登记（不新增第二碰撞算法） |
| D-11 | 名称消费＝IPolicyNameContext＋场景 Frame 整名；policy 实现禁止前缀拼接/剥离 | R-4 红线；例外登记语义"消费整名≠拼接"（P-POL-8 请架构侧确认措辞） |
| D-12 | 替身 header-only 置于 `policy/test/`（不入产品 include/），供各单元测试目标消费 | T-1 红线（产品不链测试件）与跨单元契约测试需求（AT-19 替身入口）的折中；备选（独立 testdouble 库）增加目标数无新边界价值 |
| D-13 | JointLimit 评估器检出区间/工作范围非法为**诊断级**发现并跳过，不执行阻断 | 硬断言执行权归 modeling/project 处理器（MDL-06 就地阻断语义）；policy 只供给比较事实，避免双权威 |
| D-14 | 策略不按工况差异化阈值（caseObjects 仅限定适用范围） | 工况差异经场景对象集（负载/工件不同）表达已是需求现状（REQ-04 工况定义）；阈值按工况覆盖会破坏单一策略身份（ARC-05）——登记 P-POL-7 待确认 |
| D-15 | `PolicyError` 不跨进程边界；评估内部异常全捕获转 Failed | 进程模型边界（ARCH §4；core §6.2/evidence D-15 同源）；worker 通道错误码化归 execution |

### 15.2 风险

| # | 风险 | 影响 | 缓解 |
| --- | --- | --- | --- |
| R-1 | 已消除（2026-09-10，CR-04）：runtime.md 已产出且承接 §3.3/§6.1 全部期待，单侧冻结转为双侧核对一致 | （已消除）原装配适配器与场景装配协议可能返工 | 映射记录见 traceability/foundation-api-diff.md CR-04；后续 runtime 修订触及所涉接口走双方增量修订；契约测试仍以替身先行（POL-T11） |
| R-2 | core.md/evidence.md 均 Draft 未冻结，签名可能变化 | POL-T02 起返工 | §3.2 消费清单逐项锚定章节；冻结时出 diff 增量同步（P-POL-4） |
| R-3 | 无冻结默认的阈值导致默认策略"近限位/间距检查不适用"，产品开箱行为可能不符预期 | 用户体验/验收解释分歧 | 显式 NotApplicable 输出（不伪造）；P-POL-2 推动模板策略数值尽早裁决 |
| R-4 | RobWork ProximitySetup 规则按 Frame 名模式工作，名字直通链路（Frame→名→规则）存在编码/转义边缘 | 过滤规则不生效或误伤 | 规则以完整名**精确**匹配（不用通配）；POL-SCOPE-2/AT-19 用例覆盖；与 runtime 名称生成唯一性（AT-18）联动 |
| R-5 | 大场景会话构建成本（几何对集展开＋检测器初始化） | 首次评估时延 | 会话按 (policy, scene, nameMap, backend) 复用（调用方持有）；性能基准登记 performance-baseline 类（阶段 C 规模化再优化） |
| R-6 | project.md 当前 §1～§15 已齐备 | 历史磁盘缺失问题已消除 | 接口复核仍按 P-POL-6 / CR-03 执行 |
| R-7 | 后端内置容差模型与工程阈值的语义边界被误解为"隐式阈值" | 判定口径争议 | CollisionBackendDescriptor.toleranceModel 显式登记＋§7.4 表区分"后端数值分辨率"与"工程阈值"；POL-EVAL-8 钉住 |
| R-8 | 替身被误读为碰撞算法验证 | 验收误判 | POL-TD-1 显式边界声明；解析算例（analytic-case）承担数值正确性 |
| R-9 | ARCHITECTURE v0.11 Draft 评审（A9+）调整④端口/依赖表 | §3/§9 返工 | 严格锚定 §7.2/§7.5/§3.5 明文；P-POL-4 登记同步义务 |

### 15.3 待裁决项（问题—依据—影响—建议—需要裁决者）

| # | 问题 | 依据 | 影响 | 建议 | 需要裁决者 |
| --- | --- | --- | --- | --- | --- |
| P-POL-1 | **已关闭（2026-09-10，CR-04）**：runtime.md 已产出，`IPolicyNameContext`/`CollisionScene`/场景内容身份/编译器契约版本供给全部被其 §7.3/§8.3/§9.1/§10.5 承接，交叉核对无分歧 | ARCH §3.5（无 policy→runtime 边）、§7.3/§7.4；evidence.md §13 对称交接 | （已消除）原装配适配器与场景协议可能调整 | （保留）后续 runtime 详设修订若触及 §3.3/§6.1 所涉接口，走双方增量修订；映射记录见 traceability/foundation-api-diff.md CR-04 | runtime 详设所有者＋本文所有者 |
| P-POL-2 | 安全间距/近限位比/条件数警告阈值无上游冻结默认（附录 D 仅第 11 项）；产品模板策略数值空缺 | 附录 D 类别说明、KIN-13、TRJ-04③ | 开箱默认行为与验收解释 | 由需求所有者以附录 D 增补或"模板策略基线"形式冻结数值；裁决前 policy 保持 nullopt＝显式不适用 | 需求所有者＋WP-07 |
| P-POL-3 | SEL-05 惯量比阈值归属：EngineeringPolicySet 还是 selection 域配置 | SEL-05"可配置工程规则"未指明所有者；ARCH §7.5 未列 | 阶段 C 选型切片的 Policy 条目声明与失效矩阵 | 建议归 EngineeringPolicySet（与行程上限同模式——跨项目工程规则、进缓存键）；selection 详设起草时交叉核对后走本文 schema 次版本追加 | 需求所有者＋selection 详设所有者 |
| P-POL-4 | 上游三份协作输入（core/evidence/testkit）与 ARCHITECTURE 均 Draft | 各文档头状态行 | 契约签名漂移 | 各自冻结时出 diff 清单，本文按影响面增量修订并留痕 | 各详设所有者＋本文所有者 |
| P-POL-5 | 碰撞后端版本取值依赖 NFR-DEP-05 冻结版本基线（未产出）；内置 ProximityStrategyRW 版本串暂取 RobWork 构建版本 | NFR-DEP-05、§8.1 | 复现块与兼容判定的版本口径 | WP-24 冻结基线时锁定 `CollisionBackendDescriptor` 取值并回填本文；变更走设计变更评审（全体切片身份影响） | 版本基线所有者（WP-24） |
| P-POL-6 | project.md 磁盘不完整（§8～§15 缺失）：确认绑定细节、命令编排、升级器不可查证 | 本文 §1.2 实测（与 P-EV-6 同源） | 策略编辑命令适配器与确认绑定实现细节 | project.md 补全后核对 §10.4/§13 交接项；不阻塞本文其余设计 | project 详设所有者 |
| P-POL-7 | 按工况差异化策略阈值的需求口径未定义；本文按"策略全项目统一、工况差异经场景对象集表达"设计（D-14） | REQ-04 工况含碰撞要求；ARC-05 单一权威 | 多工况项目的行为解释 | 维持本文解释；如需按工况阈值覆盖，走需求变更（策略身份模型需重构） | 需求所有者 |
| P-POL-8 | R-4 红线例外登记措辞：policy 消费完整设备作用域名（名称上下文/场景 Frame 整名）不构成"拼接/剥离"，但例外清单原文仅列 runtime 名称解析器 | ARCH §3.2 R-4、NFR-MNT-07 | 静态检查可能误报 policy 实现为违规 | 架构侧在例外登记中补充措辞："policy 策略消费点（只读消费整名，禁止拼接/剥离）"；本文实现遵守该边界 | 架构所有者 |
| P-POL-9 | 策略编辑命令处理器适配器的宿主未定（policy 为 L2 不能实现 project 的 ICommandHandler；UX-08 主 WP-07） | ARCH §7.1/§7.2①、project.md §6.5 | 策略编辑上线路径 | 建议 L5 应用壳装配（或 ui 单元）承载适配器，policy 供纯函数（解析/校验/编码）；development-task-breakdown 产出时分配 | 构建约定所有者＋ui 详设所有者 |

### 15.4 变更记录

| 版本 | 日期 | 说明 |
| --- | --- | --- |
| v0.1 | 2026-09-10 | 首版：基于 REQUIREMENTS v1.16（Accepted）与 ARCHITECTURE v0.11（Draft）及 core/evidence/testkit/project 四份协作输入（均 Draft；project 磁盘不完整；runtime.md 未产出）完成 15 章详细设计；冻结 EngineeringPolicySet 数据模型（含内容身份语义闭包）、解析管线与错误分类、CollisionEvaluator 唯一实现（会话/状态机/稳定排序/防旁路五规则）、作用域矩阵与过滤可追溯、阈值三分与边界值决策表、兼容判定与失效矩阵、公共接口（含注入最小接口）；验证反例矩阵 POL-* 30 组；实现任务 POL-T01～T12；待裁决 9 项（P-POL-1～9）。同日：`policy/include/sdurws/ird/policy/README.md` 的任务卡指向由"§9"修正为"§12"（与本文任务拆分章节号一致——evidence.md 同型修正先例） |
| v0.2 | 2026-09-10 | FOUNDATION-CR-01 契约冻结审查修正（CR-04/CR-07）：§1.2/§2.1/§10.2"runtime.md 未产出/待核对"过期记载更正并关闭 P-POL-1（runtime.md 已产出且承接全部期待，映射记录见 traceability/foundation-api-diff.md）；POL-T01 行 gtest 引用改指 development-task-breakdown §5.5 定稿 |

### 15.5 交付前自审记录（v0.1；自审≠实现测试≠正式验收）

| 检查项（任务约束§八） | 结论 | 证据位置 |
| --- | --- | --- |
| 是否重复 core/runtime/evidence/project 的职责 | ✔ 未重复：身份/摘要/单位/比较/诊断数据契约全经 core（§3.2）；编译/名称映射/场景归 runtime（注入——§3.3）；快照/切片/汇总/当前性归 evidence（§8.3 分工表）；持久化/命令/放行归 project（§10.4） | §2、§3、§8 |
| 是否存在第二套碰撞实现 | ✔ 唯一实现＋唯一构造入口＋红线扩展建议 R-POL-1~5（业务禁直链 proximity） | §6.5、§3.4 |
| 是否存在业务单元反向依赖 | ✔ 编译依赖仅 core＋RobWork；反向需求全部为注入/值传递/文档对齐 | §3.2/§3.3 |
| 是否把碰撞发现误判为任务不可行 | ✔ 输出类型无工程判定字段；构型/路径级作用域贯穿（C8）；任务级仅三类证明素材且判定归 evidence | §6.3、§8.3/§8.4 |
| 是否把评估失败误判为碰撞 | ✔ Failed/Canceled→finalized=false＋诊断；KIN-05 子诊断明确；coverage 防伪装 | §6.3、§7.5、§8.5 |
| 是否把阈值、容差、策略版本混为一谈 | ✔ 三分表＋版本要素分列；浮点近似相等禁入身份（POL-ID-5） | §7.3、§8.1 |
| 是否允许未验证策略进入正式证据 | ✔ 无效策略不发布（ValidationState 只有 Valid 可发布）→快照 CON-06 非空失败→②级门禁拦截 | §4.5、§5.2 |
| 是否绕过 RuntimeNameMap | ✔ 名称消费仅经 IPolicyNameContext/场景整名；R-4 合规与 P-POL-8 登记措辞确认 | §3.3、§6.1、D-11 |
| 是否将 RobWork 默认行为冒充产品策略 | ✔ R-POL-3 显式禁止＋检测器以策略 setup 显式初始化＋后端容差模型登记区隔 | §6.4/§6.5、R-7 |
| 是否在快照或上下文释放后继续评估 | ✔ 共享只读所有权＋alive() 迟到拒绝双层防护（POL-LATE-1） | §6.1、§9.7 |
| 是否引入未经上游批准的新状态、阈值或证据等级 | ✔ 无新评估模式/证据等级；唯一默认＝附录 D 第 11 项；无默认阈值以 nullopt 显式不适用（P-POL-2 登记）；边界归属 D-08 为语义冻结非数值新增 | §4.4/§4.5、D-05/D-08 |
| 是否越权修改需求、架构或其他单元机制 | ✔ 未修改任何上游文件；冲突与含混全部集中登记 P-POL-1~9（问题—依据—影响—建议—裁决者）；不受影响的设计已全部完成 | §15.3 |
| 文档自审不等于实现测试通过或正式验收 | ✔ §11 全部用例标注"未实现未运行"；§12 完成条件要求测试留痕 | §11、§12 |

## AI 执行就绪补充

本单元进入 AI 实现前必须满足：

- 本文中的职责、非职责、公共接口、数据模型、错误语义和不变量不得与 ARCHITECTURE.md 冲突。
- 单元任务使用 $(policy.ToUpper())-Txx 编号，并通过 doc/industrial-robot-design/tasks/*.json 声明前置任务、允许修改文件和验证命令。
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
