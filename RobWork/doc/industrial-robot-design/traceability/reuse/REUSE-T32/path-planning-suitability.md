# PATH-PLANNING 适用性报告（REUSE-T32，catalog candidate→verified 前置）

capability：`PATH-PLANNING`（reuse/catalog.json）。review.version＝`c65ffc4fc495da0b01b2060ff3049eb127091d72`（本包起点；框架只读，该树内上游文件与当前工作树逐字节一致——validate-reuse SUITABILITY 检查项复核）。本报告补足 boundary 中"生产接线待运行核验"的缺口；生产扰动证据见同目录 production-search.json / production-doincollision.json 与 README。

## 1. 上游消费审计（本包 2026-10-25 定向阅读）

`trajectory/src/Planner.cpp`（sdurw_pathplanners/sdurw_pathplanning 真实消费，行号为 REUSE-T32 后）：

- `RRTPlanner::makeQToQPlanner(plannerConstraint, sampler, metric, extend, RRTConnect)`（rwlibs/pathplanners/rrt/RRTPlanner.hpp）——K 轮候选供给，黄金算例锁定选型。
- `QConstraint::makeBounds / makeMerged`（rw/pathplanning/QConstraint.hpp）＋产品子类 `PolicySessionQConstraint::doInCollision`（每查询直穿 policy 会话——④端口，KIN-05 不可采信保守剪枝）。
- `QEdgeConstraint::make(merged, metric, resolution)`（直线插值离散边检查）。
- `QSampler::makeUniform / makeConstrained`（设备 bounds 均匀采样＋合并约束过滤）。
- `PlannerConstraint::make / StopCriteria`：产品按剩余秒数传 query 预算（boundary 登记的"仅传秒数"口径保持，StopCriteria 组合为后续独立事项）。
- `rw::math::Math::seed` 进程级种子化（uint64→unsigned 确定性截断，NFR-COR-02）。

## 2. 运行核验（本包真实运行）

- 黄金算例：`TrjPlannerAvoidance.CollisionThenReworkSucceeds_WP16T06_ACC1_ACC2`（直连必碰→淘汰→种子化 RRT-Connect 重规划成功→同种子重放采纳段逐点相等）；`DirectClearAdoptsStraightPath`（直连清晰不经搜索）；`BudgetExhaustedYieldsMaterialNotInfeasible`（1DOF 密闭→素材轨不判不可行）；`UntrustedReviewNotTreatedAsCollision`（finalized=false 保守剪枝，KIN-05）。
- RRT/采样计数直接 oracle 测试受零模型构造红线（IRD-GATE-AD）限制未建；观测面由黄金算例承载（同种子重放采纳段逐点相等断言内置）。
- 真实扰动（disconnected-cll.*）：产品 QConstraint 适配器"无碰撞发现也剪枝"变异→CollisionThenRework/ReworkAdopt 2 项断言真实失败、退出码 1；恢复复测绿（disconnected-cll-recheck.*）。
- 全量：`final.xml` 193 passed / 0 failed / 0 skipped（含上述全部黄金算例）。

## 3. 实测澄清（登记为知识，不改契约）

规划期剪枝与路径复核的样本语义在变异实验中实测澄清：原始 RRT 2 节点直连候选仅端点样本受复核（中间连续性由 QEdgeConstraint 离散检查按 resolution 保证）——"碰撞剪枝断开"变异（返回恒 false）不破坏该黄金算例（端点无碰的 2 节点候选可过端点复核），故扰动红例采用"无碰撞也剪枝"方向（真实失败在案）。该事实属于测试面观测，不改变 §10.3 契约（QEdgeConstraint 分辨率离散检查是中间连续性的权威面）。

## 4. 边界与不适用声明

- 不自建模型/采样器/规划器家族：WorkCell/Device 只经 runtime 只读视图借持（⑥端口），family 词表封闭（rrt-connect）。
- policy 终态/Applicable/finalized 三态不可削弱——`doInCollision`/`reviewPathSequence` 的不可采信保守映射保持，未读本地阈值。
- 取消/预算/种子行为不变；并发 search 的种子互扰归装配面线程约束（文件头注在案），本包未引入共享可变状态。
