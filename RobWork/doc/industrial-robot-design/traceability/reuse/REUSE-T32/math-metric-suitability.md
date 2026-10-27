# MATH-METRIC 适用性报告（REUSE-T32，catalog candidate→verified 前置）

capability：`MATH-METRIC`（reuse/catalog.json）。review.version＝`c65ffc4fc495da0b01b2060ff3049eb127091d72`（本包起点；框架只读，该树内上游文件与当前工作树逐字节一致——validate-reuse SUITABILITY 检查项复核）。本报告为机器门禁外的语义适用性原件；生产接线与扰动证据见同目录 production-*.json 与 README。

## 1. 上游算式审计（本包 2026-10-25 定向阅读）

- `RobWork/RobWork/src/rw/math/MetricFactory.hpp:127-152`：`EuclideanMetric::doDistance(a,b)`＝`MetricUtil::dist2(a,b)`；`:192-216`：`InfinityMetric::doDistance(a,b)`＝`MetricUtil::distInf(a,b)`；`:431-452`：`ManhattanMetric::doDistance(a,b)`＝`MetricUtil::dist1(a,b)`（Manhattan 类同文件内联，工厂 `makeManhattan:441`）。
- `RobWork/RobWork/src/rw/math/MetricUtil.hpp:61-70`：`dist1`＝轴升序 `result += std::fabs(a[i]-b[i])`；`:132-151`：`dist2`＝`sqrt(Σ(a[i]-b[i])²)` 轴升序；`:227-239`：`distInf`＝轴升序 `max|a[i]-b[i]|`。三算式与 trajectory 生产原手写内层循环（`std::abs`/`delta*delta`/逐轴 max）**逐位同序同式**。
- `RobWork/RobWork/src/rw/pathplanning/PathAnalyzer.hpp:214-231`：`pathLength(begin,end,metric)`＝逐相邻对 `result += metric.distance(*p,*q)` 折叠（空序列 0、单点 0）。

## 2. 生产接线（迁移后实际消费）

`trajectory/src/Planner.cpp`（REUSE-T32 后）：

- `maxAdjacentStep`＝跨对 max 归约（产品稳定选择序归约保留）＋每对 `makeInfinity<Q>()->distance`（延续性键）。
- `pathJointLength`＝`PathAnalyzer::pathLength(begin,end,*makeManhattan<Q>())`（路径长度键；两处内联 Σ|Δq| 循环同日删除并委托该函数——"同语义同一实现点"）。
- `sampleStraightPath`＝单对 `makeEuclidean<Q>()->distance`（直连筛查分段数）。
- 既有 RRT 度量 `MetricFactory::makeEuclidean<Q>()`（Planner.cpp 第四步装配）不变。

catalog boundary 的求和序警示在本次落地：`pathJointLength` 由"单累加器逐轴交错"改为上游"逐对先归约再累加"，微基准实测 50 节点路径相对差 1.9e-15（末位舍入量级，performance.log）；max 折叠与单对 L2 逐位不变。稳定选择序仍为同一确定性全序（195 项含 PlannerAvoidance 黄金算例全过）。

## 3. 探测与生产证据（本包原件）

- 独立 oracle（非上游自身）：`PlannerReuseTest` 的 referenceManhattan/referencePathL1/referenceEuclidean 独立重写算式，与产品输出 EXPECT_DOUBLE_EQ 逐位对照（DirectAdoptPathLengthMatchesIndependentOracle 用例——直连采纳单对 L1 与独立参考逐位对照）。
- 采样观测面：采样计数公式级观测（计数替身夹具）受零模型构造红线（IRD-GATE-AD）限制未建——其敏感性由断开红例 l2 证实（L2→0 即漏检中间碰撞，CollisionThenRework/BudgetExhausted 黄金断言真实失败）；1e6 分段上限护栏属病态输入防溢出面，触发即 ≥10⁶ 样本——**登记不测**（README 分列）。
- 真实扰动：三组断开（L1 折叠→Linf、采样 L2→恒 0、规划期剪枝恒剪枝）各自 ≥1 断言真实失败且退出码≠0，恢复后复测绿（disconnected-*.xml/log + disconnected-*-recheck.*）。
- 全量：`final.xml` 193 passed / 0 failed / 0 skipped；契约 69 passed（contract.xml）。
- 性能：`performance.log`（/O2，200k 次×场景×三轮交替）——pathL1(50 节点) 相对 +24~41%（绝对 +40ns/次）、maxStep/L2 在噪声区。生产面每次 search 仅 K 次键计算，被 RRT 碰撞查询（ms 级）主导；开销接受与否由所有者/独立验收裁决，本包不声称性能 PASS。

## 4. 边界与不适用声明

- `PathAnalyzer::pathLength` 只用于 L1 路径长度键；不用于 L∞ 步长键（跨对 max 折叠是产品归约，上游无对应折叠器）。
- 不引入权重度量/Mahalanobis/Rotation3D 度量；rad|m 逐轴混合计量原口径保留。
- 4 项探测（T01 期小整数例）不构成生产接线证明——本报告由 §2/§3 的真实接线与扰动补足。
