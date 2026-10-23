# GAP-IK-01：受限任务误差与迭代可观测性的最小扩展提案

状态：**Draft / 未批准，不得实施 patch 或新增 IK 核心**。本提案不是 REUSE-T01 退出证明，也未声称已检尽上游。需求与架构不改：KIN-02/13、UX-03，任务约束语义见 units/kinematics.md 的受限 IK 接口；ARCH §3.6 / SA-19 继续约束模型所有权。检索、版本与摘要见同目录 `proposal-search.json`，运行反例在 `traceability/reuse/REUSE-T01/probe-r3/`。

## 1. 已证实差异及仍可直接复用的部分

| 功能 | 原库事实与证据 | 边界 |
| --- | --- | --- |
| 完整位姿 | JacobianIKSolver::solveLocal 接收 State、maxIter、单一 maxError；公开 setSolverType/setClampToBounds 可用 | 优先原API，结束后以任务实际位置/姿态容差独立复核，不另写DLS |
| 仅位置 | ParallelIKSolver::Target 有6行掩码；同一帧链单leg XYZ探测通过 | 不谎称原库无掩码；ParallelDevice视图的构造权限须在runtime侧审查，不能在业务自建链 |
| 部分姿态 | Target::enabled 明确是 EAA x/y/z；产品含 referenceInBase 下 Euler 分量和自由工具滚转 | EAA行不是Euler行；probe-r2中固定Euler yaw/pitch但改变roll仍产生不同EAA已选分量，不可直接套掩码 |
| 结果正确性 | Jacobian局部求解不可达Rx(pi)可误报成功；ParallelIK预算耗尽后可返回半径残差1m的候选 | 非空vector/成功bool均需独立任务残差校验；这些保护可在薄适配完成，无需为此单独patch |
| 取消/统计 | IterativeIK只有最大次数配置；solveLocal给bool及最终State，没有真实执行次数/停止原因回传 | 不能用配置上限伪造实际迭代数；每次maxIter=1反复调用改变内部算法边界，不能先验宣称等价 |

QPController::solve 接收固定6D VelocityScrew，IKQPSolver内部performLocalSearch私有。它们保留为备选，但当前公开头未提供任意任务残差/行Jacobian注入；尚未据此宣称全部算法库无此能力。

## 2. 首选最小变更形状（待批准）

只在原 `rw/invkin/ParallelIKSolver.hpp/.cpp` 增加一个**非虚**重载及少量独立值结构，复用原有单次迭代、结点闭合、SVD与关节限位代码。不得另增产品求解器、复制主循环、修改 IterativeIK 虚表或给既有类追加成员。目标继续 `sdurw_invkin`；新增gtest放既有 `gtest/invkin/ParallelIKSolverTest.cpp`。是否需要JacobianIKSolver同类观测接口，应由完整位姿路径审计再决定，不能一并扩大patch。

拟议接口职责（名称与完整签名需批准后的设计审查冻结，当前不是可编译代码）：

- `TaskLinearization`：本次任务误差和对应Jacobian行，采用既有 Q/Jacobian 类型。仅替换目标任务行；并联机构原有闭合约束行仍由原库生成。
- `SolveOptions`：可选任务线性化回调、合作取消查询、每行容差/预算。无回调走原任务定义；位置m与角度rad不得混用同一无解释标量阈值。
- `SolveReport`：真实迭代/残差求值次数、Converged/IterationLimit/Cancelled/NumericFailure/BoundsRejected等通用停止原因；原库不引入产品稳定码、ResultEnvelope或诊断文案。
- 非虚 `solve(targets, initialState, options, report)`：新路径明确只返回通过任务残差与边界复验的候选；已有重载保留既有ABI/API及原行为。共同主循环以显式策略参数复用，不能出现两份数值内核。若无法在不改变布局/旧行为下复用，应退回重新审查，不偷偷改变ABI。

回调仅描述任务的参考系、启用分量及其微分关系，不执行数值迭代/消元/步长搜索。FK/Jacobian仍取原Device公共API；是否采用解析Euler微分或原库已存在的任务映射，要继续检索并以奇异位形反例证明。自由滚转集合的最近目标规则也要明确验证，不能只把当前完整位姿目标后过滤。**这部分任务数学若确需新增，属于本提案需逐项批准的最小扩展范围，不因叫adapter自动获准。** 不引入任务插件注册表、通用优化平台或第二套数值类型。

## 3. 架构与影响范围

runtime继续拥有WC/DWC与名称、offset/关节/TCP映射。求解会话持快照保活的受控视图，State和solver任务私有；如需单leg模型视图，由runtime既有工厂或经T02审查的最小能力提供。业务禁止new/load/clone WorkCell。worker只传冻结描述与身份，在进程内物化，不跨进程传Frame/State指针。

产品接线起点为 `kinematics/src/Ik.cpp` 的 IkSolver::solve、constrainedPoseError、constrainedDampedLeastSquaresStep、nearestToolRollTarget；替换后删除自持DLS/差分核心，保留初值编排、碰撞硬条件、关节空间去重/稳定排序、payload/provenance。evidence负责结果包络，diagnostics负责稳定码。公共产品请求/结果契约若因真实统计口径变化须同步版本和缓存身份，不能默默复用旧结果。

API新增符号需重编 `sdurw_invkin` 及产品依赖；不更换vcpkg/Eigen或许可证。继承Apache-2.0头与原目录风格。回调每迭代增加一次间接调用，其耗时、分配与取消延迟须计入对照，不预先承诺无性能损耗。最终patch须登记 `industrialrobot/patches/PATCHES.md` 并保存基点/摘要；本提案阶段不修改框架或PATCHES登记为已批准。

## 4. 被否/保留的替代方案

| 方案 | 处理与原因 |
| --- | --- |
| 直接原API + 独立复验 | 完整位姿/仅位置优先保留；尚不能解决任意Euler任务及真实内部统计 |
| 全位姿求解后按部分约束过滤 | 否：施加了用户未启用的约束，会错误丢弃合法构型 |
| EAA掩码冒充Euler掩码 | 否：已有可复现反例，参考系转换也不能一般等价 |
| 复制DLS/差分到产品或另建solver类 | 否：H1/H2禁止，无批准不能保留平行核心 |
| 每次公开solveLocal迭代1步的外循环 | 保留作进一步实证备选；不能把次数视为内部真实更新数，也未覆盖Parallel受限任务 |
| QP控制器 | 保留原库候选，需先证明任务行/速度/位置边界与求解预算适配，当前不选作通用替换 |

ARCH §9 的SA-19及原有被否备选继续唯一有效；本表是本能力候选比较，不另建架构权威。

## 5. 批准后验收与回滚

先通过旧上游IK测试，新增部分Euler、不同referenceInBase、自由滚转范围、仅位置、±pi/奇异位形、限位、不可达、零/耗尽预算、每次取消点和真实统计断言。对比同模型FK和独立几何/差分oracle；旧API结果/布局/导出符号与已冻结基线核查。产品真实入口完成断开/扰动正向证据、多任务State隔离、生命周期及worker边界、性能对照后才删除旧核。patch只在任务分支启用，不做旧自写算法生产fallback；失败回退该阶段整组提交并保持迁移未完成。

批准项必须明确覆盖：①是否允许上述原库最小扩展；②任务数学具体范围；③旧/新API行为与ABI承诺；④产品契约版本和性能预算。未取得具体批准时，只继续独立审计，依赖该能力的T03-B不得ready或实现。
