// =====================================================================
// make_partial_pose_golden.mjs —— req-partial-pose 数据集生成脚本
// （contract-fixture；units/requirements.md §10.1 数据集 2/4、§4.3
// PoseConstraint/constrainedDof、§5.3 五姿态规则、§4.7 I-REQ-5）。
//
// 用法：
//   node make_partial_pose_golden.mjs   （脚本位于 <版本目录>/generate/ 下）
//
// 生成逻辑（按设计文档规则**独立转写/重算**——与产品 C++ 断言互证）：
//   1. 读 inputs/partial-pose.json；
//   2. 正向点表投影：constrainedDof 掩码（六分量布尔）、position 四态
//      （provided 值字面 / not-provided——缺失≠0 的黄金表达）、五规则
//      载荷字面（Fixed rpy 不归一——D-REQ-3；PointAtTarget 参考方向
//      ＝targetPoint/‖targetPoint‖ 独立重算；ToolRollFree 区间字面）；
//   3. 负样例表：逐条 case→expectedErrorCode 转写（构造边界拒绝码——
//      域错误表 RequirementErrorCode 的 token 字面）；
//   4. 写 expected/partial-pose-expected.json，打印 integrity 申报表。
// =====================================================================

import { createHash } from "node:crypto";
import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const inputPath = join(here, "..", "inputs", "partial-pose.json");
const outputPath = join(here, "..", "expected", "partial-pose-expected.json");

const model = JSON.parse(readFileSync(inputPath, "utf8"));

// ---------- 正向点表投影（掩码/四态/字面保真——逐字段独立转写） ----------

function norm(v) {
  const l = Math.hypot(v[0], v[1], v[2]);
  if (l === 0) { throw new Error("PointAtTarget 零向量目标（负样例才允许）"); }
  return [v[0] / l, v[1] / l, v[2] / l];
}

const pointProjections = model.points.map((p) => {
  const o = p.pose.orientation;
  const projection = {
    id: p.id,
    name: p.name,
    constrainedDof: [
      p.pose.constrainedDof.x,
      p.pose.constrainedDof.y,
      p.pose.constrainedDof.z,
      p.pose.constrainedDof.roll,
      p.pose.constrainedDof.pitch,
      p.pose.constrainedDof.yaw,
    ],
    constrainedCount:
      Number(p.pose.constrainedDof.x) + Number(p.pose.constrainedDof.y)
      + Number(p.pose.constrainedDof.z) + Number(p.pose.constrainedDof.roll)
      + Number(p.pose.constrainedDof.pitch) + Number(p.pose.constrainedDof.yaw),
    positionState: p.pose.position.state,
    positionValue: p.pose.position.state === "provided" ? p.pose.position.value : null,
    orientationKind: o.kind,
  };
  if (o.kind === "Fixed") {
    projection.fixedRpy = o.fixedRpy;  // 参数字面（D-REQ-3：等效角不归一）
  }
  if (o.kind === "PointAtTarget") {
    projection.referenceDirection = norm(o.targetPoint);  // 单位向量独立重算
    projection.targetPoint = o.targetPoint;
  }
  if (o.kind === "ToolRollFree") {
    projection.rollRange = { min: o.rollRange.min, max: o.rollRange.max };
  }
  if (o.kind === "AlignFrame") {
    projection.targetFrameObjectId = o.targetFrame.objectId;
  }
  if (o.kind === "AlignGeometryNormal") {
    projection.targetSceneObject = o.targetSceneObject;
    projection.feature = o.feature;
    projection.invertNormal = o.invertNormal === true;
  }
  return projection;
});

// ---------- 负样例表（case→首个违例码——构造边界拒绝，不静默转默认） ----------

const negativeExpectations = model.negativeCases.map((n) => ({
  case: n.case,
  expectedErrorCode: n.expectedErrorCode,
  note: n.note,
}));

// ---------- 期望文件 ----------

const expected = {
  schema: "req-partial-pose-expected/1",
  generatedBy: "generate/make_partial_pose_golden.mjs",
  note: "部分位姿约束黄金期望（contract-fixture——掩码/四态/五规则载荷字面独立转写＋PointAtTarget 参考方向独立重算；负样例逐条首违例码）",
  pointProjections,
  negativeExpectations,
  toleranceDefaultsNote: "PP-* 各点容差为输入字面（本数据集容差非全默认：PP-orientation-only 用 0.002 m/0.03490658503988659 rad 显式值——字面保真断言载体）",
};

mkdirSync(dirname(outputPath), { recursive: true });
writeFileSync(outputPath, JSON.stringify(expected, null, 2) + "\n", "utf8");

// ---------- manifest integrity 申报表 ----------

const files = [
  "inputs/partial-pose.json",
  "expected/partial-pose-expected.json",
  "generate/make_partial_pose_golden.mjs",
];
console.log("integrity 申报表（path / sizeBytes / sha256）：");
for (const rel of files) {
  const abs = join(here, "..", rel);
  const bytes = readFileSync(abs);
  const sha = createHash("sha256").update(bytes).digest("hex");
  console.log(`${rel} ${bytes.length} ${sha}`);
}
console.log("expected 写出: " + outputPath);
