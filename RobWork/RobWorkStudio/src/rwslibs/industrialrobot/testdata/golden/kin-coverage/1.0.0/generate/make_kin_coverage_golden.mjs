/**
 * make_kin_coverage_golden.mjs — 黄金数据集 kin-coverage 生成脚本
 * （WP-15-T13）。
 *
 * 独立性声明：可达格心计数由**平面 2R 臂位置存在性闭式**判定（格心到基
 * 座距离 ≤ L1+L2 即存在闭式解——圆盘可达集的解析事实），与产品 IK 数值
 * 求解零共享；固定计数 100/60 是几何常量的解析推论（生成时断言）。
 *
 * 场景（§10.1 kin-coverage：固定计数 100/60＋数据不足保留分母＋零样本）：
 *   planA   Grid 10×10×1＝100 位置样本（格心 (0.5+ix, 0.5+iy, 0)，基座
 *           (5,5,0)，L1=2.0 L2=2.9 → 可达半径 4.4）——恰 60 个格心可达、
 *           40 个超解析界限（生成时解析断言）→ AT-03 固定计数样例
 *           （计划 100、可达 60、不可达 40 → 覆盖率 60%）。
 *   planZero 零样本计划（counts [0,10,1]——乘积 0 合法存储，评估判
 *           DataInsufficient＋KIN-COVERAGE-ZERO-SAMPLES，不输出比率）。
 *   planD   4 样本小计划（碰撞要求在场、检测器缺失 → 全样本
 *           DataInsufficient 保留分母——与 planA 组合成混合分母变体）。
 *
 * 用法：node generate/make_kin_coverage_golden.mjs。
 */

import { writeFileSync, readFileSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const versionDir = join(here, '..');

// ---------------------------------------------------------------------
// 模型与计划（黄金权威常量）。
// ---------------------------------------------------------------------

const model = {
  id: 'planar-3r-centered',
  note: '平面 3R 臂——基座原点（j1 origin (0,0,0)）、TCP 偏置为零（TCP＝第'
      + '二连杆末端，位置与 q3 无关）。位置样本目标姿态恒等（φ=0）由 q3 自由'
      + '吸收——位置存在性 ⇔ 格心距离 ∈ [|L1−L2|, L1+L2]；解析界限'
      + '（Σ逐关节 origin 模长＋TCP 偏置模长＝4.4 m）与可达圆一致——超界'
      + '格心走结局 5（Unreachable 唯一来源），界内格心存在闭式解。j1/j2 '
      + '限位超 π（±3.2）覆盖全向格心，j3 ±6.5 承载 φ 归零所需行程。',
  joints: [
    { axis: [0, 0, 1], origin: [0, 0, 0], lower: -3.2, upper: 3.2 },
    { axis: [0, 0, 1], origin: [2.4, 0, 0], lower: -3.2, upper: 3.2 },
    { axis: [0, 0, 1], origin: [2.0, 0, 0], lower: -6.5, upper: 6.5 },
  ],
  tcpOffset: { xyz: [0, 0, 0] },
};
const L1 = 2.4, L2 = 2.0;
const R = L1 + L2;        // 可达外半径＝解析界限 4.4 m
const RInner = L1 - L2;   // 可达内孔半径 0.4 m

// 格心：Grid 体心规则 coord=min+(k+0.5)·size/counts（D-KIN-6 黄金生成
// 规则的闭式复算——z 轴 counts=1 → 格心 z=0 平面；盒以基座为中心对称）。
const planA = {
  id: 'planA-100',
  box: { min: [-5, -5, -0.5], size: [10, 10, 1] },
  counts: [10, 10, 1],
  demands: { collisionFreeRequired: false },
  note: '主计划——100 位置样本（相对基座 ±4.5 m 对称网格）',
};
const planZero = {
  id: 'planZero-0',
  box: { min: [-5, -5, -0.5], size: [10, 10, 1] },
  counts: [0, 10, 1],
  demands: { collisionFreeRequired: false },
  note: '零样本计划——counts 乘积 0 合法存储，评估判 DataInsufficient（R8）',
};
const planD = {
  id: 'planD-4-collision-demand',
  box: { min: [12, 12, -0.5], size: [4, 4, 1] },
  counts: [2, 2, 1],
  demands: { collisionFreeRequired: true },
  note: '碰撞要求在场（检测器缺失变体用）——4 样本全 DataInsufficient 保留分母',
};

/** Grid 体心格心（枚举序 x 最慢 z 最快——D-KIN-6）。 */
function gridCenters(plan) {
  const [nx, ny, nz] = plan.counts;
  const centers = [];
  for (let ix = 0; ix < nx; ++ix) {
    for (let iy = 0; iy < ny; ++iy) {
      for (let iz = 0; iz < nz; ++iz) {
        centers.push([
          plan.box.min[0] + (ix + 0.5) * plan.box.size[0] / nx,
          plan.box.min[1] + (iy + 0.5) * plan.box.size[1] / ny,
          plan.box.min[2] + (iz + 0.5) * plan.box.size[2] / nz,
        ]);
      }
    }
  }
  return centers;
}

// ---------------------------------------------------------------------
// 解析可达计数（位置存在性闭式——生成时断言固定计数）。
// ---------------------------------------------------------------------

const centersA = gridCenters(planA);
const base = model.joints[0].origin;
let reachedA = 0;
// TCP＝格心（tcpOffset 零、基座原点）——距离闭式（内孔 0.4 m：最近格心
// √0.5≈0.707 m，全部在外侧，无内孔剔除）。
const wristDist = (c) => Math.hypot(c[0] - base[0], c[1] - base[1]);
const reachByIndex = centersA.map((c) => {
  const d = wristDist(c);
  const ok = d >= RInner && d <= R;
  if (ok) { ++reachedA; }
  return ok;
});
if (centersA.length !== 100 || reachedA !== 60) {
  throw new Error(`固定计数黄金破坏：planned=${centersA.length} reached=${reachedA}（应 100/60）`);
}
// 边界裕度自检：最近格心与可达环带边界的距离须显著大于评估容差（1e-6 m），
// 防数值求解在解析边界附近抖动。
const margins = centersA
  .map((c) => Math.min(Math.abs(R - wristDist(c)), Math.abs(wristDist(c) - RInner)))
  .filter((m) => m > 0);
const minMargin = Math.min(...margins);
if (minMargin < 1e-3) { throw new Error('边界裕度不足: ' + minMargin); }

const centersD = gridCenters(planD);

const inputs = {
  schema: 'kin-coverage-inputs/1',
  datasetId: 'kin-coverage',
  model,
  reachRadiusM: R,
  plans: [planA, planZero, planD],
  variants: {
    fixedCount: { plans: ['planA-100'], note: 'AT-03 固定计数样例：100/60/40 → 60%' },
    insufficientDenominator: {
      plans: ['planA-100', 'planD-4-collision-demand'],
      note: '数据不足保留分母变体：planD 碰撞要求在场＋无检测器 → 4 样本 DataInsufficient、分母 104、整体降级',
    },
    zeroSamples: { plans: ['planZero-0'], note: '零样本反例：DataInsufficient＋KIN-COVERAGE-ZERO-SAMPLES、不输出 0%/100%' },
  },
  note: '样本几何由 (plan, budget, seed) 确定性再生（评估器同源）——本文件为黄金参数权威',
};

const expected = {
  schema: 'kin-coverage-expected/1',
  provenance: {
    method: 'closed-form',
    note: '可达计数＝圆盘可达集解析判定（生成时断言 100/60；边界裕度 ≥1e-3 m）',
  },
  fixedCount: {
    planned: 100, reached: 60, unreachable: 40,
    dataInsufficient: 60, downgraded: true,
    positionAxis: { planned: 100, reached: 60, unreachable: 40, dataInsufficient: 0 },
    orientationAxis: { planned: 100, reached: 0, unreachable: 40, dataInsufficient: 60 },
    note: 'AT-03 固定计数主样例（位置轴）：计划 100、可达 60、不可达 40 → 60%'
        + '（比率推导归报告层——黄金只登记计数）。位姿轴为载体的结构性事实：'
        + '斐波那契单方向（N=1 → +X，D-KIN-6 锁定）对纯 z 轴关节臂的转动'
        + '自由度不可达（rotvec y 分量恒 ±π/2）——60 个界内位姿样本恒'
        + ' DataInsufficient（非数值脆弱面）、40 个超界位姿样本随解析界限'
        + ' Unreachable。三态并存恰承载 AT-03 混合样例要求；整体降级=true',
  },
  insufficientDenominator: {
    planned: 104, reached: 60, unreachable: 40, dataInsufficient: 4,
    downgraded: true,
    note: '数据不足保留分母变体（位置轴 tally：planD 4 样本碰撞要求在场＋'
        + '检测器缺失 → DataInsufficient、分母 104 保留不计分子）＋整体降级'
        + '（KIN-04 R3/R8；位姿轴结构性 60 DI 不在此列——见 fixedCount 注）',
  },
  zeroSamples: {
    planned: 0, dataInsufficient: 0,
    diagnostic: 'KIN-COVERAGE-ZERO-SAMPLES',
    engineeringStatus: 'DataInsufficient',
    noRatioOutput: true,
    note: '零样本：不输出 0%/100%——CoverageResult 零比率字段为结构保证',
  },
  perSampleReach: reachByIndex,
  centersD: centersD,
};

const files = [
  ['inputs/coverage-inputs.json', JSON.stringify(inputs, null, 2) + '\n'],
  ['expected/coverage-expected.json', JSON.stringify(expected, null, 2) + '\n'],
];
for (const [rel, text] of files) { writeFileSync(join(versionDir, rel), text); }

console.log('# integrity 段（回填 manifest.json——含本脚本自身）:');
for (const rel of [...files.map(([r]) => r), 'generate/make_kin_coverage_golden.mjs']) {
  const data = readFileSync(join(versionDir, rel));
  const sha = createHash('sha256').update(data).digest('hex');
  console.log(`  {"path": "${rel}", "sha256": "${sha}", "sizeBytes": ${data.length}},`);
}
console.log('# planA 100 格心可达 ' + reachedA + ' / 最小边界裕度 ' + minMargin);
