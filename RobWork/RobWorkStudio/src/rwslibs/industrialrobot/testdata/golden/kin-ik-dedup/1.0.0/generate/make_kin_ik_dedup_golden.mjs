/**
 * make_kin_ik_dedup_golden.mjs — 黄金数据集 kin-ik-dedup 生成脚本（WP-15-T13）。
 *
 * 独立性声明：期望值由平面 3R 臂**闭式肘型双分支推导**产出（腕心 2R 位置
 * 闭式解＋q3=φ−q1−q2），与产品实现（kinematics/src/Ik.cpp 阻尼最小二乘迭
 * 代）零共享——解的参考值是数学闭式，不依赖数值求解过程。
 *
 * 场景（AT-03 去重反例主载体——V-04）：同末端位姿（含姿态）、不同关节构
 * 型的两个解均保留（去重对象＝关节构型、逐轴阈值——附录 D 第 3 项/C1）；
 * 解集与稳定排序四键（§6.3：裕量降序→可操作度降序→referenceQ 距离升序→
 * 初值序升序）的参考序由本脚本独立计算（§6.3 四键定义的独立复算）。
 *
 * 用法：node generate/make_kin_ik_dedup_golden.mjs（产物＋integrity 输出
 * 同 make_kin_fk_golden.mjs 口径）。
 */

import { writeFileSync, readFileSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const versionDir = join(here, '..');

// ---------------------------------------------------------------------
// 模型（黄金权威常量——与单元卡 §10.1 先行批同值；修改走数据集新版本）。
// ---------------------------------------------------------------------

const model = {
  id: 'planar-3r',
  note: '平面三连杆——全位姿逆解有两个肘型分支（2R 全位姿唯一，不能作同位姿异构型载体）',
  joints: [
    { axis: [0, 0, 1], origin: [0, 0, 0], lower: -2.97, upper: 2.97 },
    { axis: [0, 0, 1], origin: [1.0, 0, 0], lower: -Math.PI, upper: Math.PI },
    { axis: [0, 0, 1], origin: [0.6, 0, 0], lower: -Math.PI, upper: Math.PI },
  ],
  tcpOffset: { xyz: [0.4, 0, 0] },
};
const L1 = 1.0, L2 = 0.6, L3 = 0.4;

// ---------------------------------------------------------------------
// 闭式推导（独立参考——肘型双分支）。
// ---------------------------------------------------------------------

/** 平面 3R 全位姿闭式双支：腕心＝TCP−L3·e(φ)，2R 位置闭式，q3=φ−q1−q2。 */
function threeLinkBranches(x, y, phi) {
  const wx = x - L3 * Math.cos(phi);
  const wy = y - L3 * Math.sin(phi);
  const r2 = wx * wx + wy * wy;
  const cosQ2 = (r2 - L1 * L1 - L2 * L2) / (2 * L1 * L2);
  if (cosQ2 < -1 || cosQ2 > 1) { return []; }
  const out = [];
  for (const sign of [1, -1]) {
    const q2 = sign * Math.acos(Math.min(1, Math.max(-1, cosQ2)));
    const q1 = Math.atan2(wy, wx) - Math.atan2(L2 * Math.sin(q2), L1 + L2 * Math.cos(q2));
    out.push([q1, q2, phi - q1 - q2]);
  }
  return out;
}

/** 正解 FK（闭式——期望目标位姿的推导源）。 */
function forward(x) {
  const [q1, q2, q3] = x;
  const px = L1 * Math.cos(q1) + L2 * Math.cos(q1 + q2) + L3 * Math.cos(q1 + q2 + q3);
  const py = L1 * Math.sin(q1) + L2 * Math.sin(q1 + q2) + L3 * Math.sin(q1 + q2 + q3);
  return { x: px, y: py, phi: q1 + q2 + q3 };
}

/** D-KIN-6 归一化裕量（§6.3 排序键 1 的参考复算——设计默认公式）。 */
function margin(q, lower, upper) {
  const half = (upper - lower) / 2;
  return Math.min(q - lower, upper - q) / half;
}

// ---------------------------------------------------------------------
// 场景：同位姿双构型（目标＝q*=(0.2,0.4,0.0) 的闭式正解位姿）。
// ---------------------------------------------------------------------

const seedQ = [0.2, 0.4, 0.0];
const targetPose = forward(seedQ);
const branches = threeLinkBranches(targetPose.x, targetPose.y, targetPose.phi);
if (branches.length !== 2) { throw new Error('闭式推导应得两肘型分支: ' + branches.length); }
// 两分支构型确异（q2 反号——去重对象＝构型的前提）。
if (Math.abs(branches[0][1] - branches[1][1]) <= 0.1) {
  throw new Error('两分支 q2 差应显著大于去重阈值');
}

// 初值集＝两分支闭式解本身（初值序 0/1——排序键 4 的初值序源）。
const initialValues = branches.map((b) => b.map((v) => v));

// 稳定排序四键的参考复算（§6.3 原文：1 裕量降序 2 可操作度降序
// 3 ‖q−referenceQ‖ 升序 4 初值序升序；浮点全序比较）。
const referenceQ = [0.0, 0.0, 0.0];
const margins = branches.map((b, i) =>
  b.map((v, k) => margin(v, model.joints[k].lower, model.joints[k].upper)));
const minMargins = margins.map((m) => Math.min(...m));
const order = branches
  .map((_, i) => ({
    i,
    key1: -minMargins[i],                       // 裕量降序（取负升序排）
    key2: 0,                                    // n=3<6 → w≡0（D-KIN-2，两解同值）
    key3: Math.hypot(...branches[i].map((v, k) => v - referenceQ[k])),
    key4: i,                                    // 初值序
  }))
  .sort((a, b) => a.key1 - b.key1 || a.key2 - b.key2
      || a.key3 - b.key3 || a.key4 - b.key4)
  .map((o) => o.i);

const inputs = {
  schema: 'kin-ik-dedup-inputs/1',
  datasetId: 'kin-ik-dedup',
  model,
  request: {
    targetPose: { x: targetPose.x, y: targetPose.y, phi: targetPose.phi },
    targetFromSeedQ: seedQ,
    initialValues,
    referenceQ,
    positionTolerance: 1e-6,
    orientationTolerance: 1e-6,
    dedupThresholdPerAxis: 1e-6,
    iterationLimit: 200,
    note: '目标位姿由闭式正解 FK(q*) 推导（非产品路径）——初值＝两闭式分支',
  },
};

const expected = {
  schema: 'kin-ik-dedup-expected/1',
  provenance: {
    method: 'closed-form',
    note: '双分支闭式解＋四键参考排序（§6.3 独立复算）——与产品数值求解零共享',
  },
  outcome: 'SolutionsFound',
  solutions: branches.map((b, i) => ({
    sourceInitIndex: i,
    q: b,
    minimumJointMargin: minMargins[i],
    manipulability: 0,
  })),
  expectedOrder: order,
  statistics: { rawCount: 2, convergedCount: 2, dedupedCount: 2, filteredCount: 0 },
  asserts: {
    distinctConfigurationsRetained: 2,
    note: '同末端位姿两构型均保留（AT-03 去重反例——关节空间逐轴去重的黄金面）',
  },
};

const files = [
  ['inputs/ik-dedup-inputs.json', JSON.stringify(inputs, null, 2) + '\n'],
  ['expected/ik-dedup-expected.json', JSON.stringify(expected, null, 2) + '\n'],
];
for (const [rel, text] of files) { writeFileSync(join(versionDir, rel), text); }

console.log('# integrity 段（回填 manifest.json——含本脚本自身）:');
for (const rel of [...files.map(([r]) => r), 'generate/make_kin_ik_dedup_golden.mjs']) {
  const data = readFileSync(join(versionDir, rel));
  const sha = createHash('sha256').update(data).digest('hex');
  console.log(`  {"path": "${rel}", "sha256": "${sha}", "sizeBytes": ${data.length}},`);
}
console.log('# 参考序（expectedOrder）= ' + JSON.stringify(order));
console.log('# 双分支 q = ' + JSON.stringify(branches));
