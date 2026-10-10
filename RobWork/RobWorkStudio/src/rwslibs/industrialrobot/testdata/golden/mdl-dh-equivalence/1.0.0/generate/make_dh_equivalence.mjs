/**
 * mdl-dh-equivalence 黄金数据集生成脚本（testkit.md §4.4——脚本与产物同置、
 * 可独立重跑；analytic-case——参考结果为闭式推导，独立于生产实现）。
 *
 * 独立性声明（testkit §4.4/independentOfProductionImpl）：本脚本零依赖
 * 产品代码（仅 Node 内置模块），拆分累乘、三角函数与数值雅可比贪心秩
 * 分析均为本文件内独立实现（与 units/modeling.md §7.4 公式同源、与 C++
 * 实现无共享代码）；消费方以本脚本产物（expected/*.json）反查
 * DhExplicitConverter——实现漂移在附录 D 第 4/5 项容差判定处显性失败。
 *
 * 数学口径（§7.4 原文，audit F-631 拆分语义 + F-590 零位烘焙纪律——
 * q_model=0；拆分推导：运行时 origin·R(axis,q) 组合的转轴过 origin 平移
 * 后的点，标准 DH 单步转轴 z_{i-1} 过父帧原点，故 origin 平移必须沿转轴
 * 本身——Tx(a)/Rx(α) 静段逐一前移入下一关节 origin 头部；链上静段槽位
 * n 个而级联静态因子 n+1 个，链尾静段落**末关节 origin 尾部**使零位累积
 * Π origin 严格等于标准级联 Π T_{i-1,i}，全链 FK 严格还原）：
 *
 *   origin_i  = [Trans_x(a_{i-1})·Rot_x(α_{i-1})]（i>0 头部静段）
 *               · Rot_z(θ_i)·Trans_z(d_i)          （纯 z 螺旋核心）
 *               · [i＝末关：Trans_x(a_i)·Rot_x(α_i)]（链尾静段·尾部）
 *   axis_i    = (0,0,1)（中间关节——头部静段 Rx(α_{i-1}) 与显式累积帧
 *               的回拉 Rx(−α_{i-1}) 相消，世界轴向＝z_{i-1}）
 *               ｜(0, sinα_i, cosα_i)（末关节——尾部静段 Rx(α_i) 由 axis
 *               吸收：origin.R·axis＝Rx(α_i)·ez）
 *
 * roundtrip 期望（expected/roundtrip.json，F-631 新增自由坐标面）：
 *   - 判定与自由坐标由**独立数值雅可比贪心列消元**在真值参数处导出
 *     （中心差分 h=1e-6、主元相对容差 1e-8——与附录 D 第 5 项判定区
 *     同尺度；贪心即 §7.5"字典序规则"的可执行形态）；拆分语义下
 *     （a_i,α_i）的前移使部分精确链出现一阶自由族（如一般链的末关节
 *     a/α 经上游 θ/α 补偿一阶不可分）——该族二阶残差超差（字典序钉值
 *     重解被语义门拒绝），故 parameters 保持输入值（参数级 roundtrip
 *     一致性 V-11 不变），仅判定/自由坐标面如实承载。
 *
 * 重跑：node make_dh_equivalence.mjs（无时间戳/无随机源——产物与入库
 * 版本逐字节一致，NFR-COR-02）。
 */
'use strict';
import fs from 'node:fs';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

const thisDir = path.dirname(fileURLToPath(import.meta.url));

// ---- 3x3/4x4 位姿代数（本文件独立实现——零产品代码依赖） ----
const rotZ = (t) => [
  [Math.cos(t), -Math.sin(t), 0.0],
  [Math.sin(t), Math.cos(t), 0.0],
  [0.0, 0.0, 1.0],
];
const rotX = (a) => [
  [1.0, 0.0, 0.0],
  [0.0, Math.cos(a), -Math.sin(a)],
  [0.0, Math.sin(a), Math.cos(a)],
];
// 4x4 齐次（雅可比贪心与拆分累乘的承载形态——本文件内独立实现）。
const homogeneous = (R, p) => [
  [R[0][0], R[0][1], R[0][2], p[0]],
  [R[1][0], R[1][1], R[1][2], p[1]],
  [R[2][0], R[2][1], R[2][2], p[2]],
  [0.0, 0.0, 0.0, 1.0],
];
const hMul = (A, B) => A.map((row, i) => [0, 1, 2, 3].map((j) =>
  row[0] * B[0][j] + row[1] * B[1][j] + row[2] * B[2][j] + row[3] * B[3][j]));
const identity4 = () => homogeneous([[1, 0, 0], [0, 1, 0], [0, 0, 1]], [0, 0, 0]);

// ---- 读取输入样本 ----
const inputs = JSON.parse(
  fs.readFileSync(path.join(thisDir, '..', 'inputs', 'samples.json'), 'utf8'));

// ---- F-631 拆分语义闭式展开 ----
// 连杆静段 S_k ＝ Trans_x(a_k)·Rot_x(α_k)（平移 (a,0,0)、旋转 Rx(α)）。
const staticSegment = (j) => homogeneous(rotX(j.alpha), [j.a, 0.0, 0.0]);
const coreSegment = (j) => homogeneous(rotZ(j.thetaOffset), [0.0, 0.0, j.d]);

const samples = inputs.samples.map((sample) => {
  const rows = [];
  let prevStatic = homogeneous([[1, 0, 0], [0, 1, 0], [0, 0, 1]], [0, 0, 0]);
  const n = sample.joints.length;
  sample.joints.forEach((j, idx) => {
    const isLast = (idx + 1 === n);
    // origin_i ＝ [S_{i-1} 头部]·Rz(θ)·Tz(d)·[末关：S_i 尾部]（F-631）。
    let origin = prevStatic;
    origin = hMul(origin, coreSegment(j));
    if (isLast) { origin = hMul(origin, staticSegment(j)); }
    // axis_i ＝ ez（中间）｜(0, sinα, cosα)（末关节——尾部静段 Rx(α) 由
    // axis 吸收，F-591 口径在末关节保留）。
    const axis = isLast
      ? [0.0, Math.sin(j.alpha), Math.cos(j.alpha)]
      : [0.0, 0.0, 1.0];
    rows.push({
      name: j.name,
      origin: {
        position: [origin[0][3], origin[1][3], origin[2][3]],
        rotation: [
          origin[0][0], origin[0][1], origin[0][2],
          origin[1][0], origin[1][1], origin[1][2],
          origin[2][0], origin[2][1], origin[2][2],
        ],
      },
      axis,
    });
    prevStatic = staticSegment(j);
  });
  return { id: sample.id, family: sample.family, joints: rows };
});

const expansion = {
  schemaVersion: 'mdl-dh-equivalence-expansion/1',
  note: '闭式展开期望（§7.4 公式，audit F-631 拆分语义＋F-590 零位烘焙纪律——zeroOffset 不入几何；origin_i＝[Tx(a_{i-1})Rx(α_{i-1})]·Rz(θ_i)·Tz(d_i)·[末关：Tx(a_i)Rx(α_i)]，零位累积 Π origin 严格等于标准级联 Π T_{i-1,i}；axis 为关节系内转轴方向（中间 ez｜末关 (0,sinα,cosα)）；单位向量；消费比较走档案 mdl-dh 条目 dh[*].origin.*/dh[*].axis.*，附录 D 第 5 项 1e-9）',
  samples,
};

// ---- roundtrip 期望：独立数值雅可比贪心列消元（F-631 新增自由坐标面）----
const rebuild4 = (xv) => {
  const n = xv.length / 4;
  let acc = identity4();
  const out = [];
  let prev = identity4();
  for (let k = 0; k < n; ++k) {
    let step = prev;
    step = hMul(step, coreSegment({
      thetaOffset: xv[4 * k], d: xv[4 * k + 1], a: xv[4 * k + 2], alpha: xv[4 * k + 3],
    }));
    if (k === n - 1) {
      step = hMul(step, staticSegment({
        a: xv[4 * k + 2], alpha: xv[4 * k + 3],
      }));
    }
    acc = hMul(acc, step);
    out.push(acc);
    prev = staticSegment({ a: xv[4 * k + 2], alpha: xv[4 * k + 3] });
  }
  return out;
};
const residualAt = (xv, Fr) => {
  const fr = rebuild4(xv);
  const r = [];
  fr.forEach((F, i) => {
    for (let k = 0; k < 3; ++k) { r.push(F[k][3] - Fr[i][k][3]); }
    const z = [F[0][2], F[1][2], F[2][2]];
    const zt = [Fr[i][0][2], Fr[i][1][2], Fr[i][2][2]];
    // 轴偏差的垂直分量＝sin(角偏差)·法向（§7.5 残差口径）。
    r.push(z[1] * zt[2] - z[2] * zt[1]);
    r.push(z[2] * zt[0] - z[0] * zt[2]);
    r.push(z[0] * zt[1] - z[1] * zt[0]);
  });
  return r;
};
// 贪心列消元（字典序自由列——§7.5"字典序规则"的独立可执行形态；
// 主元相对容差 1e-8、中心差分 1e-6——附录 D 第 5 项判定区同尺度）。
const greedyFreeColumns = (xv) => {
  const Fr = rebuild4(xv);
  const h = 1e-6;
  const n = xv.length;
  const cols = [];
  for (let c = 0; c < n; ++c) {
    const xp = xv.slice(); const xm = xv.slice();
    xp[c] += h; xm[c] -= h;
    const ra = residualAt(xp, Fr); const rb = residualAt(xm, Fr);
    cols.push(ra.map((v, i) => (v - rb[i]) / (2 * h)));
  }
  const rows = cols[0].length;
  const maxAbs = Math.max(...cols.flat().map(Math.abs));
  const pivotTol = maxAbs * 1e-8;
  const work = cols[0].map((_, r) => cols.map((c) => c[r]));
  let nextRow = 0;
  const free = [];
  for (let col = 0; col < n; ++col) {
    let piv = -1; let best = pivotTol;
    for (let row = nextRow; row < rows; ++row) {
      const v = Math.abs(work[row][col]);
      if (v > best) { best = v; piv = row; }
    }
    if (piv < 0) { free.push(col); continue; }
    if (piv !== nextRow) { const t = work[nextRow]; work[nextRow] = work[piv]; work[piv] = t; }
    const f = work[nextRow][col];
    for (let row = nextRow + 1; row < rows; ++row) {
      const fa = work[row][col];
      if (fa === 0.0) { continue; }
      for (let j = col; j < n; ++j) { work[row][j] -= (fa / f) * work[nextRow][j]; }
    }
    ++nextRow;
  }
  return free;
};

const roundtrip = {
  schemaVersion: 'mdl-dh-equivalence-roundtrip/1',
  note: '显式→DH 重解期望（audit F-631 拆分语义：判定/自由坐标由独立数值雅可比贪心列消元在真值参数处导出——(a_i,α_i) 前移入下一关节头部后，部分精确链出现一阶自由族（如末关节 a/α 经上游 θ/α 补偿），该等价补偿族为真实规范自由度；本期望只承载判定与自由坐标清单（合同稳定面）——选定解的参数值为一阶自由族内成员（依赖求解器路径，不作黄金期望），其几何一致性由消费测试的 FK 半区（真实编译链对照）与程序化用例（钉值拒绝时参数保持真值）承载；附录 D 第 5 项 1e-9）',
  samples: inputs.samples.map((sample) => {
    const xTrue = sample.joints.map((j) => [
      j.thetaOffset, j.d, j.a, j.alpha,
    ]).flat();
    const free = greedyFreeColumns(xTrue);
    return {
      id: sample.id,
      family: sample.family,
      determination: free.length === 0 ? 'Exact' : 'ExactNonUnique',
      freeCoordinates: free,
    };
  }),
};

fs.writeFileSync(
  path.join(thisDir, '..', 'expected', 'expansion.json'),
  JSON.stringify(expansion, null, 2) + '\n', 'utf8');
fs.writeFileSync(
  path.join(thisDir, '..', 'expected', 'roundtrip.json'),
  JSON.stringify(roundtrip, null, 2) + '\n', 'utf8');
console.log('mdl-dh-equivalence: expected/expansion.json + expected/roundtrip.json 已生成');
