# 上游复用迁移计划（2026-10-22）

## 1. 审计基点与结论

基点 b12e8503cc1c2a1acc040f506842a02e126b9515；根目录内还有 RobWork/ 子树。Fk.cpp 自持 FK/Jacobian；Ik.cpp 自持 DLS/约束差分；InverseDynamics.cpp 自持 Vec3/Mat3/RNEA；ForwardDynamics.cpp 自持质量阵/消元/RK4。CouplingMath.hpp、DescriptionValidator.cpp、MappingCore.cpp 各有 SVD；CartesianLine.cpp 手写 SLERP。Planner.cpp 与 policy/runtime 的 RRT/碰撞适配保留。全文件入口为 inventory.json，未完成语义核查如实标记 audit-required。

## 2. 功能与复用映射

| 能力 | 当前位置（industrialrobot/ 下） | 上游文件/API | 边界 |
| --- | --- | --- | --- |
| FK/Jacobian | kinematics/src/Fk.cpp | RobWork/src/rw/models/Device.hpp：setQ/baseTframe/baseJframe | q_auth-offset、TCP、参考系、固定段 |
| 普通 IK | kinematics/src/Ik.cpp | RobWork/src/rw/invkin/JacobianIKSolver.hpp：DLS/solve/solveLocal | 双容差、取消、统计、稳定解集 |
| SVD/伪逆 | modeling/src/CouplingMath.hpp；runtime/src/DescriptionValidator.cpp；drivetrain/src/MappingCore.cpp | RobWork/src/rw/math/LinearAlgebra.hpp：svd/pseudoInverse | 精度参数显式，保留业务病态阈值 |
| 逆动力学 | dynamics/src/InverseDynamics.cpp | RobWorkSim/src/rwsim/util/RecursiveNewtonEuler.hpp：solve/setGravity/setPayload/setEnvironment | RigidDevice/DOF/Body 对齐、物性参考系、载荷与摩擦 |
| 正动力学 | dynamics/src/ForwardDynamics.cpp | RobWorkSim/src/rwsim/simulator/PhysicsEngine.hpp：load/initPhysics/step | 真实任务私有场景、力控制、后端、步长与初态 |
| 插值 | trajectory/src/Ptp.cpp；CartesianLine.cpp | rw/trajectory/LinearInterpolator.hpp；rw/math/Quaternion.hpp：slerp | 端点、最短弧、异常和零长段 |
| 旋转/变换 | runtime/src/BaseWorldTransform.cpp；modeling/src/CanonicalBridge.cpp/DhConvert.cpp/RpyMath.hpp；kinematics/src/Sampling.cpp | rw/math/EAA.hpp、RPY.hpp、Transform3D.hpp | 坐标约定、π/零角、DH 类型 |
| 高阶轨迹 | trajectory/src/Smooth.cpp/TimeParam.cpp | rw/trajectory/CubicSplineFactory.hpp、RampInterpolator.hpp、ParabolicBlend.hpp（候选） | 不把三次当五次/七次，不放宽 jerk/C²/C³ 要求 |
| 规划/碰撞 | trajectory/src/Planner.cpp；policy/runtime | RRTPlanner、QSampler、CollisionDetector | 保留 policy 端口与已复用计算 |

上述上游路径以仓库根下 RobWork/ 为前缀；catalog.json 保存完整路径、具体符号和规范化内容摘要，review-checklist.md 定义真实调用证据。

## 3. 优先级与终态

P0：先修规则/构建/冲突约束，替换 FK/Jacobian、普通 IK、RNEA 和不符合 DYN-05 的正动力学路径。P1：消除重复 SVD/旋转/SLERP/线性插值。约束 IK、高阶轨迹、DH 拟合和固定段模型在证据不足时先提案，不扩写内核。保留业务过滤、排序、工况覆盖、包络与可信度，不保留旧生产内核作隐藏兜底。

## 4. 架构与兼容

runtime 复用已编译 WorkCell/SerialDevice/DynamicWorkCell/RigidDevice，统一关节与 TCP 映射；不从 CanonicalModel 在业务域再建链。单元私有适配器连接现有 Evaluator 门面与上游 API；通过工厂/版本化能力接口补齐 IKinRuntimeView，保持 DTO/命令/错误语义，ABI 变化显式版本化。State/求解器/引擎任务私有，快照只读。

## 5. 文档覆盖

根与分区 AGENTS、REQUIREMENTS、ARCHITECTURE、DETAILED-DESIGN、20 单元卡、DTB、契约编译/验收/执法文档、README、六阶段/Qt/Python Skills、PR 模板均链接政策。旧 O-40“rw 无 SVD”与 P-DYN-7“自持 RK4/禁引擎”登记为已被本治理决定取代的历史，不修改历史验收证据或将旧 done 解释为复用合规。

## 6. 门禁与约束

validate-reuse 与负夹具接入本地/CI/ird_gates；新增/改动产品文件须精确摘要与复用方式；扩展须提案＋范围明确的批准；库存不能删项移基点；最终 RequireMigrationComplete 必须零债务零未审计。启发式扫描无法证明所有语义重复，独立审查是必需检查。

## 7. 顺序、范围与回滚

| 任务 | 范围/动作 | 前置 | 回滚 |
| --- | --- | --- | --- |
| GOV-T21 | 文档、约束、门禁、CI、审查规则及库存；零算法修改 | 本次所有者指令 | 完整任务 revert |
| REUSE-T01 | 全 20 单元逐功能复用审计、最小扩展提案、API/构建/运行探测 | GOV-T21 独立 pass 记录 | 独立审计任务 revert |
| REUSE-T02 | runtime 能力/线程私有 State/上游链接及数学薄封装设计、实现与真接线 | 该能力及依赖审计/权限就绪＋治理验收；相关提案单独批准 | 适配任务 revert |
| REUSE-T03 | FK/Jacobian/普通 IK 替换；约束 IK 按批准提案 | T02 | 分能力 revert |
| REUSE-T04 | RNE、真实 RobWorkSim 正动力学场景 | T02＋动力学差异验证/批准 | 分能力 revert |
| REUSE-T05 | trajectory/modeling/drivetrain 及其余单元重复实现清理 | 按能力依赖准入；纯值SVD/旋转无需等待FK/动力学，高阶能力单独批准 | 分能力 revert |
| REUSE-T06 | 移除旧核心、完整回归/性能/包验证、最终零债务验收 | T05 | 发布制品/完整提交 revert |

后续 planned 契约必须按审计结果细化精确 allowedFiles、verify、acceptance，不能把此表当作任意修改许可。各次代码、测试、CMake、文档同提交并立即推送；不自行合入。

## 8. 测试与验收

复用上游 SerialDeviceTest/KinematicsTest/JacobianIKSolverTest/LinearAlgebraTest/TrajectoryFactoryTest；新增适配边界与真实端口集成。包含非 Z 轴、offset、移动/固定/连续关节、多 TCP、倒挂/侧装、载荷事件、零/缺失物性、摩擦唯一计入、奇异/病态、取消/错误、轨迹连续性与 jerk。解析 oracle 独立于新旧内核；不通过重产旧黄金值掩盖差异。双模式日志＋gtest XML＋ird-test-report；GUI 按 VS x64/windows/逐 exe 规则。治理 PowerShell 自测用 JUnit XML＋JSON，单独计数。

## 9. 风险、缺口与扩展停止条件

上游 RNE validate 限定 SerialDevice/转动或移动关节；solveMotorTorques 返回 DOF+1 且按 torque 投影，移动关节不能机械调用。Body 参考系与固定段需验证，载荷不可重复。RK4Integrator 是刚体接口，不是关节 ODE 直接替代；PhysicsEngine 力控制/后端/确定性须实测。约束 IK 双容差、取消/统计和高阶时间律能力尚未验证。以上逐项写入 extensions/required-proposals.md，不能因此另写核心或降低需求。

## 10. 分阶段完成定义

A 文档/约束/门禁经独立冻结 SHA 验收后，B 审计与适配能力探测，C 分能力替换，D 清理与最终验收。治理 pass 只证明规则可执行，不证明现有算法已合规；算法扩展与合入决策仍归所有者。远程分支保护必须实际读取验证，未配置不得声称已强制。

## 11. 最终方案补充阶段门（2026-10-22）

本节细化 §7/§10 的阶段，不重写历史 GOV-T21 证据；本次仅实施文档治理。**16 个 migration-debt 不是范围上限**；全部 20 单元、既有 adapter、测试/生成器、脚本、工具及构建基础均须按需求检查上游复用机会。架构权限唯一见 ARCHITECTURE §3.6 / SA-19，数据与新增门禁验收见 [audit-contract.md](audit-contract.md)。

| 阶段 | 文件/动作与退出条件 | 放行边界 |
| --- | --- | --- |
| A 文档与权限契约 | 根/分区 AGENTS、ARCH §3.6/§9、单元卡、policy、audit-contract、审查/PR/CI 说明建立同一引用链；明确角色、宿主例外与 worker/evidence/diagnostics 边界 | 文档门禁及事实核对；不等于新增门禁已实现 |
| B 门禁补强 | 在现有 validate-reuse.ps1、test-reuse-governance.ps1、ird_gates.cmake/白名单和既有 CI 中实现权限、catalog/功能矩阵、新鲜度、direct/adapter 正向证据与结项规则；隔离正负夹具＋独立冻结 SHA 验收 | 新增规则未验收不得进入 REUSE-T02；现有 GOV-T21 pass 不自动覆盖 |
| C REUSE-T01 双向审计 | 按 audit-contract §2 分批 review 上游并反查项目，扩展 catalog，填 functions/<unit>.json；真实 GAP 探测和候选排除；完成全项目功能决策，影响 REUSE 契约随审计细化 | 不要求先通读整个库；每个缺口单独批准，审计完成不批准新核心 |
| D REUSE-T02～T05 能力迁移 | 先 runtime 受控视图/线程会话与 owner 边界，再按 FK/Jacobian/IK、RNE/FD、轨迹/其余单元逐能力替换；具体顺序按依赖/风险确定 | 生产入口复用证据、同模型上游对照、独立 oracle、相关断开/扰动实验、兼容/数值/性能验收；无 silent fallback |
| E REUSE-T06 清理验收 | 删除旧生产核心与死依赖，基线关联语义关闭记录；全范围回归、性能、打包和最终零债务检查 | unresolved/pending/debt/audit-required 均不得冒充关闭；最终冻结版本独立验收 |

阶段 B/C 的数据与校验器按同一 schema 演进，不先伪填 catalog/矩阵状态；保留旧凭证兼容，避免治理切换放行空窗。后续任务 ready 前必须核验原治理验收、新增补强验收和实际能力前置，不能只在 Markdown 勾选。

每能力批次同时替换生产路径、删被替代核心、补真实边界测试及同步文档；不为每个值运算加接口，不建第二模型层/通用 facade。回滚完整能力提交或已知制品，不以恢复隐藏旧内核掩盖差异。数值差异不放宽需求容差，批准 SA-19 或宿主例外不能授权算法重写。

当前 RNE 的 DOF+1、移动关节广义力、Body/惯量参考系，FD 引擎力控制/确定性及高阶轨迹均仍须探测；GAP-IK/RNE/FD/TRJ 是待验证入口，不是已证实缺库。上游测试/示例优先复用，新增仅补产品边界，独立 oracle 限测试使用。最终声明限定冻结版本与审计范围，不声称语义重复绝对不可漏检。

## 12. 下一阶段实施与重构工作包（2026-10-22）

**目标**：在原有 20 单元/六类端口内消除平行核心，使实际生产功能尽可能复用 RobWork/RobWorkSim，并用权限、适用性和结果消费证据验收。

**执行方式**：单会话逐工作包实施；最新AGENTS禁止自动派发子智能体，验收使用所有者另开的独立会话；既有在途结果按冻结版本保留。本文是唯一迁移计划，不另建重复 spec/进度台账；schema 和证据格式引用 audit-contract，不在此复制。GOV-T22 已启动工作包 0～2 的治理补强，独立验收前不进入适配/算法阶段；未勾选步骤仍未完成。

### 12.1 真实起点与依赖

文档治理提交为 `25ecf7f6d`，已合入本地 redesign-main；截至本计划制定时远程同步因网络失败未完成，不能声称远程交付。现有 REUSE-T01～T06 均为 planned，仅有通用 verify，尚无精确 allowedFiles 或 acceptance，**不能直接作为实施授权契约**。旧 GOV-T21 验收、文档自查、新门禁验收、全功能审计和能力迁移验收分别记录，不相互替代。

顺序更新见§12.11：治理验收后，全项目覆盖筛查持续进行，能力及其依赖完成审计/权限确认即可按精确任务实施；完整能力包独立验收后放行依赖能力。全T01完成是T06最终收口前置，不再是所有产品改动的统一前置。门禁开发时允许在隔离夹具先做代表性审计样本，不改真实生产算法，不把样本冒充全量审计。

以下产品路径统一相对仓库根下 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/`，设计路径相对 `RobWork/doc/industrial-robot-design/`；脚本/CI 路径按仓库根给出。产品 implementation/test/CMake/相关单元卡和复用证据同批同步。

### 12.2 工作包 0：固定交付基点与实施契约

- [ ] 网络恢复后读取 origin/redesign-main，与本地文档提交比较；普通同步/PR，不强推、不改分支保护、不触碰冻结 main。记录真正同步的 SHA，不能以本地 ahead 状态宣布交付。
- [x] 定向核验原 GOV-T21 验收记录和祖先关系；保留旧失败记录、F-955 迁移债务/F-956 产品 CI 问题的实际状态，不以文档治理关闭它们。引用见 tasks/GOV-T22.json。
- [x] 将门禁补强作为 GOV-T22 限定精确脚本、权限/schema 数据、CI 与夹具范围；新增 reuseGateAcceptance 引用独立 Git 对象。bootstrap 不授权产品算法；本次复用原 ird_gates 调用，无需重复修改 CMake。
- [ ] REUSE-T01～T06 进入 ready 前分别补齐精确 allowedFiles、相关需求/架构章节、knownPitfalls、真实 verify/acceptance、原治理及新增补强验收引用。按§12.11能力依赖编制精确子任务；父任务planned不构成产品授权，不要求纯值数学等待无关动力学。

退出条件：交付 SHA、授权范围、阶段准入和可执行验收均可追溯；没有用 approved=true 或 Markdown 勾选冒充独立批准。网络问题只阻止远程交付，不伪报本地验证失败或算法失败。

### 12.3 工作包 1：门禁数据模型与兼容升级

本地治理验收：`e6f06e5b9` 已由独立会话第三轮 pass（[证据](../traceability/acceptance/GOV-T22-20261022-r3.json)，不可变 Git 引用见 tasks/GOV-T22.json）；PS7/PS5.1 各 136 passed、0 failed/skipped/notRun，权限/BOM 独立探针各 10 passed，完整关闭对照各 2 passed。工作包 1 和工作包 2 的机器规则已实现；静态规则之外的间接调用/生命周期仍须逐功能审查，不视为全语义覆盖。下方 CI 项的本地双运行时与模板同步已完成，但远程 CI/保护未核验，故保持未勾选。工作包 0 的远程同步仍因本机代理不可达阻断，未合入主线、未启动后续适配/算法阶段。

修改范围：`reuse/catalog.json`、后续 `reuse/functions/<unit>.json`、原 `reuse/changes/` 格式和阶段验收引用；校验实现为 `RobWork/scripts/industrialrobot/validate-reuse.ps1`，夹具为同目录 `test-reuse-governance.ps1`。若需机读权限数据，集中在现有 `reuse/` 中一个权限文件，引用 ARCH §3.6 与 HOST-* ID，不复制架构 prose 或另建 policy 引擎。

- [x] 按 audit-contract §3/§4/§5 在隔离夹具定义 catalog 角色/操作、functionId/调用链、权限记录与来源摘要的有效样例；固定基线和旧能力 ID 保持不变。
- [x] 先增加拒绝夹具：缺 owner/consumer、未知能力、权限引用不存在、伪上游符号/target、重复 functionId 关系、路径逃逸、失效摘要、丢文件/丢功能覆盖；确认旧门禁不能满足新增验收。
- [x] 同一提交升级校验器、数据和夹具。旧字段兼容迁移须显式标为待审，不能默认 verified；采用一个明确 schema 版本分派，禁止新字段被旧入口静默忽略。
- [x] 固定状态含义：文件已登记≠功能已审计，功能已审计≠适用性已验证，适用性已验证≠生产已迁移，生产已迁移≠独立验收已关闭。缺口可记录为 proposal；无证据功能保持 pending，不填虚假通过。

退出条件：新/旧有效凭证可受控读取，非法数据与未知 schema 显式拒绝；没有清空 inventory 或把历史债务重分类洗白。后续业务专属规则只因真实需求扩充，不一开始设计万能 schema/插件体系。

### 12.4 工作包 2：权限、证据与 CI 门禁补强

修改范围：上述两脚本、`cmake/ird_gates.cmake`、`cmake/ird_gates_whitelist.cmake`、`RobWork/scripts/industrialrobot/validate-task.ps1`（ready 校验确需时）、既有 `.github/workflows/upstream-reuse-governance.yml` 及镜像 `RobWork/scripts/industrialrobot/ci/upstream-reuse-governance.github-workflow.yml`；其他入口仅必要接线，不复制校验逻辑。

- [x] 实现 IRD-GATE-AD：按文件/编译目标/头与类型/对象来源/操作核对权限。正例包括数学值、runtime 编译、受控视图、任务私有 State/solver、四组精确宿主操作；负例覆盖业务 load/clone/factory 建 WC、模型结构写入、共享 State 写、宿主例外扩散、worker Widgets/借持指针。
- [ ] 静态扫描不能可靠识别的别名/间接工厂/句柄逃逸，输出具体位置并要求调用链和独立审查，不宣称正则可完备判断；若现有构建信息足够则复用，不预先引入第二套解析工具链或全局 AST 服务。
- [x] 实现逐功能覆盖与证据新鲜度：改动的源区段、上游能力、依赖/配置或测试摘要失效时，相关证据失效；隐藏 helper、模板、脚本/黄金生成器不能绕过。格式正确但未审计的存量仍输出债务，不冒充 PASS。
- [x] 实现 direct/adapter 正向证据结构与结果读取：入口→上游→消费端、真实目标/用例、正常执行与隔离断开/扰动的相反结果。门禁拒绝 demo-only、只 include/link、忽略返回值、伪造/过期报告；测试语义和来源真实性仍由独立审查负责。
- [x] 按状态应用证据要求，避免循环：审计阶段允许 pending/proposal 但不得登记 migrated/closed；真实 migrated 行必须有生产正向证据；最终模式拒绝 pending/debt/audit-required/旧 fallback。不能要求尚未替换的旧路径提供新路径证据，迫使伪造报告。
- [x] 原验收引用之外增加新补强验收检查；T02 及之后缺记录、非 pass、错误 SHA、非祖先或引用错阶段均拒绝。普通文档检查不因此要求产品验收。
- [ ] PS7/PS5.1 执行相关正负夹具并保留 JUnit/JSON；CI 调用同一入口和冻结树，模板同步。不更改保护规则以绕过失败；远程必需检查是否生效须实时读取设置确认。

退出条件：audit-contract §7 各类规则均有允许/拒绝实测，驱动完整执行、notRun/driverFailure 不伪报通过；受影响 CMake 目标注册/ird_gates 验证通过；独立会话冻结 SHA 验收 pass 后才能进入 T02。此工作包不修改 FK/IK/RNE/FD 等生产算法。

### 12.5 工作包 3：REUSE-T01 全项目双向审计

每批都执行需求定位→上游检索/阅读→产品反查→最小运行探测→功能矩阵决策；无需每批通读整个库。批次只区分审计范围，不新增单元或改变既有 owner。

| 批次 | 覆盖单元/资产 | 关键输出与上游检索方向 |
| --- | --- | --- |
| 校准与内核 | runtime、core、evidence、policy、diagnostics、testkit | 模型/名称/State、数学/单位、证据/诊断权威、碰撞适配、测试夹具/报告；区分 owner 与 adapter |
| 计算能力 | modeling、kinematics、trajectory、dynamics、drivetrain | 变换/DH/SVD、FK/Jacobian/IK、采样/规划/插值/时间律、RNE/FD；复核 16 已知项并找隐藏核心 |
| 业务编排 | requirements、selection、optimization、workflow | 区域/工况、目录与筛选、搜索/随机源/优化、门控编排；不能把整单元当作 business-specific |
| 平台与宿主 | project、execution、io、reporting、ui | 加载/序列化、worker 物化/调度、资源/导入/导出、呈现例外；检索上游工具、示例、测试和构建可复用点 |

- [ ] 每个功能给出 direct/adapter/business-specific/pending/proposal 决策；沿调用链反查共同 helper、inline、测试辅助和生成器。已观察适配也要验证权限与结果消费。
- [ ] catalog 加入已实际读到的测试/示例/文档/target 和限制；functions 矩阵关联具体源码区段/摘要、需求/AT、权限与候选排除。不把上游测试直接复制为产品自己的平行测试基础。
- [ ] 最小探测优先使用现有上游/产品测试目标与模型；需加探测测试时严格限原公共 API，记录依赖和范围，不能提前把实验接入生产，也不能把实验输出记为生产正向证据。
- [ ] 分别验证 GAP-IK/RNE/FD/TRJ；未满足的功能提交最小扩展提案，保留搜索/反例/替代/影响/回滚。没有足够证据不得宣称缺库；提案未批不得补写核心。
- [ ] 独立审查全 20 单元的覆盖与决策，生成覆盖/重复/缺口报告；矩阵审计完成可以包含明确阻塞的提案，但依赖该缺口的迁移任务不得 ready。审计完成不能等同零债务结项。

退出条件：全项目功能都有可解释决策和权限边界，未审计项不隐去；T02 范围能落实到真实接口与调用链，缺口依赖逐项明确。按代表批次耗时再估算其余工期，不采用推测百分比节省或固定总天数。

### 12.6 工作包 4：REUSE-T02 runtime 边界与最小适配

主要文件：`runtime/include/sdurws/ird/runtime/Adapter.hpp`、`Snapshot.hpp` 和 `runtime/src/Snapshot.cpp`；现有 WorkCellConstView 实现在 `runtime/src/WorkCellCompiler.cpp`，DynamicWorkCellConstView 实现在 `runtime/src/DynamicWorkCellCompiler.cpp`，先核已有视图，确有缺口才改对应实现/编译映射。消费端核对 `kinematics/include/sdurws/ird/kinematics/KinTypes.hpp` 中 IKinRuntimeView 与实际装配端，不假设存在 Adapter.cpp 或新的 RuntimeView.hpp。

- [ ] 先核现有 IRuntimeModelView::workCell/tryDynamicWorkCell/makeState/nameResolver、Device/Body/TCP 映射是否足够；够用直接消费，不为全库建 facade，不把全部算法搬进 runtime。
- [ ] 为真实缺口选择最小公共视图/工厂调整；不直接给旧虚表插槽并宣称 ABI 无影响。逐一列消费者、契约版本和迁移办法；不得借此开放裸可变模型句柄。
- [ ] 先修runtime现有移动关节offset映射：T01真实编译器审计确认0.25 m偏置被编码为旋转且缺平移（[证据](../traceability/reuse/REUSE-T01/runtime-boundary-audit.md)）；原规范反例先在runtime原测试落位，不允许kinematics第二次补偿。此为T02相关适配验收前置，不是已完成或新核心批准。
- [ ] runtime 统一 q_auth/offset→q_rw、ObjectId/名称/关节/TCP 和 WC/DWC 对齐。业务拿快照保活的受控视图，State 与求解器/引擎会话任务私有；主进程/worker 按同一规则物化。
- [ ] 数学函数优先直接 rw::math 调用，仅在真实单位/坐标/错误边界留薄函数，不增通用数学接口。上游导出目标/现有 find_package 为唯一构建来源。
- [ ] 先写接口与生命周期回归，再改适配；验收多任务 State 不串扰、快照关闭/保活、缺设备/TCP/DWC、物化身份一致、异常转译及真实装配。宿主呈现只能用既有例外，不能新增计算构造链。

退出条件：受影响集成目标和独立冒烟、runtime/消费端契约通过，权限门禁与正向接线证据有效，公共接口/跨单元独立验收通过。薄适配不能携带新的数值迭代或替代内核。

### 12.7 工作包 5～7：分能力替换

| 工作包 | 精确修改起点/复用来源 | 保留与删除边界 | 核心验收 |
| --- | --- | --- | --- |
| T03-A FK/Jacobian | kinematics/src/Fk.cpp → rw/models/Device.hpp 的 setQ/baseTframe/baseJframe；必要时 models/JacobianCalculator.hpp | 保留 Evaluator/DTO 与业务度量/稳定码，删除自建链乘积/Jacobian；runtime 提供映射与模型 | FkTest、GoldenKinTest、真实端口契约；非 Z 轴/固定段/移动关节/offset/多 TCP/倒挂，上游同状态对照与独立几何/差分检验 |
| T03-B 普通与受限 IK | kinematics/src/Ik.cpp → rw/invkin/JacobianIKSolver.hpp；受限功能按 GAP-IK 结论 | 保留目标编排、稳定排序/去重与契约限位，删自持 DLS/重复差分；上游不能满足的功能阻断，不能以后过滤假冒求解约束 | IkTest、TaskPoints/RegionCoverage 契约；双容差、限位/奇异、不可达、取消/统计真实性、多解确定性 |
| T04-A 逆动力学 | dynamics/src/InverseDynamics.cpp → rwsim/util/RecursiveNewtonEuler；runtime DWC/RigidDevice 事实 | 保留工况/统计/证据、单位与已批准投影适配；删除 Vec3/Mat3/RneaChain 自建内核，摩擦/工具/载荷单点计入 | RneaTest、DynGoldenDatasetTest、真实 evaluator；DOF+1、移动关节广义力、Body/惯量参考系、固定段、重力/载荷，独立单/双杆物理 oracle |
| T04-B 正动力学 | dynamics/src/ForwardDynamics.cpp → rwsim/simulator/PhysicsEngine::load/initPhysics/step 与实际可用后端 | 保留请求/结果/工况编排，删自持质量阵/消元/关节 RK4；真实会话由 runtime 物化支持，不直接把刚体 RK4 当替换 | ForwardDynamicsTest、RNE/FD 一致性、真实力控制/初态/步长/收敛、能量与确定性、失败/取消/资源释放、性能 |
| T05-A 低风险数学重复 | modeling/src/CouplingMath.hpp、runtime/src/DescriptionValidator.cpp、drivetrain/src/MappingCore.cpp、modeling/src/RpyMath.hpp、runtime/src/BaseWorldTransform.cpp 等 → LinearAlgebra/RPY/EAA/Transform3D | 保留业务阈值/耦合语义/坐标约定；删除重复 SVD、旋转/矩阵核心，仅留必要数据适配 | 病态/秩亏、阈值边界、π/零角、单位/坐标，相关三单元回归与独立 oracle；共享 Eigen 不直接判为违规 |
| T05-B 轨迹/规划/其余发现 | trajectory/src/Ptp.cpp、CartesianLine.cpp、Smooth.cpp、TimeParam.cpp → LinearInterpolator、Quaternion::slerp、已验证高阶候选；其余按 T01 矩阵实际定位 | 保留业务时间/工艺/误差与策略端口，删重复插值；Planner.cpp 既有 RRT/碰撞复用通过审查则保留，不重写已适配路径 | PtpSequence/CartesianLineContinuity/Smooth/TimeParam/Recheck 测试；端点/零段/最短弧/连续性/jerk/碰撞，不能三次替代五/七次；隐藏项逐能力同标准迁移 |

以上文件是已知起点，不是穷尽范围；上游 API 在 T01 核查后才写入 ready 契约。对表中每一行执行同一小循环：

1. 补精确功能矩阵、调用链与验收，先运行原有有效回归建立基线；旧缺陷如实登记，不把旧输出当数学真值。
2. 写能区分生产是否消费上游的测试与独立 oracle，先确认其能拒绝当前错误/断开路径；复用现有 GTest/testkit，避免新通用测试框架。
3. 仅实现已批准薄适配/直接调用，替换真实生产入口；同一能力提交删除被替代核心/死依赖和隐藏 fallback。
4. 运行上游直接对照、隔离扰动/断开实验、相关回归/性能及权限/复用门禁；适配依赖或接口变动增加双模式与对端契约。
5. 更新源码中文契约、相关单元卡/catalog/矩阵/changes 和真实证据；冻结能力提交，由独立会话验收，所有者决定合入，再推进依赖能力。

SVD等纯值运算按§12.11允许先实施完整能力包；仍须该范围审计、精确契约、权限、真实生产正向证据与独立验收，不能借此跳过受影响门禁。高阶轨迹、约束 IK 或动力学缺口未批准则该能力停在提案，已通过其他能力不自动替其验收。

### 12.8 工作包 8：REUSE-T06 全范围收口

- [ ] 将不可变库存逐文件/逐功能关联迁移、删除或经审查的业务专属保留证据；最终判断从关闭记录得出，不改原 classification/基点。
- [ ] 检查生产和构建没有旧路径、特性开关兜底、旁路副本、无用链接/重复工具或黄金生成器内核；保留真正独立的测试 oracle，不能删成只与上游同源互证。
- [ ] 全范围运行最终迁移模式、产品集成回归、必要独立冒烟、性能/确定性与包验证；确认从干净检出可构建、证据完整且新增 CI 必需检查实际执行。
- [ ] unresolved/pending/debt/audit-required、未批提案、必需测试跳过、未关闭的本次影响缺陷均阻断最终通过。独立验收绑定完整冻结版本，结论限定版本/范围，不声称不存在不可发现语义重复。

### 12.9 可直接使用的验证入口与证据

以下均为现有命令；本计划仅列命令，**不代表已执行下一阶段测试**。任务内需把实际目标/测试正则/输出路径固化，不能把示例参数原样作为验收凭证。

```powershell
# 文档与当前复用结构检查；不能证明尚未实现的新门禁通过。
pwsh -NoProfile -File RobWork/scripts/industrialrobot/validate-docs.ps1
pwsh -NoProfile -File RobWork/scripts/industrialrobot/validate-reuse.ps1 -Head HEAD
# 门禁实施后重跑同一隔离夹具；PS5.1 以系统 powershell.exe 执行对应 -File 命令。
pwsh -NoProfile -File RobWork/scripts/industrialrobot/test-reuse-governance.ps1 -OutputDir .cache/reuse-tests
# 产品迁移时：下例为运动学受影响目标，其余单元替换为已注册目标。
cmake --build build --config Release --target sdurws_ird_kinematics_test sdurws_ird_kinematics_contract_test ird_gates
pwsh -NoProfile -File RobWork/scripts/industrial-robot/run-tests.ps1 -Configuration Release -BuildDirectory build -NoConfigure -Regex '^sdurws_ird_kinematics_(test|contract_test)$'
# 仅在 T06 最终语义关闭门禁已实现之后，才可作为最终结项命令。
pwsh -NoProfile -File RobWork/scripts/industrialrobot/validate-reuse.ps1 -Head HEAD -RequireMigrationComplete
```

脚本 JUnit/JSON 与产品 gtest XML/per-exe ird-test-report.json 分开；证据含 SHA、命令、环境/依赖/配置、退出码及 passed/skipped/failed/notRun，生产正向与独立负实验标明真实路径。GUI 验收仍须 VS x64、QT_QPA_PLATFORM=windows、绝对路径逐 exe；模型测试不强制 GUI 插件。独立冒烟按根 AGENTS §4 配置，不冒充集成交付。

### 12.10 风险、回滚与当前建议

| 风险 | 处置与回滚边界 |
| --- | --- |
| 门禁误报/漏检或状态循环 | 隔离正负夹具锁定准入；新旧 schema 同批迁移，回滚整个门禁能力提交，保留原 GOV-T21 强制规则，不降为 warning 放行 |
| 接口/ABI/DTO 不兼容 | T02 先核全部消费者与版本；按已批准接口方案迁移，回滚受影响接口及消费者完整提交组，不悄改虚表 |
| 数值/坐标/载荷行为差异 | 上游对照＋独立 oracle＋需求容差三方核对；差异未解释阻断能力验收，不重产黄金值/放宽容差洗绿 |
| 上游能力不足/后端缺失 | 单独缺口提案和批准；不提前写核心，不把仿真环境缺供伪报算法通过 |
| 性能/确定性/线程资源退化 | 冻结代表模型/硬件/配置/基线测量，查会话复用及 State 独占；回滚完整能力提交/制品，不恢复隐蔽双路径 |
| 上游版本升级或证据过期 | catalog 锁定真实版本、符号/目标和相关摘要；触发受影响能力重验，不自动沿用旧 verified |
| 外部网络/原有 CI 失败 | 区分当前变更与存量原因；保留失败，必需远程检查未完成不宣称远程交付/最终验收 |

**当前下一项产品交付为REUSE-T07：CouplingMath调用上游svd并删除自写Jacobi。** 旧工作包0～2范围及历史验收保持；后续按§12.11能力准入推进。本次规划不批准扩展/新核心，不自动改变任何 planned 任务状态。

## 12.11 所有者批准的能力交付调整（2026-10-23）

所有者明确要求停止无限扩展诊断小批次深审，改为按能力完成真实替换。本节更新执行顺序，不降低H1—H6、SA-19、上游只读、公共兼容或新核心/扩展逐项批准要求。

- T01持续对全部1401文件作覆盖筛查；数值核心、模型权限和准备替换范围深入，访问器/同形比较/枚举映射按明确同类规则分组，未审仍待审。不以批次/矩阵条数表示完成率。
- T02～T05采用精确能力子任务：写明生产范围、功能/依赖审计、上游来源、权限/适配边界、GOV-T21/GOV-T22冻结验收引用、验证和整包独立验收。原校验器已按变动文件/功能证据检查，不存在必须T01全库done的机器豁免；本次同步原文与任务dependsOn，未放松权限、完整行覆盖、正向/断开/oracle/上游对照或关闭记录。
- 实施与测试可以在当前任务分支进行；标记能力已验收、关闭债务、放行依赖或合入，必须由独立会话验收完整冻结能力包。实施者不自签；最新AGENTS禁止自动派子代理，既有在途结果保留。
- 第一包modeling/src/CouplingMath.hpp→rw::math::LinearAlgebra::svd，保持阈值/接口/诊断，删除Jacobi核心。随后旋转/位姿→runtime移动offset与映射→Device/State FK/Jacobian→Polynomial::evaluateDerivatives高阶轨迹→满足契约IK→RNE→具备现成后端证据的FD。每包审计真实依赖后执行，禁止运行时双算法/fallback/第三框架。
- GAP-IK-01、GAP-FD-BUILD-01、GAP-DIGEST-01均未获扩展、新依赖或新核心批准，仅阻断相应依赖能力；其余无依赖工作继续。B30～B33冻结验收原件保留，B34～B36已有必要材料最小保存，不再扩展诊断深审。
- functions/catalog是唯一功能数据源，可按direct/adapter、既有适配待核、proposal受阻、NONE业务专属待确认及待审生成视图；不另维护人工进度台账。主指标是独验通过的实际替换能力、已删重复核心、生产消费证据及剩余阻断。
- 相同冻结版本相同门禁只执行一次，未变原件直接引用；产品/CMake/公共接口执行受影响集成、必要独立冒烟、对端回归。T06仍要求全范围关闭与最终独验；不变库存分类，不把局部通过当全项目完成。

继续隔离reuse-audit工作树和任务分支；不合入redesign-main，不触碰main，不执行push/fetch/pull或远程排查。回滚完整能力提交/制品，不重新启用隐藏旧核心。
