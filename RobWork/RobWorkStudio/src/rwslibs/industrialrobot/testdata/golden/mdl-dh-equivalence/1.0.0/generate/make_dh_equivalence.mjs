/**
 * mdl-dh-equivalence 黄金数据集生成脚本（testkit.md §4.4——脚本与产物同置、
 * 可独立重跑；analytic-case——参考结果为闭式推导，独立于生产实现）。
 *
 * 独立性声明（testkit §4.4/independentOfProductionImpl）：本脚本零依赖
 * 产品代码（仅 Node 内置模块），逐级累乘与三角函数均为本文件内独立实现
 * （与 units/modeling.md §7.4 公式同源、与 C++ 实现无共享代码）；消费方
 * 以本脚本产物（expected/*.json）反查 DhExplicitConverter——实现漂移在
 * 附录 D 第 5 项容差判定处显性失败。
 *
 * 数学口径（§7.4 原文，零位对齐——q_rw=0）：
 *   T_{i-1,i} = Rot_z(θ_i + q0_i) · Trans_z(d_i) · Trans_x(a_i) · Rot_x(α_i)
 *   origin_i  = T_{i-1,i}（关节系相对父连杆系；position = Rz(θ+q0)·(a,0,d)，
 *               rotation = Rz(θ+q0)·Rx(α)）
 *   axis_i    = R_{0,i}·(0,0,1)（累计旋转第三列）
 *   R_z(t) = [c,-s,0; s,c,0; 0,0,1]，R_x(a) = [1,0,0; 0,c,-s; 0,s,c]
 *
 * roundtrip 期望（expected/roundtrip.json）：
 *   - family=exact：重解参数＝输入参数（链规范形态下闭式逆唯一；实测为
 *     显式链的精确参数化——附录 D 第 5 项容差内逐关节逐项一致）；
 *   - family=exact-non-unique：解族存在，期望解＝字典序规则定值（自由
 *     theta 坐标钉中性值 0——§7.5"字典序规则定值"的合同规则，非几何
 *     闭式；d/a/alpha 为几何回收值＝输入值）。
 *
 * 重跑：node make_dh_equivalence.mjs（无时间戳/无随机源——产物与入库
 * 版本逐字节一致，NFR-COR-02）。
 */
'use strict';
import fs from 'node:fs';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

const thisDir = path.dirname(fileURLToPath(import.meta.url));

// ---- 3x3 旋转与位姿代数（本文件独立实现——零产品代码依赖） ----
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
const matMul = (A, B) => A.map((row, i) => [0, 1, 2].map((j) =>
  row[0] * B[0][j] + row[1] * B[1][j] + row[2] * B[2][j]));
const matVec = (A, v) => [
  A[0][0] * v[0] + A[0][1] * v[1] + A[0][2] * v[2],
  A[1][0] * v[0] + A[1][1] * v[1] + A[1][2] * v[2],
  A[2][0] * v[0] + A[2][1] * v[1] + A[2][2] * v[2],
];
const matAdd = (u, v) => [u[0] + v[0], u[1] + v[1], u[2] + v[2]];

// ---- 读取输入样本 ----
const inputs = JSON.parse(
  fs.readFileSync(path.join(thisDir, '..', 'inputs', 'samples.json'), 'utf8'));

// ---- 逐样本闭式展开 ----
const samples = inputs.samples.map((sample) => {
  // 第一遍：逐关节相对原点 T_{i-1,i}（origin 半部——§7.4 单步公式）。
  const rows = sample.joints.map((j) => {
    const rz = rotZ(j.thetaOffset + j.zeroOffset);
    const rx = rotX(j.alpha);
    // origin_i = Rot_z(θ+q0)·Trans_z(d)·Trans_x(a)·Rot_x(α)：旋转半部
    // ＝Rz·Rx；平移半部＝Rz·(a,0,d)（Trans_z(d)·Trans_x(a) 的平移向量
    // 为 (a,0,d)，Rot_z 在左故携带旋转）。
    return {
      name: j.name,
      origin: {
        position: matVec(rz, [j.a, 0.0, j.d]),
        rotation: matMul(rz, rx).flat(),
      },
      axis: [0.0, 0.0, 0.0],  // 占位——累计轴在下方第二遍循环填入
    };
  });
  // 第二遍：累计旋转 R_{0,i}（i 从 1 起）→ axis_i = R_{0,i}·ez。
  let accR = [[1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]];
  sample.joints.forEach((j, idx) => {
    const rz = rotZ(j.thetaOffset + j.zeroOffset);
    const rx = rotX(j.alpha);
    accR = matMul(accR, matMul(rz, rx));
    rows[idx].axis = matVec(accR, [0.0, 0.0, 1.0]);
  });
  return { id: sample.id, family: sample.family, joints: rows };
});

const expansion = {
  schemaVersion: 'mdl-dh-equivalence-expansion/1',
  note: '闭式展开期望（§7.4 公式；origin.position/rotation 为 T_{i-1,i} 的平移与旋转半部；axis 为 R_{0,i}·ez 累计轴——单位向量；消费比较走档案 mdl-dh 条目 dh[*].origin.*/dh[*].axis.*，附录 D 第 5 项 1e-9）',
  samples,
};

// ---- roundtrip 期望 ----
const roundtrip = {
  schemaVersion: 'mdl-dh-equivalence-roundtrip/1',
  note: '显式→DH 重解期望（family=exact：输入参数即唯一解——链规范形态下闭式逆；family=exact-non-unique：字典序规则定值——自由 theta 钉 0，d/a/alpha 几何回收＝输入值；比较走档案条目 dh-solve[*].*，附录 D 第 5 项 1e-9）',
  samples: inputs.samples.map((sample) => ({
    id: sample.id,
    family: sample.family,
    freeCoordinates:
      sample.family === 'exact-non-unique'
        ? sample.joints
            .map((j, idx) => (j.a === 0.0 ? 4 * idx + 0 : -1))
            .filter((c) => c >= 0)
        : [],
    parameters: sample.joints.map((j, idx) => ({
      name: j.name,
      // exact：唯一解＝输入参数；exact-non-unique：自由坐标（a=0 的
      // theta）按字典序规则定中性值 0，其余字段几何回收＝输入值。
      thetaOffset: (sample.family === 'exact-non-unique' && j.a === 0.0) ? 0.0 : j.thetaOffset,
      d: j.d,
      a: j.a,
      alpha: j.alpha,
    })),
  })),
};

fs.writeFileSync(
  path.join(thisDir, '..', 'expected', 'expansion.json'),
  JSON.stringify(expansion, null, 2) + '\n', 'utf8');
fs.writeFileSync(
  path.join(thisDir, '..', 'expected', 'roundtrip.json'),
  JSON.stringify(roundtrip, null, 2) + '\n', 'utf8');
console.log('mdl-dh-equivalence: expected/expansion.json + expected/roundtrip.json 已生成');
