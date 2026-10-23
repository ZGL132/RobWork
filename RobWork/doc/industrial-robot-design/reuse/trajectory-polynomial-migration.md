# REUSE-T09 迁移说明——轨迹高阶多项式求值复用上游 Polynomial

任务：REUSE-T09（trajectory 单元能力包）。冻结起点 `0032c8e78b0a3b6c12942f945fd9182624735813`（reuse-t01 顶端，含所有者 EAA 补丁撤销批 54f0beda6）。分支 `codex/reuse-trajectory-polynomial`。授权依据＝reuse/migration.md §12.11 所有者批准能力序列原文"……→Device/State FK/Jacobian→**Polynomial::evaluateDerivatives 高阶轨迹**→满足契约IK→……"，能力边界＝catalog MATH-POLYNOMIAL（上游 `RobWork/RobWork/src/rw/math/Polynomial.hpp`/`PolynomialND.hpp`，符号 `Polynomial`/`evaluateDerivatives`，目标 sdurw_math；边界原文"五/七次系数与时间尺度、任意端导数、jerk、Interpolator 薄封装边界；不降低原连续性"）。

## 1. 保留／删除／适配／职责归属

| 对象 | 处置 | 论证 |
| --- | --- | --- |
| Smooth.cpp `hermiteH000/H100/H001/H101` | **删除**（4 个手写基展开式） | 值基/导数基属通用多项式求值——升幂系数数据＋上游 `evaluate()`（Horner）直接承载，H1 直接 API 足够 |
| TimeParam.cpp `basisH0000/H0010/H0001/H0011`、`septicS0~S3` | **删除**（8 个手写基×4 阶展开链） | 同上；导数链改经上游 `derivative()` 构造——mechanical 求导消除抄写缺陷类（WP-16-T14 批 h0000 二阶导笔误即该类缺陷） |
| 五次/七次基闭式（端点脉冲条件、升幂系数） | **保留为数据与文档** | 系数闭式是构造上游 Polynomial 对象的必要输入与测试侧独立书写对照基准（NFR-COR-01 双向对照），非求值实现 |
| λ 收缩切线（buildTangents）、贪心剔除、必经点/TCP 门控 | **保留（业务专属）** | 几何保持判据与收缩语义为 §11.4 业务规则，上游无对应物 |
| 驻留常值显式分支、结点二/三阶导恒 0 纪律 | **保留（业务专属）** | §9.1/§12.1 构造纪律；常值分支规避值基二阶导和非零的实现陷阱 |
| 中心差分结点导数、初值时长闭式系数、缩放确认环 | **保留（业务专属）** | §12.1/§12.2 运动律业务；52.5/1.875/10√3 系数为业务剖面闭式 |
| C²/C³ 连续性守卫、限值校验、诊断素材 | **保留（业务专属）** | §12.5/§12.6 契约面 |
| Hermite 加权合成（逐轴 w·基值） | **保留（业务适配）** | 端点值/导数加权是 Hermite 段构造语义（业务适配层），仅基值/导数值的求取交上游 |
| `evaluateDerivatives` | **不入生产热路径** | 每调用返回 `std::vector`（n+1 元素堆分配）——峰值扫描/采样重放热路径纪律否决（基准实证：每场景 244,800 次分配、慢 ~5 倍）；转作测试侧上游直接对照（与本生产 evaluate/derivative 组合互为独立上游路径） |
| 每轴预装配段多项式（构建期一次性） | **不采用** | 任意实系数在 u=1 处 Horner 引入舍入，破坏"端点位级还原"契约（Smooth 端点 `EXPECT_EQ`）与共线零偏差判定；整数系数基表方案无此代价 |

## 2. 行为零变化论证（位级精确性）

- 基系数全为小整数 → u=0/1 处 Horner 中间量全为整数（|值|≤180，binary64 精确）→ 基值与各阶导数在两端位级等于解析脉冲形——结点位级还原、端点零速零加速度、连续性守卫"两侧极限数学恒 0"的位级形态保持。
- 共线 NoChange 判定：探针网格 t=p/64 为二进分数，度 7 整数系数 Horner 中间量分母 ≤2⁴² 全部精确 → 基和恒等式（h000+h001≡1、h100+h001+h101≡t）无舍入破坏 → 偏差位级 0 判定保持。
- 通用参数处新旧式仅差 ulp 级（基准校验和相对漂移 ~1e-15），全部既有断言容差（黄金 1e-12 相对、限值 1e-9）吸收；替换后既有 182+69 用例零改动通过。

## 3. 证据索引（traceability/reuse/REUSE-T09/）

- `normal/`：替换后集成双目标全绿（189+69），gtest XML＋ird-test-report＋运行日志。
- `disconnected/`：两真实变异——TimeParam `evalBasis` 丢弃上游二阶导结果（筛选用例 C3KnotContinuityWithDwell：连续性守卫真实判 LawFailed，断言失败；全量运行时该变异致 21 用例 43 断言失败）；Smooth 基选择扰动（筛选用例 SmoothAnalyticGoldenCase：状态断言失败；全量运行时解析对照/容差门控/下游重规划多处失败）。变异源文件与原 SHA 绑定；恢复后 SHA 一致复跑全绿。
- `benchmark/`：polynomial-replace-bench.cpp＋bench-result.txt（MSVC /O2，与生产同工具链）——20 段×100 采样×6 关节×20 轮：新形态零分配、较旧手写式 +17.7%（五次）/＋34.3%（七次）（绝对差 0.24/0.51 ms，同量级）、复跑位级一致；evaluateDerivatives 热路径形态 +490%~542%、244,800 分配/场景。
- `production-smooth.json`／`production-timeparam.json`：ird-reuse-production/1（normal/disconnected/oracle/upstreamComparison 四面齐全；oracle＝测试侧独立闭式；upstreamComparison＝evaluateDerivatives 直接对照用例）。
- 测试独立 oracle：SmoothTest `refH000` 族＋`refSampleThreePoint`；TimeParamTest `refH0001`/`refS2` 族——测试侧独立书写，与实现无共享代码。

## 4. 差异与边界

- 单位/量纲：基值无量纲，时间换算 h^-k 保留在业务合成处（与替换前同式）；Smooth 域为几何参数（无物理时间）零换算。
- 确定性：基族表为进程级不可变常量（C++11 魔法静态，并发首调用安全，无共享可变状态）；求值纯函数，同输入位级同输出（NFR-COR-02 基准用例通过）。
- 连续性不降低：C²（五次）/C³（七次）构造纪律与守卫全保留，守卫位级零形态保持。
- CMake 零改动：Polynomial 为 sdurw_math 头内模板，trajectory 既有链接面覆盖。

## 5. 待主会话整合项（共享文件只读约定）

- `catalog.json` MATH-POLYNOMIAL `review.status`：candidate→verified，复核证据绑定本文件（翻转后 validate-reuse 对 `trajectory.smoothQuinticHermiteReuse`/`trajectory.timeParamHermiteBasesReuse` 两 migrated 行的 SUITABILITY 检查方可通过）。catalog 对本任务只读，未整合前该门禁失败属预期，不伪报通过。
- 其余共享文件（permissions.json、根规则、门禁脚本、其他单元矩阵）本任务零需求。
