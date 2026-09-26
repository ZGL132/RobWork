// =====================================================================
// make_region_sampling_golden.mjs —— req-region-sampling 数据集生成脚本
// （contract-fixture；units/requirements.md §10.1 数据集 3/4、§5.2 采样
// 计划定义、D-REQ-2 规范化、V-02 零样本合法、§4.7 I-REQ-6）。
//
// 用法：
//   node make_region_sampling_golden.mjs   （脚本位于 <版本目录>/generate/ 下）
//
// 生成逻辑（按设计文档规则**独立重算**——与产品 C++ 实现互证）：
//   1. 读 inputs/region-sampling.json；
//   2. D-REQ-2 规范化逐计划独立重算：counts[i]=floor(size[i]/spacing[i])+1
//      （IEEE754 确定运算——与 C++ std::floor 同规则）；Grid/Random 原样；
//   3. 计划分母独立重算：positionSamples=∏counts（乘积=0 合法——零样本）、
//      orientationSamples=directionSamples×rollSamples；
//   4. 同义计划对判定：RS-spacing-a/RS-spacing-b 规范化计数逐轴相等——
//      "同义计划同身份"（计划字节/摘要相等的黄金判据，由测试以产品
//      builder/codec 对照）；
//   5. 负样例表转写（case→首违例码）；
//   6. 写 expected/region-sampling-expected.json，打印 integrity 申报表。
// =====================================================================

import { createHash } from "node:crypto";
import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const inputPath = join(here, "..", "inputs", "region-sampling.json");
const outputPath = join(here, "..", "expected", "region-sampling-expected.json");

const model = JSON.parse(readFileSync(inputPath, "utf8"));

const regionByName = new Map(model.regions.map((r) => [r.name, r]));

// ---------- D-REQ-2 规范化＋计划分母（独立重算） ----------

const plans = model.planSpecs.map((spec) => {
  const region = regionByName.get(spec.region);
  if (!region) { throw new Error(`计划引用的区域不存在: ${spec.region}`); }
  const ps = spec.positionSampling;
  let counts;
  let changed = false;
  if (ps.method === "GridBySpacing") {
    const size = region.box.size;
    counts = size.map((s, i) => Math.floor(s / ps.spacing[i]) + 1);
    changed = true;  // 编辑态→权威形态（规范化改写发生）
  } else if (ps.method === "Grid") {
    counts = ps.counts;  // counts 即权威
  } else {
    throw new Error(`本夹具不含 Random 计划: ${ps.method}`);
  }
  const positionSamples = counts.reduce((a, b) => a * b, 1);
  const orientationSamples =
    spec.orientationSampling.directionSamples * spec.orientationSampling.rollSamples;
  return {
    planId: spec.planId,
    region: spec.region,
    method: ps.method,
    changed,
    counts,
    positionSamples,
    orientationSamples,
  };
});

// ---------- 同义计划对判定（规范化计数逐轴相等⇒同计划身份） ----------

function planByRegion(name) { return plans.find((p) => p.region === name); }
const a = planByRegion("RS-spacing-a");
const b = planByRegion("RS-spacing-b");
if (!a || !b) { throw new Error("同义计划对缺失"); }
const synonymEqual = a.counts.every((c, i) => c === b.counts[i]);
if (!synonymEqual) {
  throw new Error("同义计划对规范化计数不等——数据集设计错误（应同 counts）: "
    + JSON.stringify(a.counts) + " vs " + JSON.stringify(b.counts));
}

// ---------- 零样本计划合法面（乘积=0——存储合法、判定归评估） ----------

const zeroPlan = planByRegion("RS-zero");
if (!zeroPlan || zeroPlan.positionSamples !== 0) {
  throw new Error("零样本计划缺失或分母非 0——数据集设计错误");
}

// ---------- 负样例表转写 ----------

const negativeExpectations = model.negativeCases.map((n) => ({
  case: n.case,
  expectedErrorCode: n.expectedErrorCode,
  note: n.note,
}));

// ---------- 期望文件 ----------

const expected = {
  schema: "req-region-sampling-expected/1",
  generatedBy: "generate/make_region_sampling_golden.mjs",
  note: "区域与采样黄金期望（contract-fixture——D-REQ-2 floor 规则/计划分母独立重算；同义计划对计数相等；零样本分母 0 合法）",
  plans,
  synonymPlanPair: {
    regions: ["RS-spacing-a", "RS-spacing-b"],
    countsEqual: synonymEqual,
    rule: "同义计划（不同 spacing 规范化到同 counts）→同一采样内容身份输入（D-REQ-2：planContentIdentity 以规范化计数计算——normalizeSampling 产出相等）；计划条目字节因 regionRef 指向不同区域而不同，采样身份面相同",
  },
  zeroSamplePlan: {
    region: zeroPlan.region,
    positionSamples: zeroPlan.positionSamples,
    legal: true,
    note: "零样本判定归评估（KIN-04 R8 DataInsufficient）——本单元存储/编码合法（V-02）",
  },
  negativeExpectations,
  coverageDefaults: {
    minPositionCoverage: 0.8,
    basis: "units/requirements.md §4.4 CoverageTargets 设计默认（黄金锁定）；RS-zero 显式 0.5 字面保真",
  },
};

mkdirSync(dirname(outputPath), { recursive: true });
writeFileSync(outputPath, JSON.stringify(expected, null, 2) + "\n", "utf8");

// ---------- manifest integrity 申报表 ----------

const files = [
  "inputs/region-sampling.json",
  "expected/region-sampling-expected.json",
  "generate/make_region_sampling_golden.mjs",
];
console.log("integrity 申报表（path / sizeBytes / sha256）：");
for (const rel of files) {
  const abs = join(here, "..", rel);
  const bytes = readFileSync(abs);
  const sha = createHash("sha256").update(bytes).digest("hex");
  console.log(`${rel} ${bytes.length} ${sha}`);
}
console.log("expected 写出: " + outputPath);
