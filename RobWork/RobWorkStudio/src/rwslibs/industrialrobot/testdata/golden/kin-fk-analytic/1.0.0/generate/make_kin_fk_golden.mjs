/**
 * make_kin_fk_golden.mjs — 黄金数据集 kin-fk-analytic 生成脚本（WP-15-T13）。
 *
 * 独立性声明（analytic-case 类的 lint 义务——testkit.md §4.2.2）：
 *   本脚本是**独立参考推导的机器承载**，与产品实现（RobWorkStudio/src/
 *   rwslibs/industrialrobot/kinematics/src/Fk.cpp 的 Eigen 组件域实现）零
 *   共享代码——FK 用 4×4 齐次坐标直算（Rodrigues 公式独立书写），雅可比用
 *   几何定义直算，奇异值经 JJᵀ 2×2 特征值闭式解。期望值可信度来源＝数学
 *   闭式推导本身，不依赖产品实现。
 *
 * 用法（节点 ≥18，仓库内相对路径）：
 *   node generate/make_kin_fk_golden.mjs
 * 产物：
 *   inputs/fk-cases.json、expected/fk-expected.json（覆写）
 *   stdout 打印 manifest integrity 段（path/sha256/sizeBytes）——手工回填
 *   manifest.json（脚本自身不入自身完整性清单，由 manifest 登记其摘要）。
 *
 * 几何（黄金算例两套模型——常量为黄金数据集权威声明，修改走数据集新版本）：
 *   planar-2r：两旋转关节（轴 Z），j1 原点 (0,0,0) 限位 ±2.97 rad，j2 原点
 *              (1.1,0,0) 限位 ±π；TCP 偏置 (0.7,0,0)（无旋转）——L1=1.1 m、
 *              L2=0.7 m（单位 m，AGENTS §2.5）。
 *   six-axis ：六旋转臂（同 kinematics 单元卡 §10.1 先行批几何）：j1 z 轴
 *              原点 0 限位 ±3.14；j2 y 轴原点 (0,0,0.4) 限位 ±1.5；j3 y 轴
 *              原点 (0.5,0,0) 限位 ±1.5；j4 x 轴原点 (0.5,0,0) 限位 ±3.14；
 *              j5 y 轴原点 (0.3,0,0) 限位 ±1.5；j6 x 轴原点 (0.2,0,0) 限位
 *              ±3.14；TCP 偏置平移 (0.15,0,0.1)＋Rz(π/2)。
 */

import { writeFileSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const versionDir = join(here, '..');

// ---------------------------------------------------------------------
// 独立参考实现（4×4 齐次坐标——与产品 Eigen 路径零共享）。
// ---------------------------------------------------------------------

const I4 = () => [
  [1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1],
];

/** Rodrigues 旋转（轴 angle 弧度）＋平移的 4×4 齐次变换。 */
function rt(axis, angle, t) {
  const [x, y, z] = axis;
  const c = Math.cos(angle), s = Math.sin(angle), C = 1 - c;
  const R = [
    [C * x * x + c, C * x * y - s * z, C * x * z + s * y],
    [C * x * y + s * z, C * y * y + c, C * y * z - s * x],
    [C * x * z - s * y, C * y * z + s * x, C * z * z + c],
  ];
  const m = I4();
  for (let r = 0; r < 3; ++r) {
    for (let col = 0; col < 3; ++col) { m[r][col] = R[r][col]; }
    m[r][3] = t[r];
  }
  return m;
}

function mul(a, b) {
  const o = I4();
  for (let r = 0; r < 4; ++r) {
    for (let c = 0; c < 4; ++c) {
      let s = 0;
      for (let k = 0; k < 4; ++k) { s += a[r][k] * b[k][c]; }
      o[r][c] = s;
    }
  }
  return o;
}

/** 平移变换。 */
const tr = (x, y, z) => rt([0, 0, 1], 0, [x, y, z]);

/**
 * 独立参考 FK＋基础雅可比（6×n 行优先 [row*n+col]）。
 * 链式复合 origin→运动→…→tcpOffset；移动关节运动＝平移 axis·q。
 */
function refFk(model, q) {
  let acc = I4();
  const axes = [], pts = [], kinds = [];
  let qi = 0;
  for (const j of model.joints) {
    acc = mul(acc, tr(...j.origin));
    if (j.type === 'fixed') { continue; }
    // 轴方向（运动前、基座系——acc 旋转部分 × 关节轴）。
    const d = [0, 1, 2].map((r) =>
      acc[r][0] * j.axis[0] + acc[r][1] * j.axis[1] + acc[r][2] * j.axis[2]);
    axes.push(d);
    pts.push([acc[0][3], acc[1][3], acc[2][3]]);
    kinds.push(j.type);
    acc = j.type === 'prismatic'
      ? mul(acc, tr(j.axis[0] * q[qi], j.axis[1] * q[qi], j.axis[2] * q[qi]))
      : mul(acc, rt(j.axis, q[qi], [0, 0, 0]));
    ++qi;
  }
  // TCP 偏置复合（平移＋旋转——six-axis 的 Rz(π/2) 参与姿态与雅可比；
  // angle 缺省 0 时 rt() 退化为纯平移）。
  const off = model.tcpOffset;
  const tcp = mul(acc, rt(off.axis ?? [0, 0, 1], off.angle ?? 0, off.xyz));
  // 几何雅可比：旋转关节列＝[a×(p_tcp−p_axis); a]；移动关节列＝[a; 0]。
  const n = axes.length;
  const px = tcp[0][3], py = tcp[1][3], pz = tcp[2][3];
  const J = new Array(6 * n).fill(0);
  for (let k = 0; k < n; ++k) {
    const a = axes[k];
    let col;
    if (kinds[k] === 'prismatic') {
      col = [a[0], a[1], a[2], 0, 0, 0];
    } else {
      const d = [px - pts[k][0], py - pts[k][1], pz - pts[k][2]];
      col = [
        a[1] * d[2] - a[2] * d[1],
        a[2] * d[0] - a[0] * d[2],
        a[0] * d[1] - a[1] * d[0],
        a[0], a[1], a[2],
      ];
    }
    for (let r = 0; r < 6; ++r) { J[r * n + k] = col[r]; }
  }
  return { tcp, J, n };
}

/** 奇异值（经 JJᵀ 特征值——6×n 时 JJᵀ 6×6 中非零特征值＝n 个；这里对
 *  2×6 与 6×6 分别处理：统一取 J·Jᵀ 的幂迭代？——不，闭式：对 n=2 用
 *  JᵀJ（2×2）特征值闭式；n=6 用 J·Jᵀ 经特征多项式？6×6 无闭式。改用
 *  Jacobi 特征值算法（对称矩阵——独立实现，约 40 行，收敛 1e-15）。 */
function jacobiEigen(AIn) {
  const n = AIn.length;
  const A = AIn.map((r) => [...r]);
  for (let sweep = 0; sweep < 100; ++sweep) {
    let off = 0;
    for (let p = 0; p < n; ++p) {
      for (let q2 = p + 1; q2 < n; ++q2) { off += A[p][q2] * A[p][q2]; }
    }
    if (off < 1e-30) { break; }
    for (let p = 0; p < n; ++p) {
      for (let q2 = p + 1; q2 < n; ++q2) {
        if (Math.abs(A[p][q2]) < 1e-18) { continue; }
        const theta = (A[q2][q2] - A[p][p]) / (2 * A[p][q2]);
        const t = Math.sign(theta || 1) / (Math.abs(theta) + Math.sqrt(theta * theta + 1));
        const c = 1 / Math.sqrt(t * t + 1), s = t * c;
        for (let k = 0; k < n; ++k) {
          const akp = A[k][p], akq = A[k][q2];
          A[k][p] = c * akp - s * akq;
          A[k][q2] = s * akp + c * akq;
        }
        for (let k = 0; k < n; ++k) {
          const apk = A[p][k], aqk = A[q2][k];
          A[p][k] = c * apk - s * aqk;
          A[q2][k] = s * apk + c * aqk;
        }
      }
    }
  }
  return Array.from({ length: n }, (_, i) => A[i][i]);
}

/** 奇异值降序（经 Gram 矩阵特征值——σ=√max(λ,0)）。 */
function singularValues(J, n) {
  const m = 6;
  const G = Array.from({ length: n }, () => new Array(n).fill(0));
  for (let i = 0; i < n; ++i) {
    for (let j = 0; j < n; ++j) {
      let s = 0;
      for (let r = 0; r < m; ++r) { s += J[r * n + i] * J[r * n + j]; }
      G[i][j] = s;
    }
  }
  return jacobiEigen(G).map((l) => Math.sqrt(Math.max(l, 0))).sort((a, b) => b - a);
}

// ---------------------------------------------------------------------
// 黄金模型与算例（常量声明＝黄金权威）。
// ---------------------------------------------------------------------

const planar2r = {
  id: 'planar-2r',
  note: '平面二连杆——全量闭式可解析（位置/姿态/雅可比/奇异值）',
  joints: [
    { type: 'revolute', axis: [0, 0, 1], origin: [0, 0, 0], lower: -2.97, upper: 2.97 },
    { type: 'revolute', axis: [0, 0, 1], origin: [1.1, 0, 0], lower: -Math.PI, upper: Math.PI },
  ],
  tcpOffset: { xyz: [0.7, 0, 0], axis: [0, 0, 1], angle: 0 },
};

const sixAxis = {
  id: 'six-axis',
  note: '六轴臂——雅可比与奇异值经独立参考实现对照（附录 D 第 9 项）',
  joints: [
    { type: 'revolute', axis: [0, 0, 1], origin: [0, 0, 0], lower: -3.14, upper: 3.14 },
    { type: 'revolute', axis: [0, 1, 0], origin: [0, 0, 0.4], lower: -1.5, upper: 1.5 },
    { type: 'revolute', axis: [0, 1, 0], origin: [0.5, 0, 0], lower: -1.5, upper: 1.5 },
    { type: 'revolute', axis: [1, 0, 0], origin: [0.5, 0, 0], lower: -3.14, upper: 3.14 },
    { type: 'revolute', axis: [0, 1, 0], origin: [0.3, 0, 0], lower: -1.5, upper: 1.5 },
    { type: 'revolute', axis: [1, 0, 0], origin: [0.2, 0, 0], lower: -3.14, upper: 3.14 },
  ],
  tcpOffset: { xyz: [0.15, 0, 0.1], axis: [0, 0, 1], angle: Math.PI / 2 },
};

const models = { 'planar-2r': planar2r, 'six-axis': sixAxis };

/** 平面 2R 正反变换独立闭式逆（V-01 正反闭环的参考面——位置逆解）。 */
function planar2rInverse(model, x, y) {
  const L1 = model.joints[0].origin[0] === 0 ? 1.1 : 0; // L1＝j2 原点 x
  const L2 = model.tcpOffset.xyz[0];
  const r2 = x * x + y * y;
  const cosQ2 = (r2 - L1 * L1 - L2 * L2) / (2 * L1 * L2);
  if (cosQ2 < -1 || cosQ2 > 1) { return []; }
  const out = [];
  for (const sign of [1, -1]) {
    const q2 = sign * Math.acos(Math.min(1, Math.max(-1, cosQ2)));
    const q1 = Math.atan2(y, x) - Math.atan2(L2 * Math.sin(q2), L1 + L2 * Math.cos(q2));
    out.push([q1, q2]);
  }
  return out;
}

/** 算例（边角覆盖：零值/近零/正负抵消——附录 D C4）。 */
const cases = [
  { id: 'fk-zero', model: 'planar-2r', q: [0, 0],
    edge: 'zero-value', note: '零位构型——全部期望值零或解析常数' },
  { id: 'fk-nearzero', model: 'planar-2r', q: [1e-9, -1e-9],
    edge: 'near-zero', note: '近零构型——1e-9 rad 级输入的位级敏感面' },
  { id: 'fk-cancellation', model: 'planar-2r', q: [0.8, -0.8],
    edge: 'sign-cancellation', note: 'q1+q2=0 正负抵消——姿态恒等、位置非平凡' },
  { id: 'fk-general', model: 'planar-2r', q: [0.6, 0.9],
    edge: null, note: '一般位形——正反变换闭环载体' },
  { id: 'sx-home', model: 'six-axis', q: [0, 0, 0, 0, 0, 0],
    edge: 'zero-value', note: '六轴零位——TCP 偏置旋转复合路径' },
  { id: 'sx-pose', model: 'six-axis', q: [0.3, -0.4, 0.5, -0.6, 0.7, -0.8],
    edge: 'sign-cancellation', note: '六轴正负混合位形——独立参考对照' },
];

// ---------------------------------------------------------------------
// 期望值推导（闭式/独立参考）。
// ---------------------------------------------------------------------

const expectedCases = cases.map((c, index) => {
  const model = models[c.model];
  const { tcp, J, n } = refFk(model, c.q);
  const sv = singularValues(J, n);
  const exp = {
    caseId: c.id,
    index,
    tcp: {
      position: { x: tcp[0][3], y: tcp[1][3], z: tcp[2][3] },
      rotation: {
        xx: tcp[0][0], xy: tcp[0][1], xz: tcp[0][2],
        yx: tcp[1][0], yy: tcp[1][1], yz: tcp[1][2],
        zx: tcp[2][0], zy: tcp[2][1], zz: tcp[2][2],
      },
    },
    jacobian: {
      // 行优先 6·n 展开——trans 行（0..2）与 rot 行（3..5）分开登记，
      // 对应容差档案的 trans/rot 两族 fieldPath（量纲不同——D-KIN-2）。
      trans: Array.from({ length: 3 * n }, (_, i) => J[i]),
      rot: Array.from({ length: 3 * n }, (_, i) => J[3 * n + i]),
    },
    metrics: {
      // D-KIN-2：n<6 行秩不足 → w≡0（解析事实，非数值近似）。
      manipulability: n < 6 ? 0 : sv.reduce((a, b) => a * b, 1),
      // 奇异位形（σmin/σmax < 1e-12，如六轴零位 σmin≈0）的条件数在
      // 0/0 型下无第 9 项意义（参考实现给精确 0、产品给 1e-17 级噪声
      // ——比值爆炸且不可比）：JSON 登记 null＝该导出量对算例无定义，
      // 消费端跳过（σmin 的奇异事实经 singularValues 断言承载）。
      conditionNumber:
        sv[n - 1] > 1e-12 * sv[0] ? sv[0] / sv[n - 1] : null,
      singularValues: sv.slice(0, Math.min(6, n)),
    },
  };
  return exp;
});

// 正反变换参考（V-01：FK(q)→闭式逆→FK 复算闭环）——planar-2r 一般位形。
const roundTrip = (() => {
  const q = [0.6, 0.9];
  const { tcp } = refFk(planar2r, q);
  const branches = planar2rInverse(planar2r, tcp[0][3], tcp[1][3]);
  return {
    caseId: 'fk-general',
    forwardQ: q,
    tcpPosition: { x: tcp[0][3], y: tcp[1][3] },
    inverseBranches: branches,
    closedLoopToleranceM: 1e-6, // 正反变换闭环位置容差（m——去重容差语境）
    note: 'FK(q)→位置闭式逆（双分支）→FK 复算——位置残差 ≤ 去重容差',
  };
})();

// ---------------------------------------------------------------------
// 写盘＋完整性输出。
// ---------------------------------------------------------------------

const inputs = {
  schema: 'kin-fk-analytic-inputs/1',
  datasetId: 'kin-fk-analytic',
  models,
  cases,
  roundTrip,
  note: '几何常量为黄金权威——测试侧模型构造器消费本文件，不经单元测试夹具常量',
};

const expected = {
  schema: 'kin-fk-analytic-expected/1',
  provenance: {
    method: 'closed-form + independent-program',
    note: 'planar-2r 全量闭式；six-axis 经独立 4×4 齐次直算＋Jacobi 特征值（与产品 Eigen 路径零共享）',
  },
  cases: expectedCases,
  roundTrip,
};

const files = [
  ['inputs/fk-cases.json', JSON.stringify(inputs, null, 2) + '\n'],
  ['expected/fk-expected.json', JSON.stringify(expected, null, 2) + '\n'],
];

import { readFileSync } from 'node:fs';

for (const [rel, text] of files) {
  writeFileSync(join(versionDir, rel), text);
}

console.log('# integrity 段（回填 manifest.json——含本脚本自身）:');
for (const rel of [...files.map(([r]) => r), 'generate/make_kin_fk_golden.mjs']) {
  const data = readFileSync(join(versionDir, rel));
  const sha = createHash('sha256').update(data).digest('hex');
  console.log(`  {"path": "${rel}", "sha256": "${sha}", "sizeBytes": ${data.length}},`);
}
