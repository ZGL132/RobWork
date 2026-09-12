// =====================================================================
// make_fk_equation.mjs —— rt-fk-equation 数据集生成脚本（analytic-case，
// 附录 D 第 4/5 项容差载体；units/runtime.md §12 RT-T12）。
//
// 用法（仓库内任意目录）：
//   node make_fk_equation.mjs <本数据集版本目录>
// 例如：
//   node make_fk_equation.mjs .
// 脚本定位约定：脚本位于 <版本目录>/generate/ 下，输入读
// ../inputs/model.json，输出写 ../expected/fk.json（相对本脚本目录）。
//
// 独立性声明（manifest.referenceSource）：闭式解在脚本内独立推导——
//   平面 3R、轴全为 +Z、origin 旋转恒等、零位偏置 0，故
//   O_1 = t_1
//   O_i = O_{i-1} + Rz(q_1+…+q_{i-1}) · t_i   （i = 2,3）
//   axis_i(world) = (0,0,1)                    （绕 Z 旋转不动 Z 轴）
// 不引用任何产品实现代码（RobWork / industrialrobot 零依赖）——
// 附录 D C4/NFR-COR-01 的"独立正确性依据"由本脚本与测试内手写 FK、
// RobWork Device FK 三方互证承载。
// =====================================================================

import { readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const inputPath = join(here, "..", "inputs", "model.json");
const outputPath = join(here, "..", "expected", "fk.json");

const model = JSON.parse(readFileSync(inputPath, "utf8"));

/** 平面 Rz（double 精度——JS number 即 IEEE754 双精度，与 C++ double 同规）。 */
function rz(angle) {
  const c = Math.cos(angle);
  const s = Math.sin(angle);
  return [
    [c, -s, 0.0],
    [s, c, 0.0],
    [0.0, 0.0, 1.0],
  ];
}

/** 3×3 矩阵 × 3 向量。 */
function mulVec(m, v) {
  return [
    m[0][0] * v[0] + m[0][1] * v[1] + m[0][2] * v[2],
    m[1][0] * v[0] + m[1][1] * v[1] + m[1][2] * v[2],
    m[2][0] * v[0] + m[2][1] * v[1] + m[2][2] * v[2],
  ];
}

/** 闭式 FK：第 i 关节原点（世界系＝设备基座系，ground 预设 R=I）与轴。 */
function closedFormFk(joints, q) {
  const out = [];
  let origin = [0.0, 0.0, 0.0];
  let acc = 0.0; // q_1 + … + q_{i-1}（rad）
  for (let i = 0; i < joints.length; ++i) {
    const t = joints[i].originTranslation;
    origin = [
      origin[0] + Math.cos(acc) * t[0] - Math.sin(acc) * t[1],
      origin[1] + Math.sin(acc) * t[0] + Math.cos(acc) * t[1],
      origin[2] + t[2],
    ];
    const axis = mulVec(rz(acc + q[i] + joints[i].zeroOffset), joints[i].axis);
    out.push({ origin, axis });
    acc += q[i];
  }
  return out;
}

const samples = model.configurations.map((cfg) => ({
  id: cfg.id,
  q: cfg.q,
  joints: closedFormFk(model.joints, cfg.q),
}));

const expected = {
  comment:
    "rt-fk-equation 期望面：闭式 FK（平面 3R，见 generate/make_fk_equation.mjs 推导）。" +
    "origin 单位 m（设备基座系）；axis 为世界系单位方向（无量纲）。" +
    "容差经容差档案 rt-runtime（testdata/tolerance/rt-runtime/v1.0.0.json，附录 D 第 4/5 项 1e-9）。",
  samples,
};

writeFileSync(outputPath, JSON.stringify(expected, null, 2) + "\n", "utf8");
console.log("written:", outputPath, "samples:", samples.length);
