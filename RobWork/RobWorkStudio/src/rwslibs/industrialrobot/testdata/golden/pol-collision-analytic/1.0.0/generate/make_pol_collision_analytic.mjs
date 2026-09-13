// =============================================================================
// make_pol_collision_analytic.mjs — pol-collision-analytic 期望生成器
// （POL-T11，≙WP-07-T11；碰撞解析算例——手算闭式推导的机器承载）。
//
// 设计依据：
//   - units/policy.md §11：真实碰撞数值正确性经内置后端对构造场景（已知
//     相交/分离几何）的解析算例验证（analytic-case 类黄金数据集）；§10.5
//     （POL-AT-4 倒挂场景对象对与手算一致——analytic-case 数据集对照）；
//   - units/testkit.md §4.2.1（analytic-case：闭式解/手工可推导结果——
//     参考结果来源要求＝解析推导，manifest 登记推导出处）、§4.2.2（字段表）、
//     §4.5（integrity 申报）、§4.3.2（producer.solverConfig 记录策略阈值
//     快照——工程策略默认类别的消费方式）。
//
// 独立性声明：期望值＝手算闭式公式的直接编码（同姿态凸盒面间隙＝轴向
// 中心距−边长；重合/深度重叠→二值碰撞），不引用任何产品碰撞代码；
// inputs/scene-cases.json 各 case 的 derivation 字段为逐例推导记录。
// 脚本对推导自检（间隙公式的符号/边界断言）后才写 expected。
//
// 用法：node generate/make_pol_collision_analytic.mjs
// =============================================================================

import { createHash } from "node:crypto";
import { readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { Buffer } from "node:buffer";

const scriptDir = dirname(fileURLToPath(import.meta.url));
const versionDir = join(scriptDir, "..");

const input = JSON.parse(readFileSync(join(versionDir, "inputs", "scene-cases.json"), "utf8"));
const boxSize = input.geometry.boxSizeM; // 0.2 m

// ---------------------------------------------------------------------------
// 手算闭式推导（同姿态凸盒——轴向对齐立方体的面间隙公式）。
//   gap = centerOffsetAlongAxis − boxSize（中心距沿分离轴；姿态恒轴对齐：
//   q∈{0,π} 的盒绕 Z 旋转不变式）。gap > 0 ＝分离（无碰撞）；gap ≤ 0 ＝
//   重合/重叠（二值碰撞——内置后端二值语义含零距离接触）。
// ---------------------------------------------------------------------------

function handDerived(c) {
  // 分离轴与中心距：直立 mount＝z 轴（环境盒 z 偏移）；倒挂 q=0＝x 轴
  // （工具心 (−0.5,0,0) vs 环境心 (0.5,0,0)，中心距 2·armLength）；
  // 倒挂 q=π＝重合（工具心世界 (0.5,0,0)＝环境心）。
  let axis, centerOffset;
  if (c.mount === "identity") {
    axis = "z";
    centerOffset = Math.abs(c.envCenterZM);
  } else if (c.qRad === 0) {
    axis = "x";
    centerOffset = 2 * input.geometry.armLengthM;
  } else {
    axis = "coincident";
    centerOffset = 0;
  }
  const collision = centerOffset < boxSize; // 中心距小于边长＝投影重叠（重合含于）
  const gap = collision ? 0.0 : centerOffset - boxSize; // 碰撞例不承载间隙参考（二值语义）
  const pairs = collision ? [["ToolBox", "EnvBox"]] : [];
  return { id: c.id, axis, centerOffsetM: centerOffset, collision, referenceGapM: gap, pairs };
}

const expectedCases = input.cases.map(handDerived);

// 推导自检（手算公式的边界与符号——推导本身的防呆）：
//  - 全部 case 的 q ∈ {0, π}（盒姿态轴对齐的前提——公式适用域）；
//  - zeroValue 例必须判定碰撞且间隙参考为 0；
//  - signCancellation 例（正负镜像对）间隙参考逐位一致且判定一致；
//  - 倒挂无二次旋转判别例必须为"无碰撞"（若实现二次旋转即翻转为碰撞）。
const byId = new Map(expectedCases.map((e) => [e.id, e]));
for (const c of input.cases) {
  if (c.qRad !== 0 && c.qRad !== 3.141592653589793) {
    throw new Error(`自检失败：${c.id} 的 q 超出公式适用域 {0, π}`);
  }
}
const zero = byId.get("upright-contact");
if (!zero.collision || zero.referenceGapM !== 0) {
  throw new Error("自检失败：zeroValue 例应为零间隙碰撞");
}
const pos = byId.get("near-zero-separation");
const neg = byId.get("sign-symmetry-negative");
if (pos.collision || neg.collision || pos.referenceGapM !== neg.referenceGapM) {
  throw new Error("自检失败：正负镜像例应同为分离且间隙参考一致（signCancellation）");
}
const invClear = byId.get("inverted-clear-no-second-rotation");
if (invClear.collision) {
  throw new Error("自检失败：倒挂 q=0 应为无碰撞（该例是二次旋转的判别面）");
}
const invContact = byId.get("inverted-contact");
if (!invContact.collision) {
  throw new Error("自检失败：倒挂 q=π 应为重合碰撞（POL-AT-4 对象对手算）");
}

const expected = {
  note:
    "由 generate/make_pol_collision_analytic.mjs 按手算闭式推导产出（同姿态凸盒面间隙公式；" +
    "推导记录见 inputs/scene-cases.json 各 case 的 derivation）——本文件不是产品代码产物。" +
    "消费测试（CollisionAnalyticDatasetTest）以内置后端对构造场景的评估结果与本文件对照" +
    "（对象对/判定/coverage 事实；间隙参考为解析记录——二值评估不查询距离，P-POL-11）。",
  geometryEcho: input.geometry,
  policySnapshot: input.policySnapshot,
  cases: expectedCases,
};

const outPath = join(versionDir, "expected", "analytic.json");
writeFileSync(outPath, JSON.stringify(expected, null, 2) + "\n", "utf8");

console.log("== integrity 申报表（path / sizeBytes / sha256） ==");
for (const rel of [
  "inputs/scene-cases.json",
  "expected/analytic.json",
  "generate/make_pol_collision_analytic.mjs",
]) {
  const data = readFileSync(join(versionDir, rel));
  console.log(
    `  { "path": "${rel}", "sha256": "${createHash("sha256").update(data).digest("hex")}", ` +
      `"sizeBytes": ${data.length} },`,
  );
}
console.log(`expected/analytic.json 已写出：${outPath}`);
console.log(`cases=${expectedCases.length}，碰撞例=${expectedCases.filter((e) => e.collision).length}`);
