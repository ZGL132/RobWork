# 上游复用迁移计划（2026-10-22）

> **当前执行入口：§13 主工作树串行工作包（2026-10-24）。** §1～§12 保留历史审计、决策与证据来源，其中旧的下一任务、任务分支、A/B 并行及合入安排不再用于当前调度；H1—H6、权限与独验阶段门不变。

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

1. 补精确功能矩阵、调用链与验收，并按 policy §1.1 在既有任务/迁移说明中完成“保留/删除/适配/职责归属”设计判断；先运行原有有效回归建立基线，旧缺陷如实登记，不把旧输出当数学真值。
2. 写能区分生产是否消费上游的测试与独立 oracle，先确认其能拒绝当前错误/断开路径；复用现有 GTest/testkit，避免新通用测试框架。
3. 在精确范围内按上游能力重构内部结构，使用已批准薄适配/直接调用接入真实生产入口；同包删除被替代核心、无用数据结构/转换/缓存/包装、死依赖和隐藏 fallback。必要保留项说明理由，不为复用扭曲业务契约。
4. 运行上游直接对照、隔离扰动/断开实验、相关回归/性能及权限/复用门禁；适配依赖或接口变动增加双模式与对端契约。
5. 更新源码中文契约、相关单元卡/catalog/矩阵/changes 和真实证据，按 policy §1.1 交付准确性、架构、简洁性和运行效果四维结论；冻结能力提交，由独立会话验收，所有者决定合入，再推进依赖能力。

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

**历史调度快照：当时下一项为 REUSE-T07。该包已有成果，当前调度以 §13 为准，不重复实施。** 旧工作包0～2范围及历史验收保持；本次规划不批准扩展/新核心，不自动改变任何 planned 任务状态。

## 12.11 所有者批准的能力交付调整（2026-10-23）

所有者明确要求停止无限扩展诊断小批次深审，改为按能力完成真实替换。本节更新执行顺序，不降低H1—H6、SA-19、上游只读、公共兼容或新核心/扩展逐项批准要求。

每项交付是按能力完成局部架构重构，执行 policy §1.1 的设计与四维验收；包括当前 REUSE-T07，不得把函数替换或上游调用当成完成标准。保留必要业务契约，重审旧内部结构，按最小范围删除冗余；不扩大为全项目推倒重写。

复用范围与投入按 policy §1.2：全部相关功能持续审计，但不接入无需求的上游能力，不追求复用数量。先收口已实施SVD/EAA能力包的受影响验证、冗余清理、实际性能判定和独验，再推进产品旋转、runtime映射及FK/Jacobian等高价值核心；允许开展未依赖前包验收的调查/设计，不提前放行依赖实施。已有正确RRT/碰撞适配核查后保留；通用工具只在语义匹配且整体收益明确时迁移，其余排队，不抢占无关核心主线。测试oracle保持必要独立性，业务专属保留须有差异证据，不改变H1—H6或缺口批准门槛。

- T01持续对全部1401文件作覆盖筛查；数值核心、模型权限和准备替换范围深入，访问器/同形比较/枚举映射按明确同类规则分组，未审仍待审。不以批次/矩阵条数表示完成率。
- T02～T05采用精确能力子任务：写明生产范围、功能/依赖审计、上游来源、权限/适配边界、GOV-T21/GOV-T22冻结验收引用、验证和整包独立验收。原校验器已按变动文件/功能证据检查，不存在必须T01全库done的机器豁免；本次同步原文与任务dependsOn，未放松权限、完整行覆盖、正向/断开/oracle/上游对照或关闭记录。
- 实施与测试可以在当前任务分支进行；标记能力已验收、关闭债务、放行依赖或合入，必须由独立会话验收完整冻结能力包。实施者不自签；最新AGENTS禁止自动派子代理，既有在途结果保留。
- 第一包modeling/src/CouplingMath.hpp→rw::math::LinearAlgebra::svd，保持阈值/接口/诊断，删除Jacobi核心。随后旋转/位姿→runtime移动offset与映射→Device/State FK/Jacobian→Polynomial::evaluateDerivatives高阶轨迹→满足契约IK→RNE→具备现成后端证据的FD。每包审计真实依赖后执行，禁止运行时双算法/fallback/第三框架。
- GAP-IK-01、GAP-FD-BUILD-01、GAP-DIGEST-01均未获扩展、新依赖或新核心批准，仅阻断相应依赖能力；其余无依赖工作继续。B30～B33冻结验收原件保留，B34～B36已有必要材料最小保存，不再扩展诊断深审。
- functions/catalog是唯一功能数据源，可按direct/adapter、既有适配待核、proposal受阻、NONE业务专属待确认及待审生成视图；不另维护人工进度台账。主指标是独验通过的实际替换能力、已删重复核心、生产消费证据及剩余阻断。
- 相同冻结版本相同门禁只执行一次，未变原件直接引用；产品/CMake/公共接口执行受影响集成、必要独立冒烟、对端回归。T06仍要求全范围关闭与最终独验；不变库存分类，不把局部通过当全项目完成。

以上为历史隔离实施安排；当前主工作树与提交方式由 §13 取代。不触碰 main，不执行 push/fetch/pull 或远程排查。回滚完整能力提交/制品，不重新启用隐藏旧核心。

## 13. 主工作树串行工作包（2026-10-24）

### 13.1 执行现场、授权与续接入口

所有者已批准本节计划并要求先实施 WP-00。唯一实施现场为 `D:\10_Source_Repos\21_robot\RobWork`、分支 `redesign-main`；参考起点 `58962b338505fc9f69b6a9f1a50e2d55712efffc`，核查时工作区干净。新会话核对实际 HEAD/状态，承接新增提交，不能重置到参考起点。取消 A/B 并行开发；旧工作树和历史证据保留，不再按旧 HEAD 调度。禁止远程同步、框架/vcpkg 修改和自动派子智能体。

**当前续接**：WP-00 / [GOV-T23](../tasks/GOV-T23.json) 已完成，R2 独验 PASS 绑定 `85cfc8f570edc66fa7b20195e75e3587ab9c8e3d`，见 [独验记录](../traceability/acceptance/GOV-T23-20261024-r2.json)；首轮 FAIL 和返工证据保留于 [本包原始记录](../traceability/reuse/WP-00/README.md)。WP-01 / [GOV-T24](../tasks/GOV-T24.json) 已完成：IRD-GATE-LIB 三变量阻断经 kinematics 条件分支合法接线消除（门禁代码/词表零改动），冒烟 RobWork 导出包供给断链修复（core 首单元前置绑定 Threads/synchronization＋冒烟配置参数 VCPKG_OVERRIDE_FIND_PACKAGE_NAME/RobWork_DIR）并实现 T07 引入 find_package(RobWork) 以来首次全树独立冒烟配置；独立验收 PASS（所有者明确指令的单次验收子智能体，10/10 项）绑定 `df0b35fc2b32068dcfe3745d1b29a81c58d17f77`，见 [独验记录](../traceability/acceptance/GOV-T24-20261024-r1.json)，实施与复现原始证据见 [govt24-wp01-20261024](../traceability/reuse/REUSE-T19/govt24-wp01-20261024/README.md)。遗留：冒烟 kinematics test/contract exe 因 T19 遗留缺口（Fk.cpp 两模式编译而 runtime WorkCellConstView 实现集成专属）NOT RUN（LNK2019 原件在案），归 T19 语义/后续包处置。当前 WP-02 / [REUSE-T20](../tasks/REUSE-T20.json) 实现与自查完成、仍ready待独验：起点`7be67a6a59912ec243f2251dd3a127b8b5c7e679`，纠正标题后实现SHA=`9276f19b26bfe095cbe2ccf460a64dec39bf45e9`（代码树/正文不变，commit-lint和显式范围检查PASS）。所有者已接受1024稠密118.73ms对旧3.26ms及O(E)图内存，并选择第三方schema方案一保留表达债务；不假关整体合规或独验。原始数值/消费者/生产返回值扰动与批准见[原记录§9](../traceability/reuse/REUSE-T05/setcheck-max-matching/README.md#9-wp-02-串行收口2026-10-24)。本轮（2026-10-26）批量独验登记因子代理模型创建失败未完成，状态不变。必要独验由所有者另开会话、人工或所有者明确指令的单次验收子智能体完成（GOV-T23/T24 先例同型），实施者不得自签，不解除常规禁令。

**后续交接**：WP-03 / [GOV-T25](../tasks/GOV-T25.json) 定向交接核对已完成并独立验收 PASS 归档（送验 `0a091394b9466c3fa7521d34162b1c8b64a843c4`，见 [独验记录](../traceability/acceptance/GOV-T25-20261024-r1.json)），交接入口见 [WP-03 README](../traceability/reuse/WP-03/README.md)。T13 模型真值已验清无缺陷（`clean=true`：305/0/0 真实重跑＋25/25 独立 oracle，见 [t13-model-truth-20261025](../traceability/reuse/WP-03/t13-model-truth-20261025/README.md)；该记录是核验、不替代 T13 独验）——WP-04/WP-05 准入的模型真值项已满足，「放行依赖的普通 IK/RNE」裁决归所有者；T13/T15/T16 及 T07/T09/T12 六包任务级独验保持开放、由所有者安排（T07/T09 有 verdict=pass 记录在库而登记门禁未过，见本节末批量段）。T12旧coverage已修而性能NOT RUN；T07约4.662倍尚未获接受；T15仅core/CMake供给变化引用WP-01，算法输入未变；其他独立包不受无关遗留阻塞。

**独立推进**：WP-10 / [GOV-T26](../tasks/GOV-T26.json) 的 R3 有界实施核对已独立验收 PASS 归档（送验 `2136a89d5cd65e7183c734956df68291862e0fc0`，见 [独验记录](../traceability/acceptance/GOV-T26-20261024-r1.json)）。生产包[REUSE-T21](../tasks/REUSE-T21.json)已在原Compare单点接入既有Boost原语，见[最终产品原件](../traceability/reuse/REUSE-T21/README.md)：22831组精确对照与真实Q/受影响消费者自查通过，库结果扰动真实失败；正常路径/C7/公开接口保持，第三方表达债务保留。最终性能已获本包明确接受、独验NOT RUN（本轮批量登记亦执行异常未完成），F-966保持OPEN；不放行相关依赖或外推T20性能接受。

每次结束或受阻，只在本节更新当前入口、相关任务和下一动作；任务 JSON 仍是契约状态来源，functions/catalog 是功能数据来源，原始记录与独验文件是验证结论来源，不新增状态平台或重复证据总账。不改既有状态枚举：planned 尚未准入、ready 可在前提内实施/待独验、blocked 有明确阻断、done 达到本任务全部退出条件；实施完成待独验在原任务 note/证据注明，不能伪装 done。

开包时在既有任务或其引用说明中写清：文件/符号、业务契约、上游适用来源与被否候选、保留/删除/适配、唯一 owner、数据流、消费者、验证与可引用证据、回滚边界。正式 REUSE 编号先查重；下表 WP 编号只是计划别名。近期任务先细化，后续包临近执行再补可执行契约，不凭 planned 父任务任意改生产。

**WP-12当前精确子包**：[REUSE-T27](../tasks/REUSE-T27.json)惯量特征值、[REUSE-T28](../tasks/REUSE-T28.json)惯量旋转/长方体及获批相对自检已实施待独验，均未将WP-12标完成。后继[REUSE-T29](../tasks/REUSE-T29.json)为实际runtime owner的基座世界数学候选：GAP-BW-EAA-01方案A及第三方表达债务已批，53项runtime/19项直接消费者和3组扰动自查有效；所有者暂不接受本包性能，保持blocked，生产恢复起点，候选见[原始记录](../traceability/reuse/REUSE-T29/README.md)，不计已完成直接迁移。下一动作按依赖选择其他独立直接复用包；T13仍不放行普通IK/RNE。**WP-12残包推进（2026-10-25，缩减批定案）**：[REUSE-T33](../tasks/REUSE-T33.json)实施完成待独验——落地 C 插件面（rpyview 唯一实现换上游 RPY 反向换序、万向锁 roll=0 保留、PanelModel 副本删除 G1 收口），480项回归+断开必红+插件 STATIC 链接面实证，原件见[REUSE-T33](../traceability/reuse/REUSE-T33/README.md)；**A（Import.cpp 惯量旋转）/D（Template.cpp 占位圆柱）被 UI-T53 closures 冻结面拦截（acceptedPaths 逐字节比对含 Import.cpp/Template.cpp/TemplateTest.cpp）——blocked，候选+oracle+断开证据已完成留存，解冻须所有者对 closed 行启动验收**；[REUSE-T34](../tasks/REUSE-T34.json)（runtime，零生产变更）实施完成待独验——SVD 候选实测三规模 1.94~2.60× 退化按预声明规则不落地生产（与 T07 接受悬置并列裁决）、判定链 oracle 扩展+断开在案、CanonicalModel 两见证带证据保留（business-specific/audited），原件见[REUSE-T34](../traceability/reuse/REUSE-T34/README.md)；REUSE-T35（编号登记于本节，不另立 tasks/ 契约文件——planned 契约不可执行且无法承载联合范围门禁）（planned）吸收 A/D 候选落地（前置=解冻裁决）+DH 值代数/CanonicalBridge 换核（G2）+MATH-DH 终审；requirements 第三份同族反解移交 requirements 单元批次（G3）。WP-12 仍余 T29 性能、T35、A/D 解冻裁决及独验，不标完成。本轮（2026-10-26）批量登记：T27/T28 独验登记执行异常未完成；WP-12 收口门禁 validate-docs 对 [REUSE-T33](../tasks/REUSE-T33.json) 契约 FAIL（REUSE-POSITIVE : traceability/reuse/REUSE-T33/final.log）两轮修复仍未过，包保持未收口，各子包状态不变。

**WP-13 policy（2026-10-25 blocked → 2026-10-26 已落地）**：[REUSE-T36](../tasks/REUSE-T36.json)按所有者escalate裁决采T33 blocked先例收口（起点`0e14bf3e79e46a830ca1b882e4a67ecddf4335e7`，产品树保持HEAD零改动）：F-961修复候选已完成并真实验证——上游 ProximitySetupRule 无字面API实核（makeExclude/makeInclude全走convertToRegEx，hpp:147-178/.cpp:28-36）后按 findings.resolution 采用单点字面适配（`toUpstreamRulePattern` 镜像上游 REGEX 分支条件＋ECMAScript全集转义，普通名透传零扰动，恰5字节REGEX走上游字面分支），B18反例未修复码红（1 failed）转绿（3用例）＋候选构建全量214项回归，候选成品/测试/diff/红转绿证据留存[原始记录](../traceability/reuse/REUSE-T36/README.md)；随机/LHS候选独立审查零生产改动已交付——rw::math::Random（进程级全局boost态/seed(unsigned)32位截断/无任务私有流）、QSampler（无种子均匀采样非LHS）、rw全库无LHS/分层采样、RobWorkSim sandbox无优化目标注册，矩阵kinematics/optimization对应行补真实证据维持pending（实施归WP-19/后续精确子任务）。**2026-10-26 落地批（所有者批量批准落地裁决表第1~4项，解阻原全文件解决批blocked，起始`45e3e80ea`）**：候选diff对`45e3e80ea`干净应用（产品文件与候选全文件摘要逐位一致），policy全量214/214＋受影响四消费者（kinematics/trajectory/modeling/optimization）全绿；全文件解决批一次收口（18行migrated＋2 helper＋20 audit铺面＋三grant＋COLLISION candidate→verified）；disconnected-bps变异红例复验（去适配即回F-961同型反例）；落地实施中发现policy测试文件既有模型构造面的IRD-GATE-AD死锁，经所有者落地授权脉络裁决批准test-fixture例外（ARCH §3.6.2.1，独立治理批可回退待追认）；evaluate不消费setup不消费过滤，不声称碰撞误判修复；F-961 resolution改fixed，独立验收另行执行。

**WP-06/T02动力学映射**：[REUSE-T30](../tasks/REUSE-T30.json)实施完成待独验（起点`c816a2c482088b9d03d537c103cf9ac314259187`）：S7映射定向核＋3个oracle测试（非零offset参考点/固定段复合刚体多重集/RNE可消费性单z轴全分量手算），全量runtime 309项（308passed＋1既有skipped）、modeling消费者479项全过、mutation聚合断开在案；capability RUNTIME-DWC-MODEL（verified）与runtime.compileDynamicWorkCell等矩阵/凭证见[原始记录](../traceability/reuse/REUSE-T30/README.md)。2026-10-25所有者escalate裁决：GAP-RNE-FRAME（权威轴≠z时上游RNE按关节系解释masscenter错位）接受为登记缺口、包内不实施修复；B1/薄适配两落点见suitability §6供WP-07准入提案。本轮（2026-10-26）独验登记执行异常未完成，状态不变待独验。

**WP-08/FD后端供给探测**：[REUSE-T31](../tasks/REUSE-T31.json)实施完成、独立验收已 PASS 归档（起点`e74886dfcb972a2bbcc4f1336e22ab146037ffde`）：GAP-FD-BUILD-01供给定向探测全部只读实核——vcpkg port ode 0.16.6在位而未安装（installed零ode库、bullet3/fcl对照、BUILD_sdurwsim_ode=OFF/ODE_DIR-NOTFOUND、VCPKG_MANIFEST_MODE=OFF）、力驱动直连API链行号级核验（RigidDevice::setMotorForceTargets→ODEVelocityDevice::update Force分支setForce→ODESimulator::addDevice建ODEVelocityDevice）、Bullet/RWPhysics/RK4Integrator负面证据落位、部署握手（FindODE config包与vcpkg布局兼容；须规避RobWorkSimSetup WIN32的git网络拉取回退）；[GAP-FD-BUILD-01](GAP-FD-BUILD-01.md)已修订为可裁决供给提案（含阶段B验证设计与回滚边界），增量检索见[proposal-search.json](extensions/proposal-search.json)增量批，原始记录见[REUSE-T31](../traceability/reuse/REUSE-T31/README.md)。独立验收 PASS 已归档（送验 `e078984b29ba2a2862120e346db6a23712b750ba`、登记收口 `c65ffc4fc`，见 [独验记录](../traceability/acceptance/REUSE-T31-20261025-r1.json)），WP-08 收口 done。阶段B（安装ode＋隔离树启用BUILD_sdurwsim_ode＋力输入探针）属新依赖启用，未获所有者批准不实施；提案裁决仍由所有者安排；T04/WP-09阻断不变。

**WP-11 trajectory 其余工具**：[REUSE-T32](../tasks/REUSE-T32.json)实施完成、独立验收已 PASS 归档（起点`c65ffc4fc495da0b01b2060ff3049eb127091d72`；直线已由REUSE-T26交付）：Planner三处度量手写核换上游直接消费（逐对Linf→`MetricFactory::makeInfinity`、L1路径长度→`PathAnalyzer::pathLength`折叠`makeManhattan`、直连采样L2→`makeEuclidean`单对距离），两处内联Σ|Δq|循环删除并委托pathJointLength；193项全量（含PlannerAvoidance黄金算例回归）与69项契约全过，三组断开红例（L1折叠→Linf、采样L2恒0、规划期剪枝恒真）各自真实失败后恢复复测绿；MATH-METRIC/PATH-PLANNING升verified（suitability原件＋review.version树内上游逐字节一致核验在案）；rotationAngleBetween按实测反例保留2·atan2（EAA acos分支边界4.4e-11/1.6e-10超C7容差40~160倍，原"候选EAA::angle"待核项就此结案）；ird_add_gtest定案business-specific保留薄宏（TEST-HARNESS保持candidate）；矩阵95行含三改动文件全行铺面（采样计数公式级与RRT选择序直接oracle受零模型构造红线限制未建，观测面由黄金算例与断开红例承载）。GAP-TRJ-LINEAR-01候选2定案为无上游稳定求值入口（LinearInterpolator/Ramp差分式、Rotation3D特化slerp、Polynomial Horner同构差分），候选3（SA-02 patch）待所有者裁决维持blocked，批准前不改Ptp.cpp，真实Q极值探测NOT RUN；性能微基准pathL1相对+24~41%（绝对+40ns/次，每次search仅K次键计算）；独立验收 PASS 已归档（送验 `b3e0cd1349edbd8c497f6fc2d9874433bd3efc3f`、登记收口 `2f0ee6dcc`，见 [独验记录](../traceability/acceptance/REUSE-T32-20261025-r1.json)），上述性能项随之裁决，WP-11 收口 done，残项＝REUSE-T22 已登记的 PTP 线性插值极值适配缺口（生产实施待批准、独立算法验收 NOT RUN）与候选3 裁决。原件见[REUSE-T32](../traceability/reuse/REUSE-T32/README.md)。

**WP-16 project（2026-10-25 blocked → 2026-10-26 已落地）**：[REUSE-T40](../tasks/REUSE-T40.json)（正式编号，REUSE-T30~T39已占用后查重定号；候选起点`d7874f57f1f0e2cde192a6c76e5b0f90a016306e`，落地批起始`44c8d59e4`）原按T33/T36 blocked先例收口（候选留存、产品树HEAD零改动）：F-962修复候选已完成并真实验证——候选scanForRecovery实算HEAD指向清单字节摘要与head.manifestDigest严格比对（复用既有codec::contentVersionOf/core摘要，与commit路径同口径，非GAP-DIGEST-01、不新建哈希、不自写摘要）＋用户诊断cause首败钉扎；候选五用例未修复码5/5红（错误HEAD摘要/合法时间字节篡改/公开open三反例全放行——B20独立复现同型）修复后project_test 287/287＋contract_test 32/32绿零回归，候选成品/测试/四diff/红绿证据留存[原始记录](../traceability/reuse/REUSE-T40/README.md)。**2026-10-26 落地批（所有者批量批准落地裁决表第1~4项，解阻原全文件解决批blocked，§13.3原包边界相应扩至全文件解决批）**：候选diff对`44c8d59e4`干净应用（产品四件与candidate/全件摘要逐位一致），project全量287/287＋contract 32/32＋B20三反例复跑5/5全绿＋ird_gates零命中；全文件解决批一次收口（TxEngine.cpp 22功能行migrated＋四文件217铺面行＋47行TxEngine.hpp inputs摘要级联同步＋changes/REUSE-T40.json凭证，permissions零新增——四文件无rw/rwsim类别或模型操作命中IRD-GATE-AD零触发）；ProjectStoreTest夹具head.manifestDigest误写元数据对象摘要随批订正为清单字节摘要；F-962改fixed，独立验收另行执行（§6.4身份/持久化类）。

**WP-19 批次一 requirements（2026-10-25）**：[REUSE-T42](../tasks/REUSE-T42.json) 实施完成待独验（正式编号自 REUSE-T42 起，T30—T41 已占用后查重定号；起点 `caea72c7bed3cf1fc2b432a3ce3ea1086006fe7a`）：G3（requirements 第三份同族反解移交项）定案——TemplateArray.cpp 正变换 rotationFromRpy 换上游 RPY(yaw,pitch,roll).toRotation3D()（MATH-ROT 直接 API、rpymath 同型；探针 12 用例位模式逐位恒等，normal 221 项/oracle 2 项/断开 2 项真实红/恢复 recheck 绿四面对照），反解保留产品唯一职责（上游 eps=1e-5 吸附带违约 REQ-10 1e-8rad 回归＋退化轴约定与提取元素差异三点实证，probe-r6 同证）；requirements.json 48 行全收口（TemplateArray.cpp 1491 行全行铺面、Sampling 分母消费一致性待核项关闭、targetEditRotation 因 4165 行面板覆盖墙保持 pending 如实登记）；原件见 [REUSE-T42](../traceability/reuse/REUSE-T42/README.md)。批次二 selection（2026-10-25）[REUSE-T43](../tasks/REUSE-T43.json) 实施完成待独验（起点 a445f48765215121e464f9cd9d07ad342f83ae76）——Backfill 对称特征值换上游 eigenDecompositionSymmetric（MATH-EIGEN 直接 API＋sdurw_math PRIVATE 库边/drivetrain T12 同款别名分支，独立冒烟绿；探针九用例容差口径实证非逐位等价而断言容差语义全过——附录 D 第 6/7 项），字节原语族（selection 两文件 14 行）按字节身份契约保留并立跨包决策记录（Boost.Endian 列为所有者批准项），TRAJ-LINEAR 候选否决（LinearInterpolator 不覆盖 §6.2 规则/分轨，两 flop 核适配零收益——catalog selection usage 同步移除）；selection.json 52 行全收口＋两改动文件全行铺面；原件见 [REUSE-T43](../traceability/reuse/REUSE-T43/README.md)。批次三 optimization（2026-10-25）[REUSE-T44](../tasks/REUSE-T44.json) 实施完成待独验（起点 feaf93dc33a19afdaf7a4a7a3bf60dc85fb4d2b5，零产品变更）——22 行 pending 全量收口 business-specific（T36 随机/LHS 审查结论收口＋S1—S4 检索在案；SplitMix64 种子化 LHS 身份进 digest 上游无满足契约能力），catalog MATH-RANDOM 移除 optimization:adapter usage（预绑冲突修正）；原件见 [REUSE-T44](../traceability/reuse/REUSE-T44/README.md)。批次四 reporting（2026-10-25）[REUSE-T45](../tasks/REUSE-T45.json) 实施完成待独验（起点 4d9ac4777d833af5eb0efdea6eabe5ace77ccd75，零产品变更）——Archive/构建宏 2 行收口 business-specific；ReportCodec.cpp 64 行保持 pending＝AD 结构墙登记（真实受限类别命中：ReportCodec.cpp:127 借用 rw::kinematics::State 只读指针，权限表无 reporting 授权——T36/T41 结构同型，解阻＝所有者批准借用类 grant；字节原语族处置引用 REUSE-T43 决策记录，Boost.Endian 批准前置＝先捕获字节基线——评审缺口2 顺序敏感项登记）；原件见 [REUSE-T45](../traceability/reuse/REUSE-T45/README.md)。批次五至九（2026-10-25）串行收口：批次五 workflow [REUSE-T46](../tasks/REUSE-T46.json)（9 行收口——CloseSupervisor 关停监督 UTIL-THREAD 族按业务协议保留面定案）；批次六 evidence [REUSE-T47](../tasks/REUSE-T47.json)（26 行收口——Snapshot/Slice/Evidence/Evaluator 字节原语族按 REUSE-T43 决策记录保留）；批次七 ui [REUSE-T48](../tasks/REUSE-T48.json)（QtTextBillboard/测试骨架 8 行收口；PreviewBackend/Gateway/UiPlugin 20 行保持 pending＝REUSE-AD 结构墙——宿主例外/权限表无对应授权，解阻与 F-967/T41 同链由所有者裁决）；批次八 testkit [REUSE-T49](../tasks/REUSE-T49.json)（64 行收口——JsonLite 上游无 JSON 库、Boost.JSON 需所有者批；F-959 按 §13.3 保持 blocked 登记不洗绿）；批次九 drivetrain [REUSE-T50](../tasks/REUSE-T50.json)（DriveTrainCodec 8 行按字节身份契约保留；catalog MATH-SVD 补 inverse 符号——T12 §5.1 建议落实；T12 性能 NOT RUN/独验为所有者侧动作仅引用）。均零产品代码变更、实施完成待独验；原件见各批 README。本轮（2026-10-26）批量独验登记执行异常未完成，九批状态不变；F-959 与 Boost.Endian/Boost.JSON 仍为所有者保留裁决项。

**本轮批量收口其余结论（2026-10-26）**：独验记录在库而登记门禁未过（任务未置 accepted，契约 status 不变）——[GOV-T21](../tasks/GOV-T21.json)（[r2 记录](../traceability/acceptance/GOV-T21-20261024-r2.json) verdict=pass；commit-lint 对推导区间 FAIL，[r1 log](../traceability/acceptance/GOV-T21-20261024-r1/05-commit-lint.log) 记 2/88、历史提交按 F-012 豁免不回写；status 保持 ready 直至所有者接收）；[GOV-T22](../tasks/GOV-T22.json)（[r1 记录](../traceability/acceptance/GOV-T22-20261024-r1.json) verdict=pass、第三轮本地治理验收 PASS 前两轮 fail 保留；commit-lint FAIL 5 项；远程推送受 127.0.0.1:17890 代理不可达阻断单列）；REUSE-T07（[r2 记录](../traceability/acceptance/REUSE-T07-20261024-r2.json) verdict=pass、送验 `53768464c`）与 REUSE-T09（[r2 记录](../traceability/acceptance/REUSE-T09-20261025-r2.json) verdict=pass、送验 `93ef0da15`）同型 commit-lint FAIL。验收未过：WP-15 两轮独立验收未通过（第二轮 [REUSE-T37-20261025-r2](../traceability/acceptance/REUSE-T37-20261025-r2.json) verdict=fail 现场在档未入库）——唯一未核销项＝ACC-BLOCKER·口径二象性待所有者裁决：按三契约 verify 原文命令在送验 HEAD `d7874f57f` 实测 exit 1×5，唯一失败点均 REUSE-POSITIVE : traceability/reuse/REUSE-T33/final.log；修复落点在 REUSE-T33/门禁口径、超出 WP-15 授权面。blocked/skipped 均已按位置登记：WP-05 skipped（依赖 WP-04）；WP-07 blocked（WP-06/REUSE-T30 独验与 T02 父任务未收口＋RNE 依赖放行裁决未获）；WP-09 blocked（GAP-FD-BUILD-01 供给未确认未批准＋WP-06 未完成＋推翻 P-DYN-7 无批准，登记于该 GAP 文件、required-proposals.md 与 ForwardDynamics.hpp:22-26）；WP-17 blocked（GAP-PRJ-DIAG-BOUNDARY-01 自 507db46bb 后待所有者裁决，TxEngine 两诊断工厂权威与项目级空 subject 语义冲突，候选/证据登记于 [REUSE-T25](../tasks/REUSE-T25.json) note）；WP-18 blocked（F-967 修复候选留存、所有者裁决采 T33/T36/T40 先例，登记于 [REUSE-T41](../tasks/REUSE-T41.json)、findings.json F-967 与 §13.2 行）；WP-20 blocked（REUSE-T01～T05 无一 done、GOV-T21 独立 pass 前置未满足、零债务输入未生成——inventory 1401 条 audit-required=1383（rg 复核一致）、functions 1764 行 pending 934（本轮盘点），前置不得由本包代做）。执行异常（非验收结论）：REUSE-T01（登记输出超限）与 REUSE-T10/T12/T13/T15/T16/T17/T18/T19/T20/T21/T22/T23/T24/T25/T26/T27/T28 的批量独验登记因子代理模型创建失败未完成——traceability/acceptance/ 下留有未入库的 r1/r2 记录草稿（如 REUSE-T13-20261025-r1、REUSE-T20-20261025-r1，均 verdict=pass），未经登记不构成验收结论、效力待所有者核验；各任务既有状态与已归档结论（T17 `1f4a871a8`、T19 `6da9cfa8f` 等）不变。**下一包指向＝WP-04 精确子契约编制与准入**（普通 IK 完整位姿模式）：模型真值项已由 T13 真值记录满足、目标文件无 CLOSURE 冻结，契约编制属本包自身工作；生产实施的放行裁决与各项独验仍按既有口径由所有者另开会话、人工或所有者明确指令的单次验收子智能体执行（GOV-T23/T24 先例同型），实施者不得自签。

**所有者保留事项（2026-10-26 盘点汇总，细节以各任务契约/记录为准）**：

1. T07 耦合 SVD 约 4.662 倍性能尚未获所有者接受；
2. T12 传动线性代数性能验收 NOT RUN（CMake REUSE-COVERAGE 遗留按 §13.2 先查最新证据）；
3. T07/T09/T12/T13/T15/T16 六包独立验收保持开放、由所有者安排；T13 模型真值已验清，放行依赖的普通 IK/RNE 裁决归所有者；
4. WP-02/REUSE-T20 待独验；第三方 schema 表达债务（方案一）保留；本次具体性能接受已记录，后续新增实质退化仍需另行裁决；
5. WP-10/REUSE-T21 独验 NOT RUN、F-966 保持 OPEN，不放行相关依赖或外推 T20 性能接受；最终生产适配、既有依赖及第三方表达待所有者裁决（GOV-T26 note）；
6. REUSE-T29/WP-12 基座世界数学及旋转惯量候选：所有者暂不接受本包性能，保持 blocked、生产恢复起点；
7. REUSE-T25/WP-17 TxEngine 两诊断工厂权威与项目级空 subject 语义冲突，无法诚实满足整文件 migrated 准入，候选/patch 已存待所有者裁决；
8. REUSE-T22/WP-11 PTP 线性插值极值适配缺口已登记，生产实施待批准（不批准新核心或框架改动；生产及独立算法验收 NOT RUN）；
9. REUSE-T23/T24/T26/T27/T28 实施自查完成、独验 NOT RUN 待所有者安排（T26/T27 具体性能开销已获明确接受并交独验）；
10. GOV-T21 status 保持 ready 直至所有者接收（不自签 done）；GOV-T22 远程推送受 127.0.0.1:17890 代理不可达阻断、远程 CI/保护分支验证未做（所有者禁止远程同步，远程交付状态单列）；
11. T19 遗留缺口：冒烟 kinematics test/contract exe 因 Fk.cpp 两模式编译而 runtime WorkCellConstView 实现集成专属 NOT RUN（LNK2019 原件在案），归 T19 语义/后续包处置；
12. UI F-560 字符串钉扎历史失败保留（§13.2 主线集成/冒烟原记录行；未受影响 UI 不返工）。

### 13.2 交接索引与已知阻断

交接适用版本为本节参考起点及其包含的集成提交；历史 README 的“下一任务/未提交/待整合”结合后续提交判断，不复制完整证据。

| 来源与范围 | 当前承接与遗留 | 责任 |
| --- | --- | --- |
| 主实施链 `82cd82ff7`；T01/README、functions、T11/README、extensions/required-proposals.md | 全项目审计未全完成；已验证成果直接引用，不重新全库深查 | 本轮实施者按能力补审；所有者裁决缺口 |
| T12 `bc212f245`；tasks/REUSE-T12.json、reuse/drivetrain-linalg-migration.md、traceability/reuse/drivetrain-linalg/README.md | SVD/求逆已替换；性能、独验、CMake REUSE-COVERAGE 遗留先查最新证据，不能因集成关闭 | WP-03 核对，实际返工先落实精确任务 |
| T09 `8db7a7e78`、`aee6ac5bf`、`b0997f8d8`；tasks/REUSE-T09.json、reuse/trajectory-polynomial-migration.md、traceability/reuse/REUSE-T09/README.md | Polynomial 已生产调用；189项与69项契约、断开及性能证据按适用性引用；独验以实际独立记录为准 | WP-03 核对；WP-11 不重写该成果 |
| A/B 最后整合 `37a07c809`、`60bcddf1c`、`58962b338`；T17、T19、T20 原记录 | T17 接缝、T19 FK/Jacobian、T18 监听器修复已集成，不重复实施 | 本轮实施者承接稳定端口 |
| T13、T15、T16 各任务及同名 traceability/reuse 记录 | 已实施，尚需核实/补足独验；FNV 本包性能开销已获所有者接受 | WP-03 准备；所有者安排独验 |
| T19/integration-20261024/a-build.log | kinematics 三个局部 CMake 目标变量被 IRD-GATE-LIB 静态扫描拒绝；实际链接成功不代替门禁通过 | WP-01 |
| T20：traceability/reuse/REUSE-T05/setcheck-max-matching/README.md §8.3/后续集成记录 | Boost 来源表达、稠密场景约220倍退化和最终独验未闭合；不得伪装 RobWork API/NONE 或恢复自写 fallback | WP-02；性能接受及独验由所有者决定 |
| 主线集成/冒烟原记录 | 冒烟 RobWork 导出包路径/供给失败；UI F-560 字符串钉扎历史失败保留 | WP-01 定向核供给；未受影响 UI 不返工 |
| REUSE-T30 `f296c73e`；tasks/REUSE-T30.json、traceability/reuse/REUSE-T30/suitability.md §6、functions/runtime.json GAP-RNE-FRAME 登记及 migration §13.1 | 上游 RNE/RigidLink 消费假设（body frame=关节帧）与产品承载帧结构（权威连杆系承载帧）错配：权威轴≠z 时质心世界位置错位（探针 τ_x=±5.886 在案）；S7 映射本身符合 Body.hpp/§8.2 契约（所有者 2026-10-25 裁决接受为登记缺口） | B1（S7 改填 R_alignᵀ 变换值）或 RNE 消费边界薄适配（H3）须经提案批准后实施；WP-07/WP-09 在缺陷处置前不得实施依赖部分 |
| REUSE-T36 `candidate/`（traceability/reuse/REUSE-T36/）；tasks/REUSE-T36.json、findings.json F-961、migration §13.1/§13.3 | **已落地（2026-10-26 所有者批量批准落地裁决表第 1~4 项，解阻原全文件解决批 blocked）**：候选 diff 对 `45e3e80ea` 干净应用（产品文件与候选全文件摘要逐位一致），policy 全量 214/214＋受影响四消费者全绿；全文件解决批一次收口（18 行 migrated＋2 helper＋20 audit 铺面＋三 grant＋COLLISION candidate→verified，升级审查证据 collision-capability-review.json）；disconnected-bps 变异红例复验（去适配即回 F-961 同型反例）＋恢复复测绿；落地实施中发现 policy 测试文件既有模型构造面的 IRD-GATE-AD 死锁（construct/mutate-model 强制 runtime-owner→unit 必须 runtime，无合法 grant 形态），经所有者落地授权脉络裁决批准 test-fixture 例外（ARCH §3.6.2.1，独立治理批可回退待追认）；F-961 resolution 改 fixed——实证范围止于公开 setup 规则输出正字性，evaluate 不消费 setup，不声称碰撞误判修复；独立验收另行执行 | ~~policy 单元迁移包（T05-policy 父任务）获所有者批准时按 candidate/*.diff 低成本落地修复并同批完成门禁全文件解决~~（已由落地批完成）；随机/LHS 审查结论（矩阵维持 pending）已交付不受阻 |
| REUSE-T40 `candidate/`（traceability/reuse/REUSE-T40/）；tasks/REUSE-T40.json、findings.json F-962、migration §13.1/§13.3 | **已落地（2026-10-26 所有者批量批准落地裁决表第 1~4 项，解阻原全文件解决批 blocked）**：候选 diff 对 `44c8d59e4` 干净应用（产品四件与候选全文件摘要逐位一致），project 全量 287/287＋contract 32/32＋B20 三反例复跑 5/5 全绿＋ird_gates 零命中（landing2-* 证据在案）；全文件解决批一次收口（TxEngine.cpp 22 功能行 migrated＋四文件 217 audit 铺面行＋47 行 TxEngine.hpp inputs 摘要级联同步＋changes/REUSE-T40.json 凭证）；permissions 零新增（四文件无 rw/rwsim 类别或模型操作命中，IRD-GATE-AD 零触发，与 T36 不同无需测试夹具例外）；ProjectStoreTest 夹具 head.manifestDigest 误写元数据对象摘要随批订正为清单字节摘要（§4.4.1）；F-962 改 fixed——实证范围＝恢复扫描与公开工厂对错误/失真 HEAD 摘要的拒绝（篡改/缺失/合法恢复/历史兼容四验收面），不声称超出 scanForRecovery④ 闭包校验的语义；独立验收另行执行（§6.4 身份/持久化类） | ~~project 单元迁移包（T05-project 父任务）获所有者批准时按 candidate/*.diff 低成本落地修复并同批完成门禁全文件解决~~（已由落地批完成）；候选阶段 blocked 留痕（§13.1/README §1—§4）保留作历史 |
| REUSE-T41 `candidate/`（traceability/reuse/REUSE-T41/）；tasks/REUSE-T41.json、findings.json F-967、migration §13.1/§13.3 | F-967（生产装配越界用 core::ReferenceEventBus 非生产组件＋投递线程数据竞争/RAII/关停序缺口）修复候选已完成并全量验证（生产装配换 execution::DomainEventBusImpl＋投递线程转接互斥＋关停 drain 序＋ui_plugin→execution 目标级边＋EventBus.hpp:144 批注更正；execution 157+38、ui_contract 63、core 74 全绿，ird_gates 正例零命中＋撤登记负例恰命中 SUB），但落地须改 UiPlugin.cpp/UiPlugin.hpp，触发复用门禁对变动文件的全文件权限扫描（REUSE-AD：categories 含 host/graphics→origin 必须 host-presentation→宿主例外封闭清单恰 4 文件机器冻结不含 UiPlugin——结构性无合法 grant，"基线后首改才入扫描集"的机器盲区如实登记）；**所有者 2026-10-25 escalate 裁决采 T33/T36/T40 blocked 先例：产品树 HEAD 零改动，候选留存；F-967 保持 open** | 解阻＝所有者以独立治理批次批准扩宿主例外面（HOST-WORKBENCH 类条目＋ARCH §3.6.2 呈现装配层审查记录）后按 candidate/*.diff 原样落地；随候选移交两独立线索：ui_test 13 项 redesign-main 未记录的真实数值链失败（源集与本包零交集）与 HEAD 既有 studio 启动段错误（GUI 冒烟阻断，双跑归因在案），均待所有者归类处置 |
| REUSE-T33 `01b770ae6`；traceability/reuse/REUSE-T33/final.log 绑定漂移（WP-12 收口门禁与 WP-15 独验三度阻断；tasks/REUSE-T33.json note 登记处置全文，2026-10-26 所有者派遣治理诊断处置） | 根因＝原 final.log（run-tests.ps1 `*>` 捕获的 gtest 控制台输出）含 291 个无效 UTF-8 字节位（PM04_AT29 两测试中文诊断经系统 ANSI 代码页 GBK 混入，区段 70269..70428、72303..72683），门禁 ReadText+Digest（validate-reuse.ps1:40 无显式解码）对同一文件在 PS5.1(.NET Framework)=f38b31ff… 与 pwsh7(.NET Core)=681754d3… 双运行时摘要必然分歧——681754d3→f38b31ff→再漂三次漂移皆因单值重钉于不同运行时，绑定目标不稳定而非任一方复算错误；处置＝final.log 全文件 GBK→严格 UTF-8 重存（ASCII 字节与 1154 行 CRLF 不变、中文诊断如实转码；原件存 git 历史 01b770ae6：树内 blob 837f7e8e…/工作树原始 sha256 e4b31849…），重存后双运行时摘要唯一＝c8e05abe…，production-rpy.json 绑定重钉（f38b31ff→c8e05abe）、modeling.json 与 REUSE-T33/verification.json 对 production-rpy.json 级联同步（7db05648→d2e0af4d）；validate-reuse 全量与 validate-docs T33/T34 契约在 PS5.1 与 pwsh 双运行时实测 exit 0——§13.1『ACC-BLOCKER·口径二象性待所有者裁决』（2026-10-26 批量收口行）与 WP-12 收口门禁 final.log FAIL 至此核销，后续复验以实际运行 exit code 为判据 | 后续证据生成纪律＝测试进程中文诊断按 UTF-8 输出、禁止混入非 UTF-8 字节进捕获日志；治理脚本 Digest 口径加固（显式解码或拒收无效 UTF-8 证据）超出本次授权未实施，待所有者裁决 |

T11 EAA 上游补丁保持撤销，局部旋转转换 1e-6 不推广到其他功能。T17/T19 一次集成裁决不等于 T13 已独验，也不自动放行未来依赖。T18 为监听器、T19 为 FK、T20 为 SetCheck，历史测试别名不作为再次占号理由。

### 13.3 顺序工作包与实施边界

路径默认相对 `industrialrobot/`；上游路径相对框架源码根。每包遵循：审清需求 → 验证上游 → 确定边界 → 重构替换 → 同包删冗余 → 增量验证及必要独验。未知具体符号在定向核查后写入任务，未查清不得放行生产修改。

| 包/任务入口 | 文件、复用来源与实施步骤 | 必要前提、验收和退出 |
| --- | --- | --- |
| WP-00 / GOV-T23 | 根/分区 AGENTS、migration/policy、CCP/ACC/PIPE；统一主树串行/本地提交/续接；复用现有 validate-task、scope 与治理测试补主线分支及显式 Base 检查；不动产品算法/catalog/permissions | 定向正负例、文档及任务检查，记录自查与独验分离；下一步 WP-01 |
| WP-01 / [GOV-T24](../tasks/GOV-T24.json) | kinematics/CMakeLists.txt 的三变量接线及 IRD-GATE-LIB；优先显式合法目标；定向核 modeling 的 RobWork 包供给，复用既有导出，不复制框架源清单/新 fallback | 契约先补确切供给修改文件及产品凭证再准入；受影响增量构建/门禁/必要冒烟；不得放宽未知依赖 |
| WP-02 / [REUSE-T20](../tasks/REUSE-T20.json) | testkit SetCheckMatch.cpp/相关 CMake 与原 README §8.3；拆测图构造/转换/算法/结果，验证 Boost 入口适用性；如实表达第三方来源；删除无用转换，禁止恢复旧匹配核心 | 当前已实施但未收口；专项性能满足标准或获明确接受、来源可追溯、最终版本独验；受阻继续独立包 |
| WP-03 / [GOV-T25](../tasks/GOV-T25.json) 引用 T13/T15/T16 | 不改生产，核已有数值/消费/所有权原证据，提交确切版本交独验；附查 T07/T09/T12 最新遗留，不重做已替换能力 | offset 正负/轴/安装/限位与旋转回归；FNV 精确 oracle/已接受性能；日志 keep 边界；发现缺陷另定返工范围；未验不得关闭 |
| WP-04 / T03 精确子任务待编制 | kinematics/src/Ik.cpp、JointMapping.hpp；验证 rw/invkin/JacobianIKSolver 的 solve/solveLocal；复用 T17 视图与私有 State，单点 q 映射，独立残差复核 | 先只迁移完整位姿普通模式；满足前置准入/模型真值；删除该模式自写 DLS/正规方程/重复链；IK 定向及消费者契约、不可达/限位/确定性 |
| WP-05 / GAP-IK-01 相关子任务 | 同 IK 入口分审位置、部分姿态、双容差、取消/迭代观测；比较已有上游入口，EAA 与 Euler 掩码不混同，后过滤不冒充约束求解 | 实质缺口先提案批准；不另写 DLS/差分。各模式残差、奇异/取消/误报收敛；所有消费者迁移后删除余下旧核心 |
| WP-06 / T02 动力学映射子任务 | runtime/src/DynamicWorkCellCompiler.cpp 及现有公共视图；定向核 Body/Device/State、质心/惯量/固定段/载荷/重力映射；复用 offset 既有修复 | runtime 唯一模型 owner、任务私有 State；不接管摩擦/时间策略，不跨进程传指针；接口变化先明确消费者并确认语义 |
| WP-07 / T04 RNE 子任务 | dynamics/src/InverseDynamics.cpp → rwsim/util/RecursiveNewtonEuler；核 solve 的 DOF+1 Wrench、参考系/索引及按关节类型投影 | 依赖 WP-06；移动关节反例必须正确，不能截尾或盲用 solveMotorTorques；删 Vec3/Mat3/RneaChain/buildRneaChain/computeGeometry/rneaTorques 等旧核；独立物理 oracle |
| WP-08 / GAP-FD-BUILD-01 | 现有 PhysicsEngine/ODE 等实际供给定向探测；核 Device、力驱动、状态/步长/确定性与部署；记录 Bullet/RWPhysics 已知不适用证据 | 仅适用性与供给提案；新依赖/扩展等未获批不实施，不把刚体积分器当关节求解器 |
| WP-09 / T04 FD 子任务 | dynamics/src/ForwardDynamics.cpp → WP-08 已验证引擎；保留公共输入/结果、单位和错误语义，删除质量阵/消元/RK4四阶段及重复状态 | 依赖 WP-06、WP-08 及所需批准；解析基准/平衡/能量/收敛/确定性及真实业务链，无隐藏 fallback |
| WP-10 / REUSE-T21 | R3有界实施已批；原Compare同时溢出分支接入既有Boost128/4224位，生产自查见本包原件 | 原A撤回、R2不入生产；最终性能已获本包明确接受、独验待完成，F-966保持OPEN；不拓展精确计算平台 |
| WP-11 / T05 trajectory 子任务 | 直线已交付（REUSE-T26待独验）；其余已实施待独验（REUSE-T32：Planner度量换核＋三文件全行铺面＋catalog两capability升verified）；Ptp.cpp仅余GAP候选3（SA-02）待所有者裁决 | 保留已实施 Smooth/TimeParam Polynomial；核端点/时标/导数/连续性；时间加权 RMS 不强套 Statistics；删旧系数/转换/缓存 |
| WP-12 / T05 modeling 子任务 | BaseWorldTransform/DescriptionValidator 按实际 owner 定位；modeling DH/惯量能力；优先 LinearAlgebra、RPY/EAA/Transform3D 及成熟标定入口 | 保留 CouplingMath 成果；模型真值归 runtime；每个数值子包独立坐标/单位/退化 oracle；LM/差分缺口不直接新写 |
| WP-13 / [REUSE-T36](../tasks/REUSE-T36.json) policy 子任务 | **已落地（2026-10-26 所有者批量批准，原 blocked 解阻）**：F-961 单点字面适配候选按 `candidate/*.diff` 对 `45e3e80ea` 落地（214/214＋四消费者全绿＋disconnected 变异复验）；全文件解决批一次收口（18 行 migrated＋逐行证据＋三 grant＋COLLISION verified＋区段铺面）；随机/LHS 候选独立审查已交付（矩阵维持pending）；不把未证实规则不匹配描述为碰撞漏检；test-fixture 治理例外（ARCH §3.6.2.1）独立治理批可回退待追认 | 门禁全文件解决批已按所有者批准完成；保留种子/分布/线程/确定性（Random全局态/32位种子/QSampler无分层已实核登记矩阵）；独立验收由所有者另行执行 |
| WP-14 / diagnostics 子任务 | F-968 原解析入口，失败后 partial names 最小事务提交/清理；复用既有错误轨 | diagnostics 稳定码/文案唯一权威；T16 不重写；中途失败/重复调用/残留验证 |
| WP-15 / core、io 工具子任务 | 摘要、基础数值、JSON/CSV/UTF-8 三类分批；SHA 按 GAP-DIGEST-01 验证同渠道成熟供给，先查既有 codec/工具/测试 | T15 FNV 保留；不新建序列化框架/新依赖渠道；业务公式与独立 oracle 不为凑复用而替换；每子包确定符号后准入 |
| WP-16 / [REUSE-T40](../tasks/REUSE-T40.json) project 子任务 | **已落地（2026-10-26 所有者批量批准，原 blocked 解阻）**：F-962 scanForRecovery 实算比对候选按 `candidate/*.diff` 对 `44c8d59e4` 落地（project 287/287＋contract 32/32＋B20 三反例 5/5＋ird_gates 全绿）；全文件解决批一次收口（22 行 migrated＋四文件 217 铺面行＋47 行 inputs 摘要级联＋逐行证据绑定 README）；permissions 零新增；ProjectStoreTest 夹具订正随批落地；不声称超出④闭包校验语义的修复面；摘要供给前置已说明＝复用既有 codec::contentVersionOf（H1 直接 API），非 GAP-DIGEST-01 | project 唯一修订/写 owner；篡改/缺失/合法恢复/历史兼容四面复跑在案（landing2-*）；不自写摘要；独立验收由所有者另行执行（§6.4 身份/持久化类） |
| WP-17 / io 子任务 | F-963 磁盘满错误映射，核现有 SDK 与转换入口，删除错误硬编码 | SDK 正确映射与可控故障注入验证，不再建数字表，不制造无必要磁盘耗尽 |
| WP-18 / execution、ui 两子任务 | F-964 既有状态机先验证再赋值，修拒 Completed 后半写；F-967 核 ReferenceEventBus 生产装配，优先 execution 现有总线与订阅生命周期 | 两项独立回滚；不重造状态机/第三总线；拒绝前后状态、退订/销毁、线程及真实 UI 接线 |
| WP-19 / 剩余单元子任务 | 依次 requirements→selection→optimization→reporting→workflow→evidence→ui→testkit，附 drivetrain 遗留；沿已有矩阵反查隐藏 helper/工具/脚本/构建 | 保留合理适配和业务规则；不把业务搬 core；不扩宿主例外/模型多 owner；F-959 黄金生成脚本先修契约再审差异，不能洗绿 |
| WP-20 / T06 最终收口 | 全20单元 functions/catalog 复核，关闭记录追溯；删死核心/转换/缓存/依赖/包装，查测试/备用路径回流生产 | 对新跨包影响增量集成、最终模式及确切版本独验；引用仍适用证据，未批/未验/本次缺陷不假关；不以全接入上游或清空无关债务为目标 |

### 13.4 调度、验证与交付约束

建议按 WP-00→WP-20 顺序，真正依赖为普通 IK 的稳定视图/准入、RNE 的模型映射、FD 的后端供给、C4 的修订批准。某提案受阻只停止相关改动，在原任务登记原因/消费者并继续下一无依赖包；影响模型真值、公共契约或必要门禁的缺陷未处理前，不推进依赖实施。计划登记不自动批准扩展/新核心或放宽性能与权限。

- 任务契约复用现有 schema/status；开包先登记起始 SHA、实际范围与验证命令。主线范围检查必须显式 Base/Head；不得用主线自比较零改动冒充通过。verify-task 不替代独验/语义批准。
- 文件/功能变更及来源、owner、批准、生产正向/独立正确性与证据新鲜度继续使用现有 validate-task/validate-reuse/verify-task-scope/架构门禁；这些检查不能自动证明所有依赖完成或批准真实。领取和验收时必须人工核对任务前置与独立记录，不宣称新自动调度规则已实现。
- 治理文档只跑相关文档/任务检查；脚本只跑变动规则正负例。私有实现跑受影响目标与测试；公共接口、CMake、依赖和核心数值扩大到实际消费者、必要冒烟/数值对照；不默认全库重配或全量构建。有效证据按源码输入、相关依赖/工具链/配置/测试输入及范围判断，不只看 HEAD。
- 同包稳定后一次同步实际受影响的矩阵/凭证/文档，批次命令去重，不重跑全部历史治理夹具、不刷新无关摘要。源文件完整功能覆盖规则继续有效，不能以拆文件/虚假 migrated 规避。
- 数值独立 oracle、真实上游消费、唯一模型/状态所有权、相关业务回归和必要一次独验必须保留；Windows GUI 按根规则，纯模型不引 GUI。PASS/FAIL/SKIP/NOT RUN 分列，继承失败不能写通过。
- 每包交付：确切版本、复用来源/数据流、删冗余清单及保留理由、原始验证与独验链接、稳定接口/限制、阻断消费者。回滚本包完整增量和相关消费者，不覆盖他人、不改历史、不恢复隐藏平行核心、不操作远程。

WP-00 只验入口可定位、旧安排不冲突、近期契约范围明确和新规则定向结果；不构建产品，不把 WP-01 既知 IRD-GATE-LIB/供给失败改为通过。本节长期作为唯一人工调度入口，任务/功能/证据事实各从原来源引用。
