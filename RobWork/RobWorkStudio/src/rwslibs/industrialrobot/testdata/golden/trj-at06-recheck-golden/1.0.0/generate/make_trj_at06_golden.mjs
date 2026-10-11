/**
 * make_trj_at06_golden.mjs —— AT-06 轨迹链四反例黄金数据集的独立参考实现（WP-16-T13）。
 *
 * 数据集：trj-at06-recheck-golden（kind=analytic-case——NFR-COR-01"解析算例为
 * 独立正确性依据首选"）。本脚本从 inputs/trj-at06-inputs.json 读取唯一数据源，
 * 按 units/trajectory.md §11.1/§11.5/§15.6 复检协议与 REQUIREMENTS 附录 C《P-06
 * 冻结数值表》**闭式直算**产出 expected/trj-at06-expected.json，与产品实现
 * （trajectory/src/Recheck.cpp 等，C++ 路径）零共享代码——两侧任一漂移即在
 * 契约测试的黄金对照用例显性失败。
 *
 * 参考实现公式来源（本脚本的唯一权威，禁止引用产品实现行为）：
 *   - §11.1/§15.6 细分协议：从单子段起逐层二分，终止条件＝关节步长与笛卡尔
 *     步长双上界同时满足（取严者生效——P-06 冻结表"读取顺序"注）；预算
 *     （最大层数/单段子段数）耗尽仍不满足 → 该段 DataInsufficient（附实际
 *     最大步长与预算占用，不执行碰撞查询——TRJ-04 R9）。
 *   - 预算占用账目（§11.5）：最终子段数＝2^层数；检查子段数＝完全二叉树
 *     节点累计 2^(层数+1)−1（含初始子段与全部细分产物——"实际检查"口径）。
 *   - 笛卡尔步长（P-06 行 5/9，1DOF 场景代表点集＝TCP 单点）：TCP 沿半径
 *     R 圆弧扫掠，子段弧长＝R·|Δq|/n（closed-form 弧长）。
 *   - 碰撞参数区间（案例 1）：障碍盒对原点的角半径 α＝atan(w/L)（w＝盒半
 *     宽、L＝盒中心径向距离），杆扫掠角 q∈[θc−α, θc+α] 与盒相交。
 *   - 碰撞参数区间（案例 3）：C-space 障碍投影带 [lowerQ, upperQ] 与线性
 *     关节插值 q1(s)＝q1s＋s·(q1e−q1s) 的交——纯代数。
 *   - FK（案例 3 绕行构型）：平面 2R 闭式 x＝L1·cos(q1)＋L2·cos(q1+q2)、
 *     y＝L1·sin(q1)＋L2·sin(q1+q2)（基座系，m）。
 *   - 峰值越限（案例 4）：q̃(s)＝k·s＋A·sin(πs) 的内部极值点 s*＝
 *     acos(−k/(Aπ))/π（k＜Aπ 时存在），峰值 q̃(s*) 闭式；越限判定＝严格
 *     大于上界（零裕度合法）。
 *
 * 用法：node make_trj_at06_golden.mjs（在本目录执行——相对路径定位 inputs）。
 */

import { readFileSync, writeFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const here = dirname(fileURLToPath(import.meta.url));
const inputsPath = join(here, "..", "inputs", "trj-at06-inputs.json");
const expectedPath = join(here, "..", "expected", "trj-at06-expected.json");

const inputs = JSON.parse(readFileSync(inputsPath, "utf8"));

// =====================================================================
// P-06 协议参数（inputs.protocol——附录 C 冻结表的物化声明，零自设）
// =====================================================================

const P = inputs.protocol;

// =====================================================================
// 独立参考实现（闭式直算——与产品 C++ 零共享）
// =====================================================================

/**
 * 细分计划闭式解：给定关节行程与笛卡尔弧长（单一路径段、均匀二分），
 * 返回满足双上界所需的最小子段数与层数；预算耗尽时返回上限处的账目。
 *
 * 终止条件（§11.1）：层 k 的子段数 n＝2^k，关节步长 jointTravel/n 与
 * 笛卡尔步长 cartTravel/n 须同时 ≤ 各自生效步长（P-06 默认值——本数据集
 * 场景未经 policy 放宽，消费冻结默认）。取严者生效＝两者所需 n 的最大值。
 */
function subdivisionPlan(jointTravel, cartTravel) {
  // 各界所需的最小子段数（向上取整；关节界 0 视为无约束——纯关节场景
  // 笛卡尔界单独驱动时 joint 需求记 1）。
  const needJoint =
    jointTravel > P.maxJointStepRevolute
      ? Math.ceil(jointTravel / P.maxJointStepRevolute)
      : 1;
  const needCart =
    cartTravel > P.maxCartesianStep
      ? Math.ceil(cartTravel / P.maxCartesianStep)
      : 1;
  let need = Math.max(needJoint, needCart);

  // 预算面：单段子段数上限 1024（2^10）与层数上限 10 同为二分幂——两预算
  // 在"最终子段数"语义下等价（2^10＝1024）；需要量超出即预算耗尽。
  const budgetExhausted = need > P.maxSubsegments;
  if (budgetExhausted) {
    need = P.maxSubsegments; // 预算耗尽：账目停在预算上限处（不满足步长）
  }

  // 层数＝log2(need)（need 恒为 2 的幂向上取整后的最小幂——二分计划只产
  // 2^k 子段；向上取整到最小覆盖幂）。
  let depth = 0;
  let n = 1;
  while (n < need) {
    n *= 2;
    depth += 1;
  }

  return {
    subdivisionDepthUsed: depth,
    finalSubsegments: n,
    // 检查子段数＝完全二叉树节点累计 2^(层数+1)−1（含初始与全部细分产物
    // ——§11.5"实际检查子段数"口径）。
    subsegmentsExamined: Math.pow(2, depth + 1) - 1,
    actualMaxJointStep: jointTravel / n,
    actualMaxCartesianStep: cartTravel / n,
    budgetExhausted,
  };
}

/** 平面 2R 正运动学闭式（基座系，m——参考实现公式来源第 5 条）。 */
function fk2R(L1, L2, q1, q2) {
  const x = L1 * Math.cos(q1) + L2 * Math.cos(q1 + q2);
  const y = L1 * Math.sin(q1) + L2 * Math.sin(q1 + q2);
  return { x, y };
}

const expectedCases = [];

for (const cs of inputs.cases) {
  const entry = { id: cs.id, counterExample: cs.counterExample };

  if (cs.id === "recheck-collision") {
    // ---- 反例①：段内碰撞检出（AT-06——端点无碰撞、段内碰撞必须被复检检出）。
    const { tcpRadius: R, centerAngle: thetaC, radialDistance: L, halfWidth: w } = {
      tcpRadius: cs.device.tcpRadius,
      centerAngle: cs.obstacle.centerAngle,
      radialDistance: cs.obstacle.radialDistance,
      halfWidth: cs.obstacle.halfWidth,
    };
    const travel = cs.path.endQ - cs.path.startQ; // 关节行程，rad

    // 障碍盒对原点的角半径（closed-form）：α＝atan(w/L)；杆扫掠角落入
    // [θc−α, θc+α] 即与盒相交（盒中心径向距离 L＜杆长，径向交叠成立）。
    const alpha = Math.atan(w / L);
    const collisionLowerQ = thetaC - alpha; // 碰撞关节角区间下界，rad
    const collisionUpperQ = thetaC + alpha; // 上界，rad

    // 路径参数区间（q(s)＝startQ＋s·travel 线性插值 → s＝(q−startQ)/travel）。
    const sLower = collisionLowerQ / travel;
    const sUpper = collisionUpperQ / travel;

    // 细分计划：关节界（travel rad）与笛卡尔界（TCP 单点弧长 R·travel m）。
    const plan = subdivisionPlan(travel, R * travel);

    // 最终采样集＝子段端点 s_i＝i/n（i＝0..n，含两端）；落入碰撞区间的
    // 采样点即检出记录（首检出的采样下标与路径参数——段内定位面）。
    const n = plan.finalSubsegments;
    const firstHit = Math.ceil(sLower * n);
    const lastHit = Math.floor(sUpper * n);
    entry.conclusion = "Collision";
    entry.materialCode = "TRJ-RECHECK-COLLISION";
    entry.recheck = plan;
    entry.collisionInterval = { lower: sLower, upper: sUpper };
    entry.firstHitSampleIndex = firstHit;
    entry.firstHitPathParameter = firstHit / n;
    entry.lastHitSampleIndex = lastHit;
    entry.hitSamplesCount = lastHit - firstHit + 1;
    // 零值样例：路径中点 s＝0.5 恰为采样点（n 偶数）且障碍中心角 θc 与
    // 路径中点重合——该采样点相对障碍区间中心的角偏差精确零（检出记录
    // 中的"零距离"锚点；越限/碰撞判定为严格包含，零偏差仍属区间内）。
    entry.centerSampleIndex = n / 2;
    entry.centerAngleDeviation = Math.abs(n / 2 / n - thetaC / travel);
  } else if (cs.id === "budget-exhausted") {
    // ---- 反例②：细分预算耗尽（AT-06/R9——达预算仍不满足步长 → 判
    // DataInsufficient 附实际步长与预算占用，不得默认接受）。
    const totalTravel = Math.abs(cs.path.legUp) + Math.abs(cs.path.legDown); // 行程，rad
    const netDisplacement = cs.path.legUp + cs.path.legDown; // 净位移恒等式

    // 关节界驱动细分（TCP 单点弧长＝R·totalTravel——双上界照常计量；本例
    // 关节界需求 n≥1200、笛卡尔界需求 n≥6000，两界需求同超预算上限 1024，
    // 取严者生效的账目与关节界单算一致——账目停在预算上限处，见
    // expected.budgetDominance 注）。
    const plan = subdivisionPlan(totalTravel, cs.device.tcpRadius * totalTravel);

    entry.conclusion = "DataInsufficient";
    entry.insufficientReason = "BudgetExhausted";
    entry.materialCode = "TRJ-RECHECK-BUDGET-EXHAUSTED";
    entry.recheck = plan;
    entry.totalTravel = totalTravel;
    entry.netDisplacement = netDisplacement; // ＋30＋(−30)＝0——正负抵消样例
    // 实际步长对默认步长的超出量（R9"仍不满足步长"的显式数值面）。
    entry.stepOverDefault = plan.actualMaxJointStep - P.maxJointStepRevolute;
    // 预算耗尽不执行碰撞查询（R9——不默认接受未充分验证段）；零查询即
    // 零值样例（账目起点）。
    entry.collisionQueriesExecuted = 0;
    entry.budgetDominance =
      "关节界需求 n≥1200、笛卡尔界需求 n≥6000（R·60 m 弧长/0.005 m），两界同超预算上限——双上界取严语义下账目停在 1024（关节界 60/1024 rad 为登记的实际步长）";
  } else if (cs.id === "replan-success") {
    // ---- 反例③：初始候选路径碰撞、重规划成功（AT-06/C8——候选路径碰撞
    // 不判任务不可行；碰撞作用域＝路径级）。
    const { link1: L1, link2: L2 } = cs.device;
    const q1s = cs.candidatePath.startQ1;
    const q1e = cs.candidatePath.endQ1;
    const q2s = cs.candidatePath.startQ2;
    const q2e = cs.candidatePath.endQ2;

    // 候选路径 q1(s)＝q1s＋s·Δq1 与 C-space 障碍投影带 [lowerQ, upperQ]
    // 的交——纯代数（线性插值的闭式区间映射）。
    const dq1 = q1e - q1s;
    const band = cs.obstacleBand;
    const sLower = (band.lowerQ - q1s) / dq1;
    const sUpper = (band.upperQ - q1s) / dq1;

    // 绕行构型存在性（替代路径可生成的解析证明）：detour 构型 q1 不落在
    // 障碍带内——角距即 C-space 裕量；其 TCP 闭式 FK 登记为替代路径的
    // 几何锚。真实 RRT-Connect 重规划与逐点确定性重放归集成模式 _test
    // 套件（V-10 已登记执行——§17.1 替身纪律），本数据集承载场景与解析面。
    const detour = cs.detourConfig;
    const tcp = fk2R(L1, L2, detour.q1, detour.q2);
    const clearance =
      detour.q1 > band.upperQ ? detour.q1 - band.upperQ : band.lowerQ - detour.q1;

    entry.candidateCollisionInterval = { lower: sLower, upper: sUpper };
    entry.taskFeasible = true; // 候选淘汰＋替代存在 → 任务可行（不判不可行——C8）
    entry.detour = {
      q1: detour.q1,
      q2: detour.q2,
      tcp,
      clearanceToBand: clearance, // C-space 角距，rad
      insideBand: detour.q1 >= band.lowerQ && detour.q1 <= band.upperQ,
    };
  } else if (cs.id === "limit-exceeded") {
    // ---- 反例④：限制超标定位（AT-06/TRJ-06——平滑诱发越限须被检出并
    // 定位到段内参数，比较型素材三要素齐备）。
    const k = cs.basePath.slope; // 基准折线斜率（q(s)＝k·s），rad
    const A = cs.smoothBulge.amplitude; // 平滑鼓包幅值，rad
    const limitUpper = cs.jointLimit.upper; // 关节限位上界，rad

    // 平滑曲线 q̃(s)＝k·s＋A·sin(πs) 的内部极值：d q̃/ds＝k＋Aπ·cos(πs)＝0
    // → cos(πs*)＝−k/(Aπ)（k＜Aπ 时内部极值存在——本例 0.5＜0.2π 成立）；
    // s*∈(0.5,1) 取 cos 单调降支的解 → sin(πs*)＞0 取正根。
    const cosPiS = -k / (A * Math.PI);
    const sStar = Math.acos(cosPiS) / Math.PI;
    const sinPiS = Math.sqrt(1 - cosPiS * cosPiS);
    const qPeak = k * sStar + A * sinPiS; // 峰值构型，rad
    const exceed = qPeak - limitUpper; // 越限量，rad（须＞0——平滑诱发越限）

    entry.violation = {
      axis: 0,
      axisKind: "revolute",
      upper: true,
      pathParameter: sStar, // 段内定位参数（TRJ-06 定位面）
      actualQ: qPeak, // 比较型要素一：实际构型，rad
      boundQ: limitUpper, // 比较型要素二：被越限的限界，rad
      unit: "rad", // 比较型要素三：单位（旋转关节——rad）
      materialCode: "TRJ-LIMIT-EXCEEDED",
    };
    entry.peak = { s: sStar, q: qPeak, exceed };
    // 零裕度锚点：基准路径端点 q(1)＝k 恰等于限位上界——裕度精确零；
    // 越限判定为严格大于（零裕度合法不触发——边界语义显式化）。
    entry.zeroMarginAnchor = {
      q: k * 1.0,
      bound: limitUpper,
      margin: limitUpper - k * 1.0,
      triggersViolation: false,
    };
    // 近零探针：把限位上界压至峰值下方 1e−6 rad——近零越限量仍必须检出
    // （近零违例不得被容差吞掉；附录 D 第 10 项容差是"校验判定"容差，
    // 不吞真实越限）。
    const probeLimit = qPeak - cs.nearZeroProbe.exceed;
    entry.nearZeroProbe = {
      limitUpper: probeLimit,
      exceed: qPeak - probeLimit, // 恒等于 inputs.nearZeroProbe.exceed
      mustDetect: true,
    };
  } else {
    throw new Error(`未知算例 id: ${cs.id}`);
  }

  expectedCases.push(entry);
}

const expected = {
  schema: "trj-at06-expected/1",
  note: "AT-06 四反例黄金期望值——由 generate/make_trj_at06_golden.mjs 独立参考实现产出（units/trajectory.md §11.1/§11.5/§15.6 复检协议与 REQUIREMENTS 附录 C P-06 冻结数值表闭式直算，与产品 C++ 实现零共享）。整数计数（层数/子段数/采样下标）经消费测试精确断言；浮点解析量经容差档案 trj-at06@1.0.0 对照（附录 D 第 9 项标量相对 1e-9）。",
  cases: expectedCases,
};

writeFileSync(expectedPath, JSON.stringify(expected, null, 2) + "\n", "utf8");
console.log(`trj-at06-recheck-golden: wrote ${expectedPath}`);
console.log(`  cases: ${expectedCases.map((c) => c.id).join(", ")}`);
