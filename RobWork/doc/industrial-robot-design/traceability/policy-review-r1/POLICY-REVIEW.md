# policy 单元专项代码评审（2026-10-28）

本轮是评审与修复建议，未实施修复，未变更正式测试、接口、治理文件或任务状态，亦不是未来修复的独立验收。发现 **12 项已确认问题（4 项 P1、8 项 P2）**：其中 10 项有隔离运行证据，2 项为静态确认。首要风险是共享后端跨场景串用几何造成真实漏检，以及当前运动学宿主另走 runtime 碰撞路径而丢失部分策略规则。正式基线 237 例全绿，未覆盖这些边界。

## 1. 现场、版本与证据边界

- 指定工作树：`C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork`；分支：`codex/core-review-fixes`。起始 HEAD：`412e173b9079fa43a1f0ef50fe07d38e852b1940`。
- 收口时另一会话提交了原有 project 修改，HEAD 为 `59af1f9b449ee23fb78dfe547b3a7339df9d325d`（PRJ-T19）。这不是本评审的提交。该提交没有改变 policy、runtime、core、testkit、evidence、diagnostics、kinematics、trajectory、optimization 或 ui 源码；相关源码行号及本轮运行证据仍适用，未因 HEAD 前进重复运行全单元基线。
- 起始脏区包括 project 源码/测试和未跟踪的 `traceability/project-review-r1/`；过程中 project 文档、凭证也由其他会话更新后提交。本评审未还原、清理、暂存或提交任何这些文件。收口现场的 `git status --short` 为空。该事实表示外部提交已发生，不表示本评审将既有修改清掉。
- [起始状态](C:/Users/zgl18/AppData/Local/Temp/ird-policy-review-20261028/initial-status.txt)、[收口现场](C:/Users/zgl18/AppData/Local/Temp/ird-policy-review-20261028/final-state.txt)、[状态差异](C:/Users/zgl18/AppData/Local/Temp/ird-policy-review-20261028/status-comparison.txt)、[版本转移核对](C:/Users/zgl18/AppData/Local/Temp/ird-policy-review-20261028/version-transition.txt)分别保存。起始状态记录只能证明路径状态，不能据此声称既有修改逐字节未变。
- 静态范围：policy 全部 **12 个公共头、12 个生产 cpp、CMake**，并检查 17 个测试 cpp、替身头、测试注册和数据集入口；补查实际消费者及所调用上游符号。逐文件路径、行数与 SHA-256 在 [源码清单](C:/Users/zgl18/AppData/Local/Temp/ird-policy-review-20261028/source-inventory.json)。跨单元只追本单元相关调用链，没有重审全部前序单元。
- 所有新增评审产物均在 `C:/Users/zgl18/AppData/Local/Temp/ird-policy-review-20261028`，未写入仓库。未建分支/工作树、未用子智能体、未执行远程命令。未改框架或 vcpkg。附件确认后的操作显式指定 C: 工作树；读取附件前曾只读查询 D: 的 git 状态，没有读取其源码或引用其构建结果，该查询不作为评审证据。

本轮读取并执行适用的根、源码区、设计区 AGENTS；用户指定 C: 分支覆盖旧 D: 串行调度条款。上下文限于 policy 相关需求/架构/策略文档、迁移 §13、功能矩阵、T36 契约及其真实独验。**T36/F-961 已关闭，不重复报缺陷**：独验绑定 `45e3e80ea08fd65e564501212347cfdc9d9a356b..d8508b1ccd4cb3e033e0638e277aa36f0000e137`，不能把这次名称适配验收扩大为整个 policy 的算法验收。

## 2. 业务与真实数据流

| 环节 | 当前代码与实际行为 | 责任和消费边界 |
| --- | --- | --- |
| 字节取得 | [HostEngineeringPolicy.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/HostEngineeringPolicy.cpp:52) 的 RevisionBytesSource 先核对冻结修订的 `(oid,cv)`，再委托 project 查询；[PolicyPort.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/policy/src/PolicyPort.cpp:170) 通过 IPolicyBytesSource 取字节 | project 管存储/修订，宿主只适配冻结闭包；worker 必须先物化同键稳定字节 |
| 解码、解析与发布 | PolicyCodec 解码 → resolvePolicy 七段校验/缺省/单位归一/规则检查 → PolicyCodec 语义 canonical 编码 → core ContentDigester → EngineeringPolicySet 发布；成功失败都按 `(oid,cv)` 记忆化 | policy 计算语义身份；core 供单位和摘要；发布对象 const 成员、按值持有容器，不暴露外部可变别名 |
| 默认、显式与不适用 | SystemDefaultPolicyProvider 只处理保留默认身份；真实策略由 PolicyProvider 解析。UI 修订中没有策略对象才用默认，多对象或坏编码失败。默认工厂关闭碰撞、有限旋转行程默认 4π；近限位等未冻结阈值保留 nullopt | 默认值不从分析入口覆写；阈值未设产生 NotApplicable，碰撞关闭为 CollisionDisabledByPolicy，空对象对为 EmptyScope，均须与“检查后无发现”区别 |
| 模型、名称与邻接 | [kinematics/Collision.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/kinematics/src/Collision.cpp:154) 从 runtime CanonicalModel/共享 WorkCell 投影链、工具、场景、相邻连杆和工具安装对；名称及内容身份由名称上下文转发 | runtime 是模型/名称 owner；policy 不重建运动链、不重复施加安装旋转。当前投影将所有 canonical scene 视作 EnvironmentObject，见待裁决 C3 |
| 作用域和过滤 | CollisionEvaluationSession 构造时按角色矩阵及启用域生成对象对，Object/Role 展开，规范化无序对；展开后的 excluded/mandatory 交集拒绝；mandatory 优先于默认相邻过滤。具体 scope 列表驱动逐对后端查询 | ProximitySetup 是可追溯构建产物；生产 evaluate 并未直接交给 CollisionDetector 使用，而是用同源 scope 跳过过滤对，不能仅凭 setup 内容认定结果正确；Group 当前保守拒绝，见 C2 |
| q 到后端 | [CollisionQuery.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/policy/src/CollisionQuery.cpp:498) 拷贝 baseState 或默认 State → 主设备 setQ → 上游 Kinematics::worldTframe → CollisionStrategy::inCollision；有距离需求时调用 DistanceStrategy | State 与查询数据是每次调用局部值；模型只借持。当前 evaluator 的共享 strategy 隔离有缺陷，见 F01；没有自写碰撞数值核心 |
| 发现、覆盖、来源 | 上游 bool 决定 Collision；有限距离 `< clearance` 决定 SafetyMarginViolation；距离失败不填伪 0，穿透深度保留 nullopt。输出绑定 policy/scene/name-map 身份，过滤、计数、诊断及最小距离分别承载；session 身份还绑定后端描述 | evidence 负责正式结果/证明资格，diagnostics 管正式稳定码，execution 管运行上下文、调度、取消及接纳；policy 的 Completed/finalized 不能代替这些判定 |
| 真实消费与接线局限 | modeling CommandHandlers 真调 JointLimitEvaluator，只消费 Completed+finalized 的 TravelLimitExceeded；kinematics 库有 PolicyCollisionSessionAdapter；trajectory Planner/Recheck 真调用传入的 policy session | **当前 UI 运动学生产入口实际使用 runtime::RuntimeCollisionSession，见 F02**。优化 Constraint.cpp:731 只调 IStaticCollisionProbe，生产源码未找到其具体 policy 适配实现；不能把标签为 optimization 的 ConsumerStub 当作生产接线证据 |

普通字面名（点号/连字符）走上游既有转义；仅对会触发上游 REGEX 分支的名称作单点适配。该薄适配没有新增第二套规则引擎。隔离真实后端控制例同时确认相切、分离及显式排除，说明 setup 未被直接消费本身不是本轮漏检根因。

## 3. 已确认问题，按严重度排序

P1 表示应优先修复的判定/接纳风险；P2 为明确的输入边界、数值可靠性或证据完整性缺陷。动态探针断言按正确契约编写，FAIL 是当前问题的证据，不能计作回归通过。共同原始静态摘录见 [source-excerpts.txt](C:/Users/zgl18/AppData/Local/Temp/ird-policy-review-20261028/source-excerpts.txt)；每项另列运行证据或明确注明未运行。

### F01 · P1 · 同 evaluator 的独立场景串用几何，真实后端漏检（运行确认）

**位置与符号：** [RobWorkCollisionEvaluator.cpp:60](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/policy/src/RobWorkCollisionEvaluator.cpp:60) 的构造及 `createSession`（82–93）把同一 m_strategy 传给全部 session；[CollisionQuery.cpp:309](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/policy/src/CollisionQuery.cpp:309) 的 `buildEvaluationHalf` 以 hasModel(frame) 跳过注册。上游 [FrameMap.hpp:74](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWork/src/rw/kinematics/FrameMap.hpp:74) 以 Frame::getID 索引，ID 仅在各 StateStructure 内有效，并非跨 WorkCell 身份；ProximityStrategy.cpp:48–74、107–115 正在使用这张表。

**契约、最小输入与结果：** policy §6.5/§9.3 要求共享 evaluator 的任务会话正确隔离，session 身份绑定场景；ARC-05/NFR-COR-05 要求判定真实一致。两个独立 WorkCell 同布局，Tool frame ID 均为 3，场景身份不同。先查询边长 0.2 m、中心轴向间隔 0.3 m 的小盒，再在同 evaluator 查询边长 0.8 m、同间隔的大盒。独立 oracle 只是轴向区间：小盒分离，大盒相交。实际 `small=0, reused-big=0, fresh-big=1` 个发现。场景身份改变没有使后端几何切换。

**影响与证据：** policy 端口全部共享 evaluator 的消费者可能得到 false negative，且 provenance 仍标新场景；当前 UI 的旁路另见 F02，未声称已通过 GUI 重现。`PolicyReviewProbe.CrossSceneGeometryMustNotReuseFrameIds`，原始 [probe.log](C:/Users/zgl18/AppData/Local/Temp/ird-policy-review-20261028/probe.log)、probe.xml、probe/ird-test-report.json，源码 [PolicyReviewProbe.cpp](C:/Users/zgl18/AppData/Local/Temp/ird-policy-review-20261028/PolicyReviewProbe.cpp:56)。

**最小修复/回归/上游边界：** 保留唯一 evaluator 与上游 CollisionStrategy；让后端几何注册及其缓存按任务会话或受控编译场景隔离，不能以 Frame ID 或几何字符串 ID 充当跨场景缓存键。补不同尺寸、相同局部 ID、反向交错查询、场景析构后新建及并发会话回归。不得修改上游 FrameMap，也不得为此让 policy 成为模型 owner。回滚只涉及 evaluator/session 后端资源装配及相关测试/凭证；接口或所有权方案若变化须先由所有者批准。

### F02 · P1 · 当前运动学宿主走另一碰撞路径，mandatory 和作用域语义未完整落实（静态确认）

**位置与符号：** [KinEvaluationChannel.cpp:663](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp:663) `makeRuntimeCollisionSession` 只投影 Object PairRule，不转 enabledDomains、Role/Group；695 行调用 runtime 的会话。`PolicyAwareIkSolver::solve` 720–725 及覆盖/批量入口 783、924、1013 行使用它；UiPlugin.cpp:3597 装配该 solver。[runtime/CollisionSession.cpp:169](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/runtime/src/CollisionSession.cpp:169) 先无条件过滤相邻对；mandatoryPairs 虽存入 rules，evaluate 全部代码没有读取该字段。

**契约、最小输入与结果：** policy §7.1 相邻连杆/工具安装对在 mandatoryPairs 声明后必须检查；§7.2 不得过滤隐藏必检；ARCH §7.5 要求 policy 唯一共享实现。最小输入是两个相交的相邻连杆、`excludeAdjacentLinksByDefault=true` 且该对为合法 Object mandatory。预期保留该碰撞；代码路径会在 runtime 181–182 行继续而丢弃该对。若仅启用 Environment，runtime rules 中也根本没有域字段限制 Self。这是完整读码确认的规则丢失，**本轮没有执行该 UI 路径或动态证明其具体几何输出**。

**影响与证据：** 当前运动学 UI 的单点、覆盖、批量及内联 IK 入口；与 policy session/轨迹使用的规则可能不同。mandatory 相邻对存在漏报风险，域/Role 丢失亦可造成额外碰撞或误拒绝。证据为 source-excerpts.txt 中完整宿主翻译和 runtime evaluate，以及 runtime/CollisionSession.hpp:59 的公开 mandatory 契约。不是 policy 核心的数值算法错误，责任为 **宿主装配＋runtime 碰撞设施＋policy 接缝**。

**最小修复/回归/上游边界：** 先由所有者裁决 MA-02 与唯一实现的整合方式（C1）。候选为宿主经已有 policy 会话入口统一规则和诊断，保留 runtime 的模型只读供给，待消费者迁完删除重复规则执行；不得简单在 runtime 再补一份 Role/域引擎。用同一真实快照比较生产宿主与 policy：mandatory 相邻对、启用域、工具安装、Role 排除、无几何/空域、正间距失败、对象 ID 对。几何 oracle 必须独立。保留上游 CollisionDetector/PQP 或 policy 后端的合规复用，不私写几何核心。涉及公共接线/历史批准边界，须批准后实施和独验；回滚以宿主接线及被替代会话整包为界，不能只撤头文件。

### F03 · P1 · 非有限 q 与路径参数可成为 Completed/finalized 输出（运行确认）

**位置与符号：** [CollisionQuery.cpp:161](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/policy/src/CollisionQuery.cpp:161) `validateQueryShape` 只查 kind 形状/维度；501 行 setQ，568–570/615–618 行复制 pathParameter，708–710 行终态化。

**契约、最小输入与结果：** NFR-COR-03 与 policy §7.5 不允许非法数值默认通过。真实 RevoluteJoint、一维 q=NaN 或 +Inf、分离盒场景：实际均 `status=Completed, finalized=true, findings=0`。另将相交盒的 PathSequence 参数设 NaN，实际得到 `finalized=true, findings=1, pathParameter=NaN`。预期应在入口按调用方违约拒绝，或走明确定义的 Failed 轨，不能产正式终态。

**影响与证据：** 直接接收求解/采样值的 policy 会话及使用路径位置的轨迹/证据消费者；某些上层可能先校验，不能以此替代公共边界。真实 RW 后端的 `NonfiniteConfigurationsCannotFinalize` 见 probe.log；`NonfinitePathParameterCannotEnterFinalizedFinding` 见 [extra.log](C:/Users/zgl18/AppData/Local/Temp/ird-policy-review-20261028/extra.log)、extra.xml、extra/ird-test-report.json。未将有限性问题归咎上游 setQ。

**最小修复/回归/上游边界：** 保留现有形状校验，补所有 Q 元素、路径参数的有限性；统一错误分类，不自行规定此前未冻结的路径取值区间/排序。覆盖 NaN/±Inf、多样本中间项和合法端点；拒绝后无 finalized 数值结果。继续用上游状态和碰撞 API，不新增运动学。回滚限 CollisionQuery 边界、诊断映射及相关回归；沿用既有失败语义不需新核心批准，若更改公共错误轨则需裁决。

### F04 · P1 · 执行中上下文失效后仍终态化（运行确认，端口协议）

**位置与符号：** [CollisionQuery.cpp:421](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/policy/src/CollisionQuery.cpp:421) 的 `evaluate` 仅在开始读取 ctx.alive；后端返回、后续样本及最终 708–710 行均不复核。

**契约、最小输入与结果：** policy §6.1:496 明确 shared WorkCell 保活与运行上下文有效性是两回事；§7.5:700 失效应 Failed＋CONTEXT-EXPIRED。探针开始 alive=true，在后端查询中改为 false；实际 `alive-after-query=0, finalized=1`、Completed。预期失效不得产生正式终态。

**影响与证据：** 能在运行/快照结束时撤销上下文的 execution/worker/业务适配器；可能产生迟到结果。当前 kinematics/trajectory 若使用 alive恒true 的适配器，无法表达这类失效，见 execution 提醒。`ContextExpiredDuringBackendCannotFinalize`，probe.log/xml/report；使用替身触发生命周期变化，**不作为真实几何数值证据**。开始即失效的现有测试已通过，与本缺口不同。

**最小修复/回归/上游边界：** 在确定的样本边界及正式发布前复核有效性；后端结束时失效必须失败，最后接纳仍由 execution 复验。保留取消的样本边界语义，未要求每个对象对都检查取消。补等待后端期间失效、最后一个样本失效、存活正常轨，必要时与 execution 做受影响契约回归。复用既有上下文端口，不建调度平台；回滚限 evaluate 生命周期处理和端口回归。若加强线程可见性/接纳语义，先共同冻结契约，不改变 runtime owner。

### F05 · P2 · 空 mesh 被拒收后仍被计作有效几何且完成检查（运行确认）

**位置与符号：** [CollisionQuery.cpp:314](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/policy/src/CollisionQuery.cpp:314) 的 `buildEvaluationHalf` 忽略 addModel 结果，仅用 hasModel 认定 registeredGeometry；上游 ProximityStrategy.cpp:48–61 先创建模型却忽略单个 addGeometry 返回值，[ProximityStrategyRW.cpp:82](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWork/src/rw/proximity/rwstrategy/ProximityStrategyRW.cpp:82) 对 0 三角形返回 false。

**契约、最小输入与结果：** KIN-05、policy §6.1 hasCollisionGeometry/§7.5:698 要求双端有效几何计数，无几何不得伪装已检。把一端 GeometryData 设为空 PlainTriMesh，scene 声明有几何。实际 `in-scope=1, with-geometry=1, evaluated=1, findings=0, finalized=1`，预期至少该对不计为双端有效几何并显式给缺口，或构建失败。

**影响与证据：** policy 对不一致/后端拒收的编译输入给出错误覆盖，消费覆盖的业务可误认为足够。当前 runtime mesh 载入有空网格跳过，未证明正常 runtime 编译会产生这个最小输入，故按边界可靠性列 P2，不冒称正常 UI 已漏检。`PolicyReviewExtra.EmptyBackendMeshCannotCountAsValidGeometry`，extra.log/xml/report，真实内置 RW 后端。

**最小修复/回归/上游边界：** 保留上游模型转换；通过既有上游查询/返回信息确认模型实际有可用几何，不能把模型容器存在当作接收成功。若 API 不足，先给缺口证据并申请最小扩展，不直接改框架。补空mesh、全部拒收、部分成功、合法几何及覆盖断言。模型有效性事实仍由 runtime/上游供给，policy 负责适配核验；回滚限几何登记/覆盖及回归。

### F06 · P2 · 非有限关节限位被归为可确认行程超限（运行确认）

**位置与符号：** [JointLimits.cpp:391](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/policy/src/JointLimits.cpp:391) `JointLimitEvaluator::evaluate` 对有限关节只查 qmin<qmax；421–430 行把差值作为 TravelLimitExceeded。

**契约、最小输入与结果：** MDL-06、JointLimits.hpp 的“有限限位”和 policy §7.5 的非法数值契约。qmin=-Inf、qmax=1 rad、q=0：实际 Completed/finalized，只有 TravelLimitExceeded(actual=Inf)，没有非法区间诊断。预期属于结构/非法输入轨，而非可由工程确认放行的阈值超限。NaN 可能落 IntervalInvalid，不等于 ±Inf 都被正确识别。

**影响与证据：** JointLimitEvaluator 的公共消费者；modeling 的 [CommandHandlers.cpp:336](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/modeling/src/CommandHandlers.cpp:336) 将 TravelLimitExceeded 变 ConfirmableFinding，但其自身前置硬断言另会拦非法输入，**本轮没有证明已绕过建模硬断言**。`JointNonfiniteEndpointsMustNotBeConfirmableTravel`，probe.log/xml/report。

**最小修复/回归/上游边界：** 先检查非 continuous 限位端点有限性，再做区间与行程检查，按既有硬错误职责产诊断或 Failed；不把正常超 4π 改成不可确认。覆盖 ±Inf、NaN、单端缺省、合法端点和 continuous 工程范围。此处是业务标量判定，无上游几何/运动学替代需要；保留 core 单位能力及 modeling/project 硬断言 owner。回滚仅限位校验/诊断/回归；新错误分类须与对端明确，不放宽阈值。

### F07 · P2 · 有限限位输入的计算溢出/下溢仍发布错误值（运行确认）

**位置与符号：** [JointLimits.cpp:421](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/policy/src/JointLimits.cpp:421) 的行程相减、438 半宽、445–446 裕量比，以及485–488 的 Provided 发布。

**契约、最小输入与结果：** policy §7.4 规定 r=距限位余量/半宽、§7.5:696 溢出应 Failed。区间 [-1e308,1e308]、q=0，实际 travel=Inf、r=0、finalized=true；数学上处于中点，r=1。区间 [0,denorm_min]、q=0，半宽舍入为 0，实际 Provided(r=NaN)，仍 finalized。预期使用可表示的稳定计算或明确失败，不能给错误的有限值或 Provided NaN。

**影响与证据：** 裕量/近限位指标、工程比较及消费者排序，触发依赖极端输入，未证明普通模型普遍受影响。`JointFiniteArithmeticCannotPublishNaN`，probe.log/xml/report。core::SourcedValue<double>::provided 是来源载体，不负责保证所有 double 有限，根因在 policy 计算。

**最小修复/回归/上游边界：** 统一同一限位数值能力，消除不必要的中间溢出/半宽下溢，并在所有派生值输出前检查；无法表达的行程必须走失败轨，不能截断/夹到阈值或放宽容差。补中点对称、正负极值、subnormal、闭区间端点及正常4π边界。保留单位/来源载体，不新建核心数值库；回滚限该算法及回归。核心数值修复须受影响消费者和独立验收。

### F08 · P2 · 未知 enabledDomains 枚举可发布并退化为 EmptyScope（运行确认）

**位置与符号：** [PolicyParsing.cpp:775](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/policy/src/PolicyParsing.cpp:775) 只拒绝空域；797–801 按底层值归一。PolicySet.hpp:490–530 的 CollisionRules::make 也没有域词表复检；CollisionEvaluator.cpp:711 自然无法命中未知域。

**契约、最小输入与结果：** policy.md:389 明确未知 enabledDomains 是非法实例。直接 RawPolicyInput.enabledDomains={CollisionDomain(99)}，其余合法：实际 published=1；随后 Completed/EmptyScope/finalized。预期解析不发布，公开发布门也不接受未知枚举。

**影响与证据：** 直接 Raw API 的宿主/导入/工厂；Codec 字节解码另有枚举校验，不能把这个缺口描述成所有文件导入均可触发。EmptyScope 在已检查的业务适配器会转数据不足，**不声称当前 consumers 将它认定安全**。`InvalidDomainCannotPublishAnEmptySafeScope`，probe.log/xml/report。

**最小修复/回归/上游边界：** 对既有4域词表作单一有效性核查，解析及发布门复用，错误使用既有 ApplicabilityInvalid。补直接 Raw、工厂、decode 三入口一致性，以及禁用碰撞下未知域规则的明确处理。不写第二套规则展开。回滚限枚举校验、相关测试及凭证；沿用已规定的未知域拒绝不需新语义批准。

### F09 · P2 · 未知查询 kind 绕过形状校验（运行确认）

**位置与符号：** [CollisionQuery.cpp:164](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/policy/src/CollisionQuery.cpp:164) `validateQueryShape` switch 无 default，202 行直接离开分支。

**契约、最小输入与结果：** CollisionQuery 公共接口仅定义 SingleState/PathSequence/SampleSet；非法调用应 QueryInvalid fail-fast（CollisionEvaluator.hpp evaluate 前置及现有形状测试）。对合法一维 q 设置 CollisionQueryKind(99)，实际没有 PolicyError，仍进入普通评估；预期拒绝未知 kind。

**影响与证据：** 直接查询/进程边界的枚举解码接缝，当前正常枚举调用不受影响。`InvalidQueryEnumCannotBecomeCompleted`，probe.log/xml/report。该探针直接断言拒绝，未将未知 kind 的某一业务解释强加给接口。

**最小修复/回归/上游边界：** 在既有形状校验拒绝未知值，与 F03 同包；补未知 kind＋空/非空 configurations，验证未调用后端。无需上游改动、新依赖或权限变化。回滚仅入口校验与回归。

### F10 · P2 · 首个间距发现短路时丢失已测最小距离（运行确认，距离端口）

**位置与符号：** [CollisionQuery.cpp:625](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/policy/src/CollisionQuery.cpp:625) `evaluate` 在 MarginViolation 后 break，跳过631–635 的 sampleBestDistance 更新，640–646 输出空 summary。

**契约、最小输入与结果：** CollisionQuery/§6.2 的 requestMinDistance 要求报告实际已检查有效对的最小距离。单个已测对距离0.01 m、clearance0.02 m、requestMinDistance=true、stopAtFirstFinding=true：实际 finding.measuredClearance=0.01，但 minDistances[0].minDistance=nullopt。预期保留已测0.01及对象对；未要求提前终止时给全域最小值。

**影响与证据：** 显式请求距离摘要的 policy 消费者；内置 RW 后端没有 DistanceStrategy，正间距当前会失败，故此问题是可注入距离后端/后续真实距离接线的明确协议缺陷，未冒称真实 RW 已计算0.01。`FirstMarginFindingRetainsMeasuredMinimum`，probe.log/xml/report，使用 ScriptedDistanceBackend。

**最小修复/回归/上游边界：** 先累计本次已测值再短路，保持部分覆盖语义。补首对/后续对短路、等阈值、正常遍历和距离失败；不虚构相交时的有符号距离或穿透深度。不扩展距离核心；回滚仅样本摘要更新和回归，新增真实距离后端另需批准。

### F11 · P2 · coverage.excluded 始终为空（运行确认）

**位置与符号：** [CollisionQuery.cpp:402](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/policy/src/CollisionQuery.cpp:402) `evaluate` 仅填 out.appliedFilters；[CollisionQuery.hpp:232](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/policy/include/sdurws/ird/policy/CollisionQuery.hpp:232) 却公开承诺 coverage.excluded 全量记录。

**契约、最小输入与结果：** PairCoverageRecord:220 与 policy §7.2③ 的过滤可追溯契约。一个真实生效的显式排除关系：实际 appliedFilters有1条，coverage.excluded为空。预期两处承诺一致；不能只靠 pairsExcludedByRule 数量找回规则与理由。

**影响与证据：** 消费 coverage 值的证据/序列化适配；生产源码定向检索未发现当前对 coverage.excluded 的直接读取，现有 appliedFilters 仍保留正确记录，所以这是证据载体缺口，非过滤执行失效。`CoverageCarriesActuallyAppliedFilters`，probe.log/xml/report，真实后端会话。

**最小修复/回归/上游边界：** 保留单一过滤结果生成点，同源填充承诺字段；若希望删除重复字段必须先变更公共契约，不能自行删。覆盖显式排除、默认相邻、空域及提前退出全量记录。复用上游结果与现有 scope，不另造 envelope。回滚限字段填充和回归；改变结构/消费者须批准。

### F12 · P2 · policy 建议稳定码与正式注册表不一致（静态确认）

**位置与符号：** [Diagnostics.hpp:182](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/policy/include/sdurws/ird/policy/Diagnostics.hpp:182) `policyDiagCode` 为29码；[diagnostics/DiagCodes.cpp:333](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/diagnostics/src/DiagCodes.cpp:333) 内置 policy 表为23码；[Factory.cpp:92](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/diagnostics/src/Factory.cpp:92) `DiagnosticsFactory::make` 对未注册码抛 CodeUnknown。

**契约、最小输入与结果：** policy §9.6:971–992 声明表尾补登并供正式注册表核对，码值唯一权威仍在 diagnostics。精确差集为：`POLICY-THRESHOLD-REQUIRED-MISSING`、`POLICY-POLICY-OBJECT-INVALID`、`POLICY-ENCODING-INVALID`、`POLICY-PORT-ASSEMBLY-INCOMPLETE`、`POLICY-OBJECT-MISSING`、`POLICY-CLL-QUERY-INVALID`、`POLICY-JNT-ENGINEERING-RANGE-INVALID`。最后一个在正式表中写成 `POLICY-ENGINEERING-RANGE-INVALID`。例如 Provider 的对象缺失/坏编码确实能构造前述码；将该记录交正式工厂会被拒。此结论由表和分支静态确认，**本轮未运行 diagnostics 工厂探针或 GUI 文案链**。

**影响与证据：** policy→diagnostics 正式诊断接缝；正式稳定码翻译、类别/严重度和展示的责任在 diagnostics，不能由 policy 自建另一注册表补洞。已核对 ui/app/DiagnosticsAssembly.cpp:42–48：装配只注册内置表和 ui 描述符后 seal，未补上述 policy 码。原始差集在 [diagnostic-code-diff.json](C:/Users/zgl18/AppData/Local/Temp/ird-policy-review-20261028/diagnostic-code-diff.json)，表及工厂摘录在 source-excerpts.txt。当前哪些UI路径直接显示 core记录、哪些经正式工厂，须在修复时按真实接线核对，不能一概断言所有错误都会崩。

**最小修复/回归/上游边界：** 由 policy/diagnostics 共同确认规范拼写、历史兼容与分类，再补正式注册/翻译和逐码对端测试；不得粗略将所有码注册为同一类别，也不擅自改变 INFO 的既有归类。保留 core DiagnosticRecord 和正式 Registry owner，不让 L2 反向直链 diagnostics。回滚为建议码/正式表/翻译/测试的兼容整包；稳定码变更需所有者确认，测试不只比较本单元29项镜像。

## 4. 未验证疑点、契约裁决与可选维护

### 4.1 有具体依据但尚待验证

| 编号 | 依据与疑点 | 必要下一证据；当前结论边界 |
| --- | --- | --- |
| S1 | PolicyPort.cpp decode 后未核对 input.policyObject 与请求对象一致，也未在 policy 重算对象字节版本 | RevisionBytesSource/project 有内容编址前提，不能用一个故意撒谎的替身直接认定生产缺陷。需通过真实 project 导入/clone/write 路径证明错 oid 字节能合法进入该键；责任与 project C4区分 |
| S2 | 发布门可接收调用方给出的非零 contentIdentity；生产 resolvePolicy 会重新算语义身份 | 需核实是否有生产调用者绕过解析且传错身份。测试使用人工身份并非生产证据，暂不要求每次发布重哈希 |
| S3 | 其他公开 enum 的 Raw/工厂路径、异源 baseState 及非有限变换的纵深边界 | 已确认仅 enabledDomains/queryKind（F08/F09）；未对每个 origin/mode/ruleLevel/ScopeTargetKind 全面注入，不能把未跑项并入已确认数量 |
| S4 | 同 evaluator/session 并发、长时间锁等待时取消/失效的响应界限 | 代码的 State、ProximityStrategyData 为调用局部，后端注册/查询有共享mutex；F01 是缓存语义错误，不自动等于数据竞争。本轮未做压力、TSAN或取消延时性能试验 |
| S5 | 重叠 mandatory 规则给同一对象对不同 level 时，map emplace 保留首条 | 规则强弱优先契约不明确；先冻结语义，再测输入顺序不应影响的情形，不能自行设计优先级 |

### 4.2 需要所有者裁决的事项

**C1 · 唯一碰撞实现与 MA-02。** ARCH §7.5:574、ARC-05 要求 policy 统一；runtime.md v0.28 与 `traceability/builds/wp13-t28-ma0102/README.md` 又明确登记了所有者立项的 runtime/PQP 会话和宿主接线。因此不能把“存在runtime设施”本身简单判成未经批准，也不能用历史批准豁免 F02 的实际规则丢失。需选定保留 policy 实现并迁宿主、把合法后端通过受控端口接入 policy，或有证据地修订唯一实现边界；本轮不实施任何选项。优先避免两处重复执行规则。

**C2 · Group。** policy §7.2:661 要求组成员进入身份，Raw schema 却无组成员载体；context.groupDefined=true 能让解析发布，但 session 遇Group保守拒绝（CollisionEvaluator.cpp:435–440 已登记 v0.5缺口）。UI当前 groupDefined=false 不会把组默认为空。需决定暂不支持并前置统一拒绝，或批准 schema/上下文/身份完整演化；不得只修 session 去猜组成员，亦不把历史已登记缺口当新完成项。

**C3 · Workpiece/Payload 与实际场景供给。** policy 角色矩阵与合成夹具包含这些角色；当前 kinematics::assembleCollisionScene 把 canonical scene 一律投为 EnvironmentObject，runtime CanonicalSceneObject/SceneObjectDescription 无角色字段，亦无独立 Payload 碰撞投影。工具会入清单并有安装相邻事实。需要 runtime/宿主/policy 共同确认角色真值通道及当前产品承诺；不能只看 policy 枚举就宣称真实工件/负载已全覆盖。公开模型/身份演化须批准，不能在policy猜名字辨角色。

**C4 · 距离能力。** 内置 ProximityStrategyRW 仅继承 CollisionStrategy；policy 动态检查 DistanceStrategy。正clearance即使 requestMinDistance=false 仍强制距离，缺能力按 Failed/DetectorUnavailable 结束，符合不默认安全。当前真实正间距与最小距离能力存在缺口；距离边界测试主要是替身协议。若要真实功能，需要上游检索/后端候选与批准，不能伪造 signed distance、用公差扩大碰撞、或把 requestMinDistance=false 当关闭间距检查。

**C5 · Codec 非规范布尔字节。** 已运行 `NonCanonicalBooleanMustBeRejected`：把有效编码的 enabled字节1改2，decode仍为true；PolicyInput.cpp:834/837/845用`u8()!=0`。编码器只写0/1，但 policy §5.3 明确的严格字节约束是 presence，尚未找到“所有非0/1 bool必须拒绝”的冻结契约。本探针 FAIL 只证明实现行为，不足以自动认定违反现有decode契约。若决定严格拒绝，先核对历史字节兼容、对象字节版本与语义身份，不能以此静默重写project内容。本项不计入12项已确认缺陷。

**C6 · coverage 的样本粒度。** pairsEvaluated 的公开口径是某对至少查询1次，不是“每个样本的每一对都检完”。early exit仍Completed是已定义行为。不能依据该计数相等就推断完整路径覆盖；如需跨进程证明精确样本覆盖，应由 evidence/execution 联合明确承载，不自行扩接口。本轮没有将合法early exit状态当缺陷。

### 4.3 可选维护性改进

- Policy.cpp 只剩无外部引用的 policyUnitAnchor；CMake:47仍称“STATIC至少一个翻译单元”必须保留，但当前双模式都有多份真实TU。可在依赖/符号核查后删除锚点和过期说明，不与安全修复捆绑。
- PolicyProvider 的缓存有明确确定性用途，同键失败也缓存；冻结闭包、借持寿命和同键稳定前提写在公共头。UI当前每次解析构造短命Provider，没有“必然跨修订误缓存”的证据。进程级“版本集有界”描述不等于内存硬上界，可补生命周期/容量说明，避免为了简洁误删必要缓存。
- 范围规范化、角色词表与序列化排序有重复形态，但分别位于非法输入诊断、合法值发布、canonical身份和真实场景展开边界，不以代码长判过度设计。优先清理真正重复的 runtime 规则执行（C1）和公共重复字段契约（F11），不要把所有检查合并成一个信任边界。

## 5. 架构、复用与准确性结论

policy 生产实现保持 **L2零Qt**，仅通过既有core能力和上游WorkCell/kinematics/proximity工作，未发现跨单元私有头或自行计算碰撞几何核心。单位归一使用 core::convert；内容摘要使用 core::ContentDigester；相邻与安装事实由输入模型提供。WorkCell以共享只读引用保活，State和查询数据任务私有；这是合理依赖隔离，不能据此宣称运行上下文也有效（F04）。后端几何注册资源不是第二模型owner，但其跨场景隔离目前错误（F01）。

必要业务规则是适用域、角色矩阵、mandatory/excluded冲突、工程阈值和覆盖；必要薄适配是上游模式名、状态/对象映射、后端能力和错误转换；必要依赖隔离是字节/名称/验证/调用上下文端口。待删除的候选是无用锚点及消费者统一后多余规则执行，不能删除正确的业务层只为增加上游调用比例。

已核查真实后端返回bool决定产品碰撞发现；正间距不依赖request开关，距离非有限转Failed；穿透深度未伪造；普通阈值等号边界和闭区间端点已有基线覆盖。canonical身份按精确IEEE位模式、稳定排序、SI归一，排除policyObject/origin/审计字段；本轮未发现应以近似相等替代身份比较的依据。显式0m间距保留为0，不等于未设置；4π默认来源维持唯一权威。JointLimits当前服务旋转/continuous，modeling排除Prismatic/Fixed，MDL-12当前正式prismatic阻断，不把rad-only直接报作现阶段错误。

F-961 的 REGEX 薄适配已核对实际上游条件和独验，本轮不复开。`reuse/functions/policy.json` 中 evaluate/buildEvaluationHalf/JointLimits 等仍有pending；T36的done不豁免本轮功能审查。报告与既有绿色测试都不代表H1—H6债务已关闭，更不自动批准新核心、扩展或模型所有权变化。

## 6. 文件/功能覆盖与真实验证

### 6.1 静态覆盖

| 文件组 | 已检查内容 | 主要输出/限制 |
| --- | --- | --- |
| PolicySet.hpp；PolicyInput.hpp/cpp；PolicyParsing.hpp/cpp | 内联工厂、Raw与发布态、缺省、单位、数值、codec reader/writer/长度与尾随、canonical语义闭包 | F08；S1–S3；C2/C5。截断、尾随、坏枚举、浮点既有测试真实执行；>4GiB编码长度未构造，不夸大已验证 |
| PolicyPort.hpp/cpp；Contexts.hpp；SystemDefaultPolicy.hpp/cpp | 端口实际取数/错误转译/成功失败缓存、冻结闭包、默认保留身份、借持寿命 | 保留字节/上下文责任边界；系统默认接受任意expectedVersion是明确契约，未当版本漏洞 |
| CollisionEvaluator.hpp/cpp；RobWorkCollisionEvaluator.cpp | 场景校验、角色矩阵、邻接、过滤冲突、setup字面适配、身份、资源共享 | F01、F02；C1–C4；未把setup正确夸大为逐对结果正确 |
| CollisionQuery.hpp/cpp | Q→任务State→worldTframe→collision/distance、结果/覆盖/短路/异常/上下文 | F03–F05、F09–F11；真实几何与替身协议证据分列 |
| JointLimits.hpp/cpp | 形状、端点、continuous、闭区间、4π、近限位比、来源与失败轨 | F06/F07；限位q元素本身已有finite检查，未误报成也缺finite |
| Compatibility.hpp/cpp；Errors.hpp/cpp；Diagnostics.hpp/cpp | 逐字段兼容/原因token、错误码、诊断构造/单位/非有限呈现 | F12；未构造新注册表或改正式分类 |
| Policy.cpp；CMakeLists.txt | 符号用途、依赖/Qt/公共头、集成/冒烟分支、测试与_report注册 | 锚点候选；真实碰撞TU仅集成构建，冒烟不能代表完整碰撞链 |
| 全部17测试cpp、PolicyTestDoubles.hpp、测试README及数据集入口 | oracle来源、替身用途、字符串扫描、断言与注册、真实过滤器命中 | 见下表；本轮不是全库测试有效性认证 |

### 6.2 构建与执行结果

现有 C: `build/CMakeCache.txt` 的 CMAKE_HOME_DIRECTORY 指向本工作树 `RobWork`，`RWS_BUILD_INDUSTRIALROBOT:BOOL=ON`，VS2022/x64/Release。vcpkg工具链只读引用获准D:依赖；Qt6.11.1是缓存依赖，policy测试未引入GUI。配置摘录：[build-config.txt](C:/Users/zgl18/AppData/Local/Temp/ird-policy-review-20261028/build-config.txt)。

| 验证 | 匹配/总数 | passed | skipped | failed | notRun | 结果和原始证据 |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| 构建 sdurws_ird_policy_test | 1目标 | — | — | 0 | 0 | PASS；[build-policy-test.log](C:/Users/zgl18/AppData/Local/Temp/ird-policy-review-20261028/build-policy-test.log) |
| 构建 sdurws_ird_policy_contract_test | 1目标 | — | — | 0 | 0 | PASS；[build-policy-contract.log](C:/Users/zgl18/AppData/Local/Temp/ird-policy-review-20261028/build-policy-contract.log) |
| 正式policy单元exe，全量一次 | 214 | 214 | 0 | 0 | 0 | PASS；[policy-unit.log](C:/Users/zgl18/AppData/Local/Temp/ird-policy-review-20261028/policy-unit.log)、[XML](C:/Users/zgl18/AppData/Local/Temp/ird-policy-review-20261028/policy-unit.xml)、[per-exe报告](C:/Users/zgl18/AppData/Local/Temp/ird-policy-review-20261028/policy-unit/ird-test-report.json) |
| 正式policy契约exe，全量一次 | 23 | 23 | 0 | 0 | 0 | PASS；[policy-contract.log](C:/Users/zgl18/AppData/Local/Temp/ird-policy-review-20261028/policy-contract.log)、[XML](C:/Users/zgl18/AppData/Local/Temp/ird-policy-review-20261028/policy-contract.xml)、[per-exe报告](C:/Users/zgl18/AppData/Local/Temp/ird-policy-review-20261028/policy-contract/ird-test-report.json) |
| PolicyReviewProbe.* | 11 | 1 | 0 | 10 | 0 | FAIL红例证据；[probe.log](C:/Users/zgl18/AppData/Local/Temp/ird-policy-review-20261028/probe.log)、[XML](C:/Users/zgl18/AppData/Local/Temp/ird-policy-review-20261028/probe.xml)、[报告](C:/Users/zgl18/AppData/Local/Temp/ird-policy-review-20261028/probe/ird-test-report.json) |
| PolicyReviewExtra.* | 2 | 0 | 0 | 2 | 0 | FAIL红例证据；[extra.log](C:/Users/zgl18/AppData/Local/Temp/ird-policy-review-20261028/extra.log)、[XML](C:/Users/zgl18/AppData/Local/Temp/ird-policy-review-20261028/extra.xml)、[报告](C:/Users/zgl18/AppData/Local/Temp/ird-policy-review-20261028/extra/ird-test-report.json) |
| diagnostics/runtime/UI宿主的定向运行；并发/性能；独立smoke；治理全门禁 | — | — | — | — | — | **NOT RUN**；本轮未改实现，不机械扩大全库或GUI范围 |

本轮原始产物摘要见 [evidence-sha256.json](C:/Users/zgl18/AppData/Local/Temp/ird-policy-review-20261028/evidence-sha256.json)。两份正式XML与per-exe JSON一致；两组探针也分别一致，详见 [validation-summary.json](C:/Users/zgl18/AppData/Local/Temp/ird-policy-review-20261028/validation-summary.json)。探针共13项：1项正控制通过、12项按正确/候选契约断言失败；其中11项失败支持10个已确认动态问题，另1项为C5待裁决。另有F02、F12两项静态确认，故不能把“探针失败数”直接当“缺陷数”。

实际使用现有_report机制的自有main输出JSON，但没有调用会重复运行的_report目标。正式exe均为本C:构建树绝对路径：`C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/RobWorkStudio/bin/Release/sdurws_ird_policy_test.exe` 与 `sdurws_ird_policy_contract_test.exe`。隔离probe用 [build-probe.ps1](C:/Users/zgl18/AppData/Local/Temp/ird-policy-review-20261028/build-probe.ps1) / [build-extra.ps1](C:/Users/zgl18/AppData/Local/Temp/ird-policy-review-20261028/build-extra.ps1) 在VS x64环境，从本C: vcxproj取include/libs独立编译；编译日志与rsp保留，未改正式工程。可复现命令见 [commands.ps1.txt](C:/Users/zgl18/AppData/Local/Temp/ird-policy-review-20261028/commands.ps1.txt)，请使用新输出目录以保留本轮原件。

### 6.3 测试能证明什么

- **PolCollisionAnalytic 4例**使用真实makeRobWorkCollisionEvaluator，预期来自解析几何/数据集，非仅同一替身互比；安装姿态与环境世界系对照有价值。但算例的几何尺寸相同，共享evaluator跨WorkCell时掩盖F01，本轮增加不同尺寸且fresh evaluator对照。
- **PolicyContractDoubles 7例**含真实后端和真实Provider；所谓kin/trajectory/optimization三入口由同一个ConsumerStub结构模拟，能证共享端口行为，不能证当前UI或优化实际装配。另有ScriptedCollisionEvaluator四轨测试，只能证替身/协议。
- **PolicyProofMaterialContract 4例**明确不链接evidence实现、不调用validateProof，只镜像证明字段与材料清单，不能声称正式证明通过。EV-T13改动后仍须真实对端证明校验。
- **距离与间距边界**主要由ScriptedDistanceBackend驱动；可证比较方向、缺能力失败及F10，不能作为真实距离数值精度/性能验收。正间距后端能力缺口见C4。
- **源码字符串/BuildRedLine**可证结构、引用和编译边，不能代替运行。测试过滤器输出明确命中11和2项，并已对XML核对；没有把未命中当PASS。
- 正式集成构建包含CollisionEvaluator/CollisionQuery/RobWorkCollisionEvaluator/JointLimits真实实现；独立冒烟配置会省略这些框架相关TU，本轮未新跑smoke，不能声称双模式均已复验。

## 7. 按依赖排序的最小修复工作包

以下都是建议，尚未领任务、改接口、刷新正式矩阵或实施；每包稳定代码/测试后再一次性同步受影响设计与逐文件凭证，不刷新历史总报告。公共接线、核心数值与复用迁移须所有者另会话独验。

| 顺序/能力包 | 保留 / 删除 / 适配 | owner、接口与消费者影响 | 必要回归与批准 |
| --- | --- | --- | --- |
| WP-P0 统一真实碰撞入口（F02，C1/C3） | 保留runtime模型/名称真值和合规上游后端；迁移后删除重复规则执行；宿主适配到选定唯一policy判定路径 | 模型owner不变，policy继续负责规则。UI/kinematics、runtime会话公共面可能变化，逐消费者迁移，不能直接删仍在用的旧设施 | 先明确MA-02整合裁决和角色通道，再实施；真实宿主mandatory/域/Role/工具/无几何/对象ID对；需所有者批准＋独验，必要GUI生产路径测试 |
| WP-P1 后端模型隔离与几何覆盖（F01/F05） | 保留上游策略、任务私有State；删除跨场景错误复用；适配注册接收有效性 | runtime owner不变；优先内部资源调整，不扩大接口。影响全部共享evaluator消费者 | 不同尺寸同frameID交错＋空mesh/部分接收＋会话并发；上游/所有权/公共面改动先批准；否则属已授权语义内修复，仍需独验 |
| WP-P2 查询边界与生命周期（F03/F04/F09） | 保留形状/取消/失败轨；拒绝非有限和未知kind；适配alive复核 | policy入口与execution接纳共同负责；不新增调度器。公共错误语义需对齐；影响轨迹/运动学会话调用 | 非有限Q/s、未知枚举、运行中失效、正常/取消/最后样本；受影响对端契约回归；若改公共语义须裁决 |
| WP-P3 限位数值可靠性（F06/F07） | 保留4π和硬断言/可确认阈值区分；删除不稳定中间运算；适配finite/可表示性失败 | 硬断言owner仍modeling/project；policy提供判定。无新核心、无容差变化；影响JointLimit消费者与指标 | finite端点、溢出/下溢、对称中点/闭端点/continuous/默认4π，以及真实modeling确认边界；数值核心须独验 |
| WP-P4 发布域与结果证据（F08/F10/F11） | 保留规范词表、scope和现有载体；统一域校验，先累计距离再短路，过滤记录同源填充 | owner不变；优先不改接口。若删重复字段或增样本coverage，须与evidence/trajectory批准后演化 | Raw/工厂/codec一致、请求距离首个发现、过滤全量；距离部分明确替身/真实两类，能力缺口不虚报 |
| WP-P5 正式诊断对齐（F12） | 保留policy建议码与正式Registry分工；删除拼写漂移，补登记/翻译 | diagnostics是稳定码/分类owner；不引入policy→diagnostics编译边；历史兼容按规范决定 | 对29项逐码真实Registry/Factory正负例、错误轨和直接UI消费者；稳定码调整需所有者确认 |
| 独立后续能力（C2/C4/C5/C6） | Group/真实距离/布尔严格度/精确路径覆盖分别按缺口批准 | schema、公共语义或上游扩展均有独立影响，不能夹带到小修复 | 批准前只做检索/具体候选与审查，不实施新核心/扩展、不降低需求 |

每个包回滚到该包前源码、测试、必要设计/凭证即可；若涉及公开数据/稳定码，须保留已写历史字节与兼容解释，不以回滚擦除结果。F01/F05不应通过改框架实现，F03/F06/F07不应靠放宽容差解决。修复工作可先完成独立的P1/P2/P3边界包，P0裁决不应阻塞这些已明确的内部缺陷调查；迁移真正消费者时再按依赖收口。

## 8. 前序变更的实际影响与 execution 评审提醒

| 前序单元 | 已核对事实 | 对本轮的实际影响 |
| --- | --- | --- |
| core | 63e2b6305修身份播种、事件生命周期；6dc1141ae登记独验。未改policy用的convert/ContentDigester契约 | policy继续用唯一单位/摘要供给；本轮错误不归到core。JointLimit Provided非法值来自调用方计算，不能以SourcedValue来源包装当数值认证 |
| testkit | e7b9546b8修SetCheck/JsonLite/报告与验证边界，4c5e4301f登记独验 | 本次重建的policy目标使用这些实现，原生XML与报告计数一致；直接gtest红例没有依赖被测helper作oracle。旧assert镜像或metadata齐备仍不等于真实语义校验 |
| project | 开始有未提交PRJ-T19修改，收口外部提交59af1f9b4；修提交事实、恢复闭包、归档等，policy代码无变化 | RevisionBytesSource继续只转冻结(oid,cv)；C4真实policy语义身份仍在project记录待裁决，不能宣称由本轮解决。未重跑其全部实现/基准，也不自签该包独验 |
| evidence | 4afdc201a修门禁、输入身份和恢复兼容，412e173b9登记EV-T13独验；新切片v2、请求键等有真实对端证据 | policy proof材料测试仍未调用validateProof，不因前序独验就自动获得正式证明资格；运行结果必须与实际快照/切片/策略和后端绑定，不能仅凭finalized采信 |
| runtime | 近期运动学/动力学复用不直接改变本轮policy的setQ/worldTframe/proximity调用；当前源码仍保留MA-02 RuntimeCollisionSession | 模型/名称/编译owner边界维持；发现了宿主旁路F02及角色供给C3。未重新审RNE/FD，也未把其独验套用于碰撞；F01要修policy资源隔离，不去改runtime帧ID |

下一轮 execution 评审只需带入以下具体问题，不在本轮推进：

1. 找到真正创建PolicyProvider/evaluator/session的宿主与worker链，核对冻结字节、场景、名称和后端身份，避免ConsumerStub/类名掩盖F02那类旁路；优化碰撞探针缺具体生产适配也应按实际能力门控。
2. 生命周期必须是可撤销运行上下文；shared_ptr保活、alive恒true和进程仍在都不足以允许正式接纳。取消/失效/失败与碰撞分轨，锁等待和最后样本结束的接纳竞态需真实验证。现有kinematics AdapterCallContext及trajectory Planner/Recheck的alive恒true只表达同步调用假设，不能替代任务epoch/currentness。
3. 缓存键必须绑定实际策略语义身份、runtime场景/名称/模型、后端及样本/配置；键看似不同也救不了F01的底层资源串用。不要只核对上层key就认定判定没有跨任务污染。
4. worker边界只传字节和值，进程内重新物化冻结模型/名称/策略；不序列化WorkCell指针、借持句柄或session。Provider同键负缓存依赖服务前物化完毕和闭包应答稳定。
5. 接纳须调用真实evidence规则，区分Completed、finalized、Applicable、几何缺口及部分样本覆盖。空作用域、缺后端、没有发现、提前退出、正式不可行证明各不等价；proof镜像测试不能代替validateProof，PRJ-T19存储完成也不能代替工程资格。

在所列版本、范围和已执行验证内，未发现其他问题。

