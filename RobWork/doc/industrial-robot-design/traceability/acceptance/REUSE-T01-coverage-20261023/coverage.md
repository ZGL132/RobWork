# REUSE-T01 冻结覆盖审查

冻结提交：`bb997f1cfb89c8d8eb24b457d2c82d4c0711af44`。结论：**NOT PASS（仅全量覆盖退出条件）**。

220 条功能关系尚不足以证明全部20单元及基础构建/测试/脚本全面逐功能审计；本次定向抽样确认16组具体未登记功能。任务 audit-in-progress 与现状相符，不能据本次冻结版本宣称 T01 全量完成或准入 T02。

任务本身仍标注 `audit-in-progress`；这是尚未满足退出条件的覆盖审查，不是把局部门禁 pass 撤销。未关闭库存数量、pending/proposal 存在与否均未用作本结论的直接理由。T01 可容纳审计清楚的阻塞提案；库存与迁移最终关闭属于 T06。

## 范围与证据口径

全量解析固定1401文件库存与functions/*.json的sources/inputs/auditedSections；sourceReferenceIndex用于区段反查。
按风险定向阅读生产入口、隐藏helper、现有adapter、测试、黄金生成器及CMake；不是全部1401文件的语义穷尽审查。

核验依据为 REUSE-T01 acceptance[0]、audit-contract §2/§4 和 migration §12.5。每项缺口都反查了 sources 及 auditedSections；CMake 的 inputs 摘要不作为功能登记。所有路径/行号均指本目录 `repo` 的冻结源码；JSON 同时保存文件 SHA-256 与已登记区段计数。

## 全量定位统计

固定库存 **1401** 文件；**220** 条关系、**219** 个不同 functionId；sources 引用 **63** 个路径，其中生产60、测试3、构建0。以下不是语义覆盖率。

| 单元/组 | 库存文件 | 功能关系 | source引用文件 | 仅input绑定文件 |
| --- | ---: | ---: | ---: | ---: |
| core | 30 | 3 | 3 | 1 |
| diagnostics | 38 | 3 | 2 | 1 |
| drivetrain | 22 | 10 | 2 | 1 |
| dynamics | 61 | 8 | 4 | 1 |
| evidence | 37 | 26 | 6 | 1 |
| execution | 60 | 2 | 2 | 1 |
| foundation-governance | 33 | 0 | 0 | 0 |
| io | 37 | 12 | 2 | 1 |
| kinematics | 104 | 24 | 5 | 1 |
| modeling | 138 | 26 | 8 | 1 |
| optimization | 76 | 5 | 2 | 1 |
| policy | 43 | 8 | 3 | 1 |
| project | 87 | 2 | 1 | 1 |
| reporting | 66 | 1 | 1 | 1 |
| requirements | 92 | 11 | 4 | 1 |
| runtime | 57 | 29 | 6 | 1 |
| selection | 71 | 16 | 3 | 1 |
| testkit | 46 | 12 | 1 | 1 |
| trajectory | 81 | 18 | 5 | 1 |
| ui | 162 | 3 | 2 | 1 |
| workflow | 60 | 1 | 1 | 1 |

三份被引用测试路径实际为：core/test/TestMainReport.cpp、evidence/test/TestMainReport.cpp、kinematics/test/KinFkFixture.hpp。前两者为测试入口，第三者含三处 FK oracle；不代表其他测试文件或黄金生成器已审。

## 具体覆盖缺口（抽样下限）

### COV-01 [P1] 已有上游 RRT 适配及避障生产链未纳入逐功能矩阵

Planner.cpp 没有任何 sources 登记。其真实链既包括上游 RRT/QSampler/Metric 调用，也包括 policy 三态保守拒绝、全局随机源播种、路径复检和候选确定性比较。不能以已审 Smooth/Recheck 等相邻文件代替，也不能只审自写算法而漏掉既有 adapter。

源码定位：
- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/trajectory/src/Planner.cpp:161` — `PolicySessionQConstraint::doInCollision`；同文件已有 0 条 sources，本定位均不在已声明区段内。
- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/trajectory/src/Planner.cpp:360` — `sampleStraightPath`；同文件已有 0 条 sources，本定位均不在已声明区段内。
- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/trajectory/src/Planner.cpp:828` — `makePathPlannerAdapter`；同文件已有 0 条 sources，本定位均不在已声明区段内。
- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/trajectory/src/Planner.cpp:837` — `planPtpWithObstacleAvoidance`；同文件已有 0 条 sources，本定位均不在已声明区段内。

上游方向与边界：已定位 RRTPlanner.hpp:108/127 makeQToQPlanner、QSampler.hpp:114/153 makeUniform/makeConstrained、MetricFactory.hpp:377 makeEuclidean、Math.hpp:242 seed、QConstraint.hpp:156 doInCollision。产品 Planner.cpp:621/658/665/669/675 已调用这些能力；应登记并审权限、State/Device 来源、全局种子并发约束及有效返回值消费。

检索证据：`planner.stdout.log`、`up-planner-corrected.stdout.log`。

### COV-02 [P1] Pareto 支配判定、容差 helper 与分层排序未登记

文件无 sources。min/max 方向、以 b 为参考元的 C4 容差、去重保首见和非支配分层/稳定排序是实质功能，不是可随整单元带过的普通循环。

源码定位：
- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/Pareto.cpp:37` — `notWorse`；同文件已有 0 条 sources，本定位均不在已声明区段内。
- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/Pareto.cpp:50` — `strictlyBetter`；同文件已有 0 条 sources，本定位均不在已声明区段内。
- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/Pareto.cpp:65` — `dominates`；同文件已有 0 条 sources，本定位均不在已声明区段内。
- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/Pareto.cpp:94` — `ParetoFrontBuilder::buildFront`；同文件已有 0 条 sources，本定位均不在已声明区段内。

上游方向与边界：实际检索到 sandbox/optimization/Optimizer.hpp:31（其文档为多输入单输出极小化）及 rwlibs/pathoptimization 的路径优化器。它们不能据名称直接替换多目标业务 Pareto。需审 core::closeWithin 的共享语义及排序业务规则，记录不重叠证据；本次检索未证明可直接替代，也未批准新核心。

检索证据：`opt-pareto.stdout.log`、`up-optimization-corrected.stdout.log`。

### COV-03 [P1] 候选生成接口和邻域扰动链未由 LHS 随机 helper 条目覆盖

Generator.cpp 无 sources。第0代批语义、取消、预算、锁定/授权变量过滤、baseline 恒首与 OFAT 正负扰动在此实现；JointStrategy 随机小核登记不涵盖本文件消费与编排。

源码定位：
- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/Generator.cpp:39` — `SeededLhsGenerator::generateNextBatch`；同文件已有 0 条 sources，本定位均不在已声明区段内。
- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/Generator.cpp:110` — `BaselinePerturbGenerator::generateNextBatch`；同文件已有 0 条 sources，本定位均不在已声明区段内。
- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/Generator.cpp:248` — `generateWithBuiltInStrategy`；同文件已有 0 条 sources，本定位均不在已声明区段内。

上游方向与边界：上游已定位 QSampler 的范围采样与 sandbox 优化器；应分开判断可复用采样原语、现有 LHS helper 消费与确定性业务邻域策略。不得仅因无同名 LHS 类就认定全链 business-specific。

检索证据：`opt-pareto.stdout.log`、`up-optimization-corrected.stdout.log`、`up-planner-corrected.stdout.log`。

### COV-04 [P1] 功率能量梯形积分与无效区间归约未登记

PowerEnergy.cpp 无 sources。真实功能为逐关节有效行归约、正负功分项梯形积分、非 Ok 缺口零贡献、首峰保留、时间不可用 NaN 等；当前 dynamics 中 RNE/FD/Replay/RMS 条目不能覆盖这些统计语义。

源码定位：
- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/dynamics/src/PowerEnergy.cpp:63` — `PowerEnergyCalculator::compute`；同文件已有 0 条 sources，本定位均不在已声明区段内。

上游方向与边界：已检索 rwsim/dynamics/Body.hpp:350 与 RigidBody.cpp:99 calcEnergy（刚体动能/势能），以及 BodyIntegrator.hpp 的状态积分。它们和机械功率时间序列正负能量积分不同；需将可复用物理原语与业务时间/缺口规则分开记录，不能据存在 energy 符号声称可直替。

检索证据：`energy-envelope.stdout.log`、`up-integrals.stdout.log`。

### COV-05 [P1] 可达包络探针与近似搜索编排未登记

文件无 sources。位置子空间 IK 请求、种子/初值派生、取消和设施失败判读、搜索未果与不可达侧关系、方向探测/二分预算均需审；IK 求解器条目不等于该消费者已审。

源码定位：
- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/kinematics/src/Envelope.cpp:127` — `probeReachability`；同文件已有 0 条 sources，本定位均不在已声明区段内。
- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/kinematics/src/Envelope.cpp:217` — `computeApproximateEnvelope`；同文件已有 0 条 sources，本定位均不在已声明区段内。

上游方向与边界：已定位 Device.hpp:215 baseTend、Kinematics.hpp:53 worldTframe、geometry/AABB.hpp:32 等几何能力。普通包围盒不能替代 IK 可达集合估计；应核 runtime/IK 端口消费与近似语义，不把几何类存在或搜索无命中当结论。

检索证据：`energy-envelope.stdout.log`、`bounds-geometry.stdout.log`。

### COV-06 [P1] Xacro 受控展开及隐藏输出转义未登记

文件无 sources。文件已经使用 pugixml；这里的缺口不是自写 XML parser，而是受控标识符替换、表达式拒绝、宏/include/预算/来源定位及 XML 输出转义的功能判断。不能用 URDF 映射条目概括整条加载链。

源码定位：
- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/modeling/src/XacroExpand.cpp:172` — `escapeXmlText`；同文件已有 0 条 sources，本定位均不在已声明区段内。
- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/modeling/src/XacroExpand.cpp:191` — `escapeXmlAttribute`；同文件已有 0 条 sources，本定位均不在已声明区段内。
- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/modeling/src/XacroExpand.cpp:403` — `substituteText`；同文件已有 0 条 sources，本定位均不在已声明区段内。
- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/modeling/src/XacroExpand.cpp:933` — `XacroExpandService::expand`；同文件已有 0 条 sources，本定位均不在已声明区段内。

上游方向与边界：已定位 DOMParser.hpp:59/65 load、:71/77 save 及 DOMElem/loader；应比较 XML 结构读取/写出与既有 pugixml 使用，同时保留受控 xacro 子集、拒绝表达式与预算业务边界。不能把通用 DOM 当作完整 Xacro 展开器。

检索证据：`xacro.stdout.log`、`up-xml-json.stdout.log`。

### COV-07 [P1] Worker 通道字节原语、增量帧与消息重组链未登记

ChannelProtocol.cpp 无 sources。现有 Scheduler.submit 和 WorkerSupervisor.spawnNewWorker 仅覆盖两个入口，未覆盖序号/身份、帧校验/长度预算、缺口超时与分片重组。安全护栏不能以一般调度审计替代。

源码定位：
- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/execution/src/ChannelProtocol.cpp:28` — `putU32`；同文件已有 0 条 sources，本定位均不在已声明区段内。
- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/execution/src/ChannelProtocol.cpp:236` — `FrameAssembler::feed`；同文件已有 0 条 sources，本定位均不在已声明区段内。
- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/execution/src/ChannelProtocol.cpp:319` — `FrameSequencer::accept`；同文件已有 0 条 sources，本定位均不在已声明区段内。
- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/execution/src/ChannelProtocol.cpp:432` — `fragmentMessage`；同文件已有 0 条 sources，本定位均不在已声明区段内。
- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/execution/src/ChannelProtocol.cpp:470` — `MessageReassembler::accept`；同文件已有 0 条 sources，本定位均不在已声明区段内。

上游方向与边界：已定位 rw/common/BINArchive.hpp 的 writeValue/readValue（:306 起 readValue 为原生内存字节读），与显式网络字节序协议不同；需区分可共享的编码原语和本域帧序/身份/预算，不能拿 BINArchive 整体替掉协议，也不能因此豁免所有 helper。

检索证据：`protocol.stdout.log`、`up-xml-json.stdout.log`。

### COV-08 [P1] 项目 canonical JSON 引擎及实体编码未登记

Codec.cpp 无 sources；project 当前仅 ObjectStore publish/read。自含 JSON 的深度/数值/转义规则、ASCII canonical 字节与实体固定字段序是不同层面的功能，ObjectStore 持久化入口不能涵盖解析核。

源码定位：
- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/project/src/Codec.cpp:160` — `JsonParser`；同文件已有 0 条 sources，本定位均不在已声明区段内。
- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/project/src/Codec.cpp:224` — `JsonParser::parseValue`；同文件已有 0 条 sources，本定位均不在已声明区段内。
- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/project/src/Codec.cpp:299` — `JsonParser::parseString`；同文件已有 0 条 sources，本定位均不在已声明区段内。
- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/project/src/Codec.cpp:914` — `codec::dump(ProjectStaticIdentity)`；同文件已有 0 条 sources，本定位均不在已声明区段内。

上游方向与边界：已检索 DOMParser/DOMElem、PropertyMap loader 和 BINArchive；这些不是现成严格 canonical JSON 替代。应继续对已依赖通用编码能力做适用性比较，并分别审通用解析/Unicode helper 与项目身份 canonical 规则，不能将严格格式需求作为整文件自实现的默认许可。

检索证据：`project-codec.stdout.log`、`up-xml-json.stdout.log`。

### COV-09 [P1] 报告二进制 Writer/Reader 与摘要链未登记

ReportCodec.cpp 无 sources；Archive.publish 不能覆盖大端整数、有限 IEEE754、presence 严格值、边界 Reader、data/full 编码和 digest 绑定。既有相似编解码 helper 的共享/差异也未作逐功能判断。

源码定位：
- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/reporting/src/ReportCodec.cpp:50` — `Writer`；同文件已有 0 条 sources，本定位均不在已声明区段内。
- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/reporting/src/ReportCodec.cpp:78` — `Writer::f64`；同文件已有 0 条 sources，本定位均不在已声明区段内。
- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/reporting/src/ReportCodec.cpp:1255` — `ReportCodec::parseData`；同文件已有 0 条 sources，本定位均不在已声明区段内。
- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/reporting/src/ReportCodec.cpp:1327` — `ReportCodec::parseFull`；同文件已有 0 条 sources，本定位均不在已声明区段内。

上游方向与边界：BINArchive 的主机内存字节语义与本文件大端稳定身份不同。应审可复用字节工具与 core 摘要消费，并将业务字段序/身份规则单列；当前只能给候选方向，不能授予新核心保留或跨域公共 codec 架构。

检索证据：`project-codec.stdout.log`、`up-xml-json.stdout.log`。

### COV-10 [P1] 已有登记文件仍遗漏值分派与护栏 helper

同文件已有12条 sources，但这些区段均不在任一已声明范围或 auditedSections 内；parseValue 控制深度上限、标量分派及 NaN/Infinity/注释拒绝，非纯转发。parseJson 545–549 的入口条目明确不代表内部未覆盖函数。该例直接否定“一文件一条映射即整文件已审”。

源码定位：
- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/src/JsonLite.cpp:78` — `Parser::skipWs`；同文件已有 12 条 sources，本定位均不在已声明区段内。
- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/src/JsonLite.cpp:113` — `Parser::parseValue`；同文件已有 12 条 sources，本定位均不在已声明区段内。
- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/src/JsonLite.cpp:153` — `Parser::expectLiteral`；同文件已有 12 条 sources，本定位均不在已声明区段内。

上游方向与边界：沿当前 JsonLite 解析核的既有候选继续比较，覆盖整条语法分派和限制；不能只增若干叶函数就声称通用解析核全量已审。DOM/XML 搜索结果不能证明 JSON 语法能力缺失，未命中还须查相邻抽象和构建。

检索证据：`testkit.stdout.log`、`up-xml-json.stdout.log`。

### COV-11 [P1] 断言辅助和真实进程运行设施未登记

两个文件均无 sources。Check 已正确委托 core::closeWithin，但结果组装/索引失败仍需分类；ProcessRunner 包括 Windows 参数转义、环境合并、Job/CREATE_SUSPENDED 生命周期与失败清理，不能由 JsonLite 12条代表 testkit 全域。

源码定位：
- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/src/Check.cpp:44` — `checkCloseWithin`；同文件已有 0 条 sources，本定位均不在已声明区段内。
- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/src/Check.cpp:60` — `checkAllCloseWithin`；同文件已有 0 条 sources，本定位均不在已声明区段内。
- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/src/ProcessRunner.cpp:65` — `detail::quoteWindowsArg`；同文件已有 0 条 sources，本定位均不在已声明区段内。
- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/src/ProcessRunner.cpp:199` — `buildEnvironmentBlock`；同文件已有 0 条 sources，本定位均不在已声明区段内。
- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/src/ProcessRunner.cpp:310` — `TestProcessRunner::start`；同文件已有 0 条 sources，本定位均不在已声明区段内。

上游方向与边界：已定位上游 ThreadPool.hpp 与产品 execution/ProcessLauncher 等真实进程链；线程池不是隔离进程替代。应核测试设施与执行域共享/差异，以及 GTest/TestEnvironment 适用范围；不为复用抽象而绕开六类端口或 worker 隔离。

检索证据：`testkit.stdout.log`、`up-process.stdout.log`、`up-test-build.stdout.log`。

### COV-12 [P1] 测试夹具、解析期望与追溯辅助未按功能覆盖

这些测试文件均无 sources。已登记的测试路径只有 core/TestMainReport.cpp、evidence/TestMainReport.cpp 和 kinematics/KinFkFixture.hpp；前三处不是全域测试覆盖证据。样本展示业务 fixture、容差独立口径和真实摘要消费都在审计范围。

源码定位：
- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/optimization/test/ParetoTest.cpp:78` — `makeCandidate`；同文件已有 0 条 sources，本定位均不在已声明区段内。
- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/dynamics/test/StatisticsTest.cpp:103` — `expectNearRel`；同文件已有 0 条 sources，本定位均不在已声明区段内。
- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/dynamics/test/StatisticsTest.cpp:113` — `rowAt`；同文件已有 0 条 sources，本定位均不在已声明区段内。
- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/modeling/test/XacroExpandTest.cpp:81` — `digestOf`；同文件已有 0 条 sources，本定位均不在已声明区段内。
- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/modeling/test/XacroExpandTest.cpp:90` — `existingNodeFor`；同文件已有 0 条 sources，本定位均不在已声明区段内。

上游方向与边界：沿 GTest/TestEnvironment、已有 testkit/core 复用核查夹具和报告辅助；解析黄金/独立 oracle 可保留测试用途，不要求改用与生产同源算法，防止两路互证失去独立性。

检索证据：`tests-corrected.stdout.log`、`up-test-build.stdout.log`。

### COV-13 [P1] 固定库存中的黄金生成脚本功能未登记

foundation-governance 库存33文件但没有功能关系；其中真实可执行 mjs 包含二连杆解析动力学、梯形统计、Pareto/排序以及身份生成。它们是明确在范围内的黄金生成器，不因库存 role=production 或位于 testdata 就可忽略。

源码定位：
- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testdata/golden/dyn-two-link-analytic/1.0.0/generate/make_dyn_two_link_golden.mjs:63` — `trapezoid`；同文件已有 0 条 sources，本定位均不在已声明区段内。
- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testdata/golden/dyn-two-link-analytic/1.0.0/generate/make_dyn_two_link_golden.mjs:77` — `rneaReference`；同文件已有 0 条 sources，本定位均不在已声明区段内。
- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testdata/golden/opt-pareto-golden/1.0.0/generate/make_opt_pareto_golden.mjs:358` — `dominates`；同文件已有 0 条 sources，本定位均不在已声明区段内。
- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testdata/golden/opt-pareto-golden/1.0.0/generate/make_opt_pareto_golden.mjs:392` — `buildFront`；同文件已有 0 条 sources，本定位均不在已声明区段内。

上游方向与边界：与产品动力学/Pareto 候选和上游测试算例对照，审输出数据消费者、输入/脚本/产物版本绑定及独立锚点。脚本可作为独立 oracle 保留；本发现要求审计与证明用途隔离，不要求迁移为生产算法或复用同源 oracle。

检索证据：`golden-search.stdout.log`、`up-integrals.stdout.log`、`up-optimization-corrected.stdout.log`。

### COV-14 [P1] CMake 目标接线及门禁解析功能未登记

全矩阵没有构建文件作为功能 sources。单元 CMake 多作为 inputs 摘要，只证明新鲜度；目标可用性、集成/冒烟条件编译、GTest main/报告接线和门禁文本解析是实际构建功能。GOV 精确 bootstrap 或局部门禁 pass 不等于 T01 已审全部构建。

源码定位：
- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/cmake/ird_gates.cmake:95` — `ird_read_flat`；同文件已有 0 条 sources，本定位均不在已声明区段内。
- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/cmake/ird_gates.cmake:104` — `ird_extract_blocks`；同文件已有 0 条 sources，本定位均不在已声明区段内。
- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/cmake/ird_gates.cmake:152` — `ird_classify_target`；同文件已有 0 条 sources，本定位均不在已声明区段内。
- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/CMakeLists.txt:32` — `find_package(GTest CONFIG REQUIRED)`；同文件已有 0 条 sources，本定位均不在已声明区段内。
- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/trajectory/CMakeLists.txt:118` — `if(TARGET sdurw_kinematics)`；同文件已有 0 条 sources，本定位均不在已声明区段内。

上游方向与边界：已检索上游 gtest/CMakeLists.txt、TestEnvironment 和 rw/rwlibs 导出目标。应对真实 CONFIG 目标、条件源集、main/全局初始化边界作接线判断；架构分类/红线扫描可归业务治理，但需明确功能边界及排除证据，不要求把业务门禁替换为机器人算法。

检索证据：`foundation.stdout.log`、`up-test-build.stdout.log`。

### COV-15 [P1] 宿主预览已登记锚点/网格但未审实际渲染接线与清理

同文件已登记 ensureSessionPreviewAnchor:471–504 与 SampleGridRender::draw:268–344，但实际 draw:360 起的宿主/场景 WorkCell 来源、WORLD/Frame 挂接、addRender 和 clear:506 起 removeDrawable 均不在声明范围。不能将已登记精确宿主例外扩展为整后端许可。

源码定位：
- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/HostView3DPreviewBackend.cpp:360` — `HostView3DPreviewBackend::draw`；同文件已有 2 条 sources，本定位均不在已声明区段内。
- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/HostView3DPreviewBackend.cpp:506` — `HostView3DPreviewBackend::clear`；同文件已有 2 条 sources，本定位均不在已声明区段内。

上游方向与边界：已定位 WorkCellScene.hpp:86/89 set/getWorkCell、:305 addRender、:406 removeDrawable。应核 HOST-RENDER 精确权限、帧与场景所有权/生命周期、替换与清理真实结果消费，保留业务预览语义；不判断本次抽样已发现违规操作。

检索证据：`ui-workflow.stdout.log`、`up-host-scene.stdout.log`。

### COV-16 [P2] 业务关闭监督生产链也需明确分类

workflow 当前只有 PureStageGateService.evaluate；关闭监督线程的只观测不推进、阈值诊断、停止/退出顺序与原子观测不在矩阵。即使最终属业务特定，也应给语义分组与不重叠证据，不能整单元默认 business-specific。

源码定位：
- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/workflow/src/CloseSupervisor.cpp:55` — `StoreCloseSupervisor::start`；同文件已有 0 条 sources，本定位均不在已声明区段内。
- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/workflow/src/CloseSupervisor.cpp:95` — `StoreCloseSupervisor::superviseLoop`；同文件已有 0 条 sources，本定位均不在已声明区段内。

上游方向与边界：已定位上游 ThreadPool（通用任务执行）及产品现有关闭/调度端口；它们不能仅凭同为线程就替代项目排空协议。需审共享并发原语与业务生命周期边界，避免引入跨域互链。

检索证据：`ui-workflow.stdout.log`、`up-process.stdout.log`。

## 后续退出判据

先按实际功能完成全范围发现与有理由的分组，再逐条给 direct/adapter/business-specific/pending/proposal、上游版本/符号/目标/差异/权限与足够适用性证据；对既有 adapter 也审真实调用及有效结果消费。阻塞提案可以保留，但不能用未审 pending 代替审计结论。随后对新的冻结 SHA 做独立覆盖/决策审查。补齐本报告列出的样本，不自动代表其他功能已审完。

## 验证与限制

- PASS：冻结 HEAD 与指定 SHA 一致，独立 clone `git status --porcelain` 为空；全量元数据解析完成，16组具体源码区段反查完成。
- NOT RUN：C++ 构建、产品测试、门禁自测和远程 CI。本轮是只读覆盖审查，没有产品或脚本改动；不重复已完成的局部门禁验收。
- 上游候选检索不是适用性批准；未批准新核心、上游扩展、权限扩大或 T02 准入。
- 初轮路径错误的 rg exit2 日志保留；后续真实路径检索均 exit0，不把错误搜索认作无能力。
- 源实施树未提交内容未消费；未提交、推送或合入。源码与报告均保存在本随机 Temp 现场。
- 元数据全量、语义定向抽样。runtime 视图/offset 不是本轮重点，亦未对全部220条关系做最终正确性背书。

完整事实、区段摘要和证据清单见 `coverage.json`；源码摘要与范围在 `inventory-analysis.json`，检索命令及退出码在 `searches.json` / `followup-searches.json`。
