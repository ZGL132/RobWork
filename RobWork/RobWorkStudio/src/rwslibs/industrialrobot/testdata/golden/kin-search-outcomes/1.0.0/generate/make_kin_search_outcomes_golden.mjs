/**
 * make_kin_search_outcomes_golden.mjs — 黄金数据集 kin-search-outcomes
 * 生成脚本（WP-15-T13）。
 *
 * 独立性声明：五类结局样例＋换初值反例的**目标位姿与参考解**由闭式推导
 * （平面 2R/3R 正解 FK 与肘型双分支逆解，与产品数值求解零共享）；结局的
 * 期望值由 §5.4 五类结局判定表（铁律）对场景几何的手工推演登记——例如
 * "全部初值收敛但都碰撞 ⇒ AllCandidatesFiltered＋逐解过滤记录，不得输
 * 出不可行"。期望的权威＝判定表语义＋几何事实，不是求解器输出。
 *
 * 场景一览（§10.2 V 矩阵映射）：
 *   s1-found      V-05  IK 有效解（可达目标、首解残差 ≤ 容差）
 *   s2-noconv     V-06  多初值未收敛（可达环带内孔——零候选＋搜索记录）
 *   s3-allfilt    V-09  全部候选被过滤（替身碰撞恒真——不得输出不可行）
 *   s4-partial    V-08  一构型碰撞另一构型有效（构型级碰撞仅过滤该解）
 *   s5-bound      V-04  组/结局 5 目标超解析界限（唯一产证明素材路径）
 *   s7a/s7b       V-07  换初值反例（原初值未收敛→扩大初值后找到有效解）
 *
 * 用法：node generate/make_kin_search_outcomes_golden.mjs。
 */

import { writeFileSync, readFileSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const versionDir = join(here, '..');

// ---------------------------------------------------------------------
// 模型（黄金权威常量）。
// ---------------------------------------------------------------------

const twoLink = {
  id: 'planar-2r',
  joints: [
    { axis: [0, 0, 1], origin: [0, 0, 0], lower: -2.97, upper: 2.97 },
    { axis: [0, 0, 1], origin: [1.1, 0, 0], lower: -Math.PI, upper: Math.PI },
  ],
  tcpOffset: { xyz: [0.7, 0, 0] },
};
const threeLink = {
  id: 'planar-3r',
  joints: [
    { axis: [0, 0, 1], origin: [0, 0, 0], lower: -2.97, upper: 2.97 },
    { axis: [0, 0, 1], origin: [1.0, 0, 0], lower: -Math.PI, upper: Math.PI },
    { axis: [0, 0, 1], origin: [0.6, 0, 0], lower: -Math.PI, upper: Math.PI },
  ],
  tcpOffset: { xyz: [0.4, 0, 0] },
};

/** 平面链闭式正解 FK（姿态角＝Σq；位置＝Σ Li·e(Σ_{≤i} q)）。 */
function fk(model, q) {
  let acc = 0, x = 0, y = 0, qi = 0;
  const segs = [...model.joints.map((j) => j.origin[0]).slice(1), model.tcpOffset.xyz[0]];
  for (const L of segs) {
    acc += q[qi++];
    x += L * Math.cos(acc);
    y += L * Math.sin(acc);
  }
  return { x, y, phi: acc };
}

/** 3R 肘型双分支（同 kin-ik-dedup 闭式）。 */
function threeLinkBranches(x, y, phi) {
  const L1 = 1.0, L2 = 0.6, L3 = 0.4;
  const wx = x - L3 * Math.cos(phi), wy = y - L3 * Math.sin(phi);
  const c = (wx * wx + wy * wy - L1 * L1 - L2 * L2) / (2 * L1 * L2);
  const out = [];
  for (const sign of [1, -1]) {
    const q2 = sign * Math.acos(Math.min(1, Math.max(-1, c)));
    const q1 = Math.atan2(wy, wx) - Math.atan2(L2 * Math.sin(q2), L1 + L2 * Math.cos(q2));
    out.push([q1, q2, phi - q1 - q2]);
  }
  return out;
}

const rz = (phi) => {
  const c = Math.cos(phi), s = Math.sin(phi);
  return { xx: c, xy: -s, xz: 0, yx: s, yy: c, yz: 0, zx: 0, zy: 0, zz: 1 };
};

// ---------------------------------------------------------------------
// 场景与期望（期望＝判定表语义＋几何事实的手工推演）。
// ---------------------------------------------------------------------

const target2r = fk(twoLink, [0.3, -0.5]);        // s1/s7 目标（可达）
const target3r = fk(threeLink, [0.2, 0.4, 0.0]);  // s3/s4 目标（双分支可达）
const branches3r = threeLinkBranches(target3r.x, target3r.y, target3r.phi);
if (branches3r.length !== 2) { throw new Error('3R 双分支推导失败'); }
// s4 碰撞谓词前提：两分支 q2 恰反号（collide-when-q2-positive 恰命中一支）。
if (branches3r[0][1] * branches3r[1][1] >= 0) { throw new Error('双分支 q2 应反号'); }

const models = { 'planar-2r': twoLink, 'planar-3r': threeLink };

const scenarios = [
  {
    id: 's1-found', model: 'planar-2r', v: 'V-05',
    target: { x: target2r.x, y: target2r.y, phi: target2r.phi },
    initialValues: [[0.3, -0.5]], iterationLimit: 200,
    collision: 'none',
    note: '可达目标单初值——有效解收敛',
  },
  {
    id: 's2-noconv', model: 'planar-2r', v: 'V-06',
    target: { x: 0.2, y: 0.0, phi: 0.0 },
    initialValues: { strategy: 'JointGrid', count: 4, seed: 7 },
    iterationLimit: 200,
    collision: 'none',
    note: '目标位于可达环带内孔（|p|=0.2 < |L1−L2|=0.4）——解析界限内、数值不可收敛（§8.1 C5）',
  },
  {
    id: 's3-allfilt', model: 'planar-3r', v: 'V-09',
    target: { x: target3r.x, y: target3r.y, phi: target3r.phi },
    initialValues: branches3r,
    iterationLimit: 200,
    collision: 'collide-all',
    note: '全部候选碰撞被过滤（C8）——DataInsufficient 素材，不得输出不可行',
  },
  {
    id: 's4-partial', model: 'planar-3r', v: 'V-08',
    target: { x: target3r.x, y: target3r.y, phi: target3r.phi },
    initialValues: branches3r,
    iterationLimit: 200,
    collision: 'collide-when-q2-positive',
    note: '一构型碰撞（q2>0 分支）另一构型有效——任务可行素材不受影响（AT-19）',
  },
  {
    id: 's5-bound', model: 'planar-2r', v: 'V-04(结局5载体)',
    target: { x: 3.0, y: 0.0, phi: 0.0 },
    initialValues: [[0, 0]], iterationLimit: 200,
    collision: 'none',
    note: '目标 ‖p‖=3.0 > ΣL=1.8——解析界限确定性排除（唯一产证明素材路径）',
  },
  {
    id: 's7a-restart-narrow', model: 'planar-2r', v: 'V-07(前半)',
    target: { x: target2r.x, y: target2r.y, phi: target2r.phi },
    initialValues: [[3.0, -2.5]], iterationLimit: 1,
    collision: 'none',
    note: '远离解的单一初值＋小预算——搜索未找到有效解（DataInsufficient）',
  },
  {
    id: 's7b-restart-expanded', model: 'planar-2r', v: 'V-07(后半)',
    target: { x: target2r.x, y: target2r.y, phi: target2r.phi },
    initialValues: [[3.0, -2.5], [0.3001, -0.5]],
    iterationLimit: 1,
    collision: 'none',
    note: '同一冻结输入扩大初值集（加入解邻域初值）——翻转为有效解（搜索未果≠不可行）',
  },
];

const expected = {
  schema: 'kin-search-outcomes-expected/1',
  provenance: {
    method: 'closed-form + judgment-table',
    note: '目标/参考解闭式推导；结局期望＝§5.4 判定表（铁律）对场景几何的手工推演——非求解器输出',
  },
  scenarios: {
    's1-found': {
      outcome: 'SolutionsFound', minSolutions: 1, hasSearchRecord: false,
      hasProof: false,
      assertResidualWithinTolerance: true,
      note: '首解位置/姿态残差 ≤ 两容差（1e-6 m / 1e-6 rad）',
    },
    's2-noconv': {
      outcome: 'MultiInitNoConvergence', solutions: 0, hasSearchRecord: true,
      initialGuessesTried: 4, hasProof: false,
      note: '零候选＋搜索未果记录（预算/初值数/迭代统计）——铁律：不得输出不可行',
    },
    's3-allfilt': {
      outcome: 'AllCandidatesFiltered', solutions: 0, hasSearchRecord: true,
      filteredCount: 2, filterReasons: ['collision', 'collision'], hasProof: false,
      note: '逐解过滤记录（构型级碰撞）→DataInsufficient——不得输出不可行（C8）',
    },
    's4-partial': {
      outcome: 'PartialCollision', solutions: 1, filteredCount: 1,
      filterReasons: ['collision'], hasSearchRecord: false, hasProof: false,
      note: '有效解保留＋碰撞解入过滤记录——构型级碰撞仅过滤该解（1 的子形态）',
    },
    's5-bound': {
      outcome: 'AnalyticBoundExceeded', solutions: 0, hasSearchRecord: false,
      hasProof: true,
      note: '解析界限排除先于迭代——证明素材非空（仅素材不裁定）；solutions 空',
    },
    's7a-restart-narrow': {
      outcome: 'MultiInitNoConvergence', solutions: 0, hasSearchRecord: true,
      initialGuessesTried: 1, hasProof: false,
      note: '前次 DataInsufficient（搜索未果）——复评语义的对照基线',
    },
    's7b-restart-expanded': {
      outcome: 'SolutionsFound', minSolutions: 1, hasSearchRecord: false,
      hasProof: false, assertResidualWithinTolerance: true,
      note: '扩大初值后同冻结输入复评找到有效解——V-07 反例闭环（AT-03）',
    },
  },
};

const inputs = {
  schema: 'kin-search-outcomes-inputs/1',
  datasetId: 'kin-search-outcomes',
  models,
  collisionScripts: {
    none: '无碰撞会话（策略未启用——硬过滤③跳过并标记 collisionNotEvaluated）',
    'collide-all': '替身碰撞评估器脚本：对任意构型返回碰撞（对象对 kin-obs-a/kin-obs-b）',
    'collide-when-q2-positive': '替身碰撞评估器脚本：仅 q[1]>0 的构型判碰撞（对象对同上）',
  },
  scenarios,
};

const files = [
  ['inputs/search-outcomes-inputs.json', JSON.stringify(inputs, null, 2) + '\n'],
  ['expected/search-outcomes-expected.json', JSON.stringify(expected, null, 2) + '\n'],
];
for (const [rel, text] of files) { writeFileSync(join(versionDir, rel), text); }

console.log('# integrity 段（回填 manifest.json——含本脚本自身）:');
for (const rel of [...files.map(([r]) => r), 'generate/make_kin_search_outcomes_golden.mjs']) {
  const data = readFileSync(join(versionDir, rel));
  const sha = createHash('sha256').update(data).digest('hex');
  console.log(`  {"path": "${rel}", "sha256": "${sha}", "sizeBytes": ${data.length}},`);
}
console.log('# 3R 双分支 = ' + JSON.stringify(branches3r));
console.log('# 2R 目标 = ' + JSON.stringify(target2r));
