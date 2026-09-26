// =====================================================================
// make_transport_task_golden.mjs —— req-transport-task 数据集生成脚本
// （contract-fixture；units/requirements.md §10.1 数据集 1/4、§6.2 必验
// 冻结 schema、§5.2/D-REQ-2 采样规范化、§5.1 顺序键拓扑、§4.3 设计默认）。
//
// 用法：
//   node make_transport_task_golden.mjs   （脚本位于 <版本目录>/generate/ 下）
//
// 生成逻辑（按设计文档规则**独立重算**——与产品 C++ 实现同源规则的两条
// 实现，互证语义确定性；任何一侧漂移都会让 GoldenDatasetsContractTest
// 失败，这正是夹具的防线价值）：
//   1. 读 inputs/transport-task.json（搬运任务语义描述——单一事实来源）；
//   2. 必验冻结解析（§6.2 逐条）：mandatory := (level=="Must")；
//      entries 按 caseId 规范文本字典序排序；必验集合 = enabled∧mandatory，
//      requiredCount 即其计数；
//   3. 需求档计数（§4.8）：mustCount/shouldCount 三集合（点/区域/工况）
//      全量汇总；minPositionCoverage = 各区域下限最小值；
//   4. 顺序拓扑（I-REQ-7/§5.1）：sequenceKey＝前驱条目名，Kahn 消去——
//      重复/悬空/成环三类违例清单（升序去重）；
//   5. 采样规范化（D-REQ-2）：counts[i] = floor(size[i]/spacing[i]) + 1
//      （GridBySpacing→Grid）；计划分母 positionSamples=∏counts、
//      orientationSamples=directionSamples×rollSamples；
//   6. 设计默认值转写（§4.3 黄金锁定：位置容差 1e-3 m、姿态容差 π/180
//      rad——π/180 由本脚本按 Math.PI/180 独立计算，非抄测点）；
//   7. 写 expected/transport-task-expected.json，并打印 manifest integrity
//      申报表（path/sizeBytes/sha256——node:crypto 与 core ContentDigester
//      同为 SHA-256 标准实现）。
//
// 期望值由脚本生成后人工复核入库（数据集变更须重新审核——testkit.md
// §4.7）；全量文件为 LF 文本（testdata/.gitattributes 钉死行尾）。
// =====================================================================

import { createHash } from "node:crypto";
import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const inputPath = join(here, "..", "inputs", "transport-task.json");
const outputPath = join(here, "..", "expected", "transport-task-expected.json");

const model = JSON.parse(readFileSync(inputPath, "utf8"));

// ---------- §6.2 必验冻结解析（独立实现——mandatory ≡ level=="Must"） ----------

/** 按规范文本字典序排序键（caseId "obj-<32hex>" 的字节序＝文本序）。 */
const byCanonical = (a, b) => (a.id < b.id ? -1 : a.id > b.id ? 1 : 0);

const sortedConditions = [...model.conditions].sort(byCanonical);
const requiredCaseEntries = sortedConditions.map((c) => ({
  caseId: c.id,
  label: c.name,
  enabled: c.enabled === true,
  // §6.2 冻结规则：mandatory 仅由 level 派生——无独立第三"必验"开关。
  mandatory: c.level === "Must",
}));
const requiredCases = requiredCaseEntries.filter((e) => e.enabled && e.mandatory);

// ---------- §4.8 需求档计数（三集合全量汇总） ----------

let mustCount = 0;
let shouldCount = 0;
for (const p of model.points) {
  if (p.level === "Must") { mustCount += 1; } else { shouldCount += 1; }
}
for (const r of model.regions) {
  if (r.level === "Must") { mustCount += 1; } else { shouldCount += 1; }
}
for (const c of model.conditions) {
  if (c.level === "Must") { mustCount += 1; } else { shouldCount += 1; }
}
const minPositionCoverage = model.regions.length
  ? Math.min(...model.regions.map((r) => r.coverageTargets.minPositionCoverage))
  : 0.0;

// ---------- I-REQ-7 顺序拓扑（Kahn 消去——重复/悬空/成环） ----------

const pointNames = new Set(model.points.map((p) => p.name));
const seqEdges = new Map(); // 前驱名 → 后继计数（重复声明检出）
let dangling = new Set();
for (const p of model.points) {
  if (p.sequenceKey == null) { continue; }
  if (!pointNames.has(p.sequenceKey)) {
    dangling.add(p.sequenceKey);
    continue;
  }
  seqEdges.set(p.sequenceKey, (seqEdges.get(p.sequenceKey) ?? 0) + 1);
}
const duplicateKeys = [...seqEdges.entries()]
  .filter(([, n]) => n > 1).map(([k]) => k).sort();
// Kahn：入度 0（无前驱或前驱已消去）者逐个移除。
const hasPred = new Map(model.points.map((p) => [p.name, 0]));
const succCount = new Map(seqEdges);
for (const p of model.points) {
  if (p.sequenceKey != null && pointNames.has(p.sequenceKey)) {
    hasPred.set(p.name, (hasPred.get(p.name) ?? 0) + 1);
  }
}
const chain = [];
const ready = model.points.filter((p) => (hasPred.get(p.name) ?? 0) === 0)
  .map((p) => p.name);
const remaining = new Map(model.points.map((p) => [p.name, hasPred.get(p.name) ?? 0]));
const predsOf = new Map(model.points.map((p) => [p.name, p.sequenceKey]));
while (ready.length > 0) {
  const n = ready.shift();
  chain.push(n);
  for (const [name, pred] of predsOf) {
    if (pred === n && remaining.has(name)) {
      remaining.set(name, 0);
      ready.push(name);
    }
  }
  remaining.delete(n);
}
const cycleNodes = [...remaining.keys()].sort();
const sequence = {
  acyclic: duplicateKeys.length === 0 && dangling.size === 0 && cycleNodes.length === 0,
  chain,
  duplicateKeys,
  danglingKeys: [...dangling].sort(),
  cycleNodes,
};

// ---------- D-REQ-2 采样规范化（floor 规则独立重算） ----------

const normalization = [];
const planDenominators = [];
const regionByName = new Map(model.regions.map((r) => [r.name, r]));
for (const spec of model.planSpecs) {
  const region = regionByName.get(spec.region);
  if (!region) { throw new Error(`计划引用的区域不存在: ${spec.region}`); }
  const size = region.box.size;
  const spacing = spec.positionSampling.spacing;
  if (spec.positionSampling.method !== "GridBySpacing") {
    throw new Error("本夹具计划规格恒为 GridBySpacing（规范化载体）");
  }
  // counts[i] = floor(size[i]/spacing[i]) + 1（IEEE754 确定运算）。
  const counts = size.map((s, i) => Math.floor(s / spacing[i]) + 1);
  normalization.push({
    region: spec.region,
    size,
    spacing,
    counts,
    rule: "counts[i] = floor(size[i]/spacing[i]) + 1",
  });
  const positionSamples = counts.reduce((a, b) => a * b, 1);
  const orientationSamples =
    spec.orientationSampling.directionSamples * spec.orientationSampling.rollSamples;
  planDenominators.push({
    planId: spec.planId,
    region: spec.region,
    positionSamples,
    orientationSamples,
  });
}

// ---------- 事件绑定表（§4.5 events——绑定点名投影） ----------

const pointNameById = new Map(model.points.map((p) => [p.id, p.name]));
const eventTable = [];
for (const c of model.conditions) {
  c.events.forEach((ev, i) => {
    eventTable.push({
      condition: c.name,
      seq: i + 1,
      type: ev.type,
      station: pointNameById.get(ev.stationRef) ?? null,
      stationRef: ev.stationRef,
      durationS: ev.durationS,
    });
  });
}

// ---------- 期望文件（转写＋独立重算结果） ----------

const expected = {
  schema: "req-transport-task-expected/1",
  generatedBy: "generate/make_transport_task_golden.mjs",
  note: "搬运任务黄金期望（contract-fixture——§6.2/§4.8/I-REQ-7/D-REQ-2 规则独立重算＋§4.3 设计默认值字面转写；手工构造类）",
  requiredCasesFrozenSchema: {
    rule: "mandatory := (level==Must)；entries 序＝caseId 规范文本字典序；必验集合 = enabled ∧ mandatory",
    entries: requiredCaseEntries,
    requiredCount: requiredCases.length,
  },
  profile: {
    mustCount,
    shouldCount,
    minPositionCoverage,
    requiredCasesCount: requiredCases.length,
  },
  sequence,
  normalization,
  planDenominators,
  eventTable,
  toleranceDefaults: {
    positionTolerance: 0.001,
    // π/180 rad＝1°（设计默认——独立计算非抄写）。
    orientationTolerance: Math.PI / 180,
    basis: "units/requirements.md §4.3 ToleranceSpec 设计默认（黄金锁定）",
  },
  edgeCases: {
    zeroValue: "expected 引用 inputs：C-transport.inertia=not-provided（零值的四态承载——缺失≠0）",
    nearZero: "inputs 引用：C-transport.com.z=1e-18（近零样本——IEEE754 位模式保真，不容差吞并）",
    signCancellation: "inputs 引用：P-pick.position x+y=0 与 C-transport.com x+y=0（正负抵消——分量级保真）",
  },
};

mkdirSync(dirname(outputPath), { recursive: true });
writeFileSync(outputPath, JSON.stringify(expected, null, 2) + "\n", "utf8");

// ---------- manifest integrity 申报表（inputs＋expected＋本脚本） ----------

const files = [
  "inputs/transport-task.json",
  "expected/transport-task-expected.json",
  "generate/make_transport_task_golden.mjs",
];
console.log("integrity 申报表（path / sizeBytes / sha256）：");
for (const rel of files) {
  const abs = join(here, "..", rel);
  const bytes = readFileSync(abs);
  const sha = createHash("sha256").update(bytes).digest("hex");
  console.log(`${rel} ${bytes.length} ${sha}`);
}
console.log("expected 写出: " + outputPath);
