/**
 * make_opt_pareto_golden.mjs —— optimization Pareto 非支配黄金数据集的独立
 * 参考实现（WP-20-T11，OPT-03/04/06/07——"不可行不入可行集＋Pareto 非支配
 * ＋显式容差支配"黄金面，AT-09）。
 *
 * 数据集：opt-pareto-golden（kind=analytic-case）。三个黄金面：
 *   A. 候选身份链（§4.2/§5.5 冻结公式）：CandidatePatchId＝SHA-256(补丁
 *      canonical 字节)；CandidateId＝SHA-256(基线根对象 16 字节‖基线根对象
 *      内容版本 32 字节‖补丁身份 32 字节)——node crypto 独立计算，与产品
 *      实现零共享代码；基线身份为黄金输入的固定 canonical 文本。
 *   B. 两级编排评估结局（§7.5 判定流＋§7.4 编排执行点的封闭语义）：逐候选
 *      状态由黄金事实封闭推导——关节限位 Must 违例→Infeasible（不进可行
 *      集）；激活目标槽位缺失→DataInsufficient（"—"不参与支配比较，不按
 *      0 合成）；证据齐备无违例→Feasible。编排预算设计为零筛选形态
 *      （Quick-Feasible 数 ≤ maxVerifiedCandidates——幸存集＝全部
 *      Quick-Feasible，避免黄金复算 T06 编排筛选序；筛选面由既有模型
 *      测试钉扎，本数据集不承载）。
 *   C. Pareto 非支配（§7.4 定义式）：支配判定/NSGA 逐层分层/稳定排序三键
 *      （rank 升序→声明序目标严格字典序〔按方向，无容差〕→CandidateId
 *      字典序终键）在两套目标集下独立复算——默认零容差集＋显式容差集
 *      （容差只进支配判定，不进排序键；近似相等互不支配；容差翻转记录
 *      ——AT-09"集合/排序满足容差支配"的黄金观测）。
 *
 * 三项静态指标（§7.1 行 1~3 暂定口径）封闭换算：包络＝基座系 AABB 三向
 * 尺寸之和（m）；结构质量＝Σ 连杆质量（kg）；最小关节裕量＝可算点最小值
 * （无量纲，D-KIN-2 归一化口径的承载值——本单元零裕量算法）。
 *
 * 用法：node make_opt_pareto_golden.mjs（在本目录执行）。
 */

import { writeFileSync } from "node:fs";
import { createHash } from "node:crypto";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const here = dirname(fileURLToPath(import.meta.url));
const inputsPath = join(here, "..", "inputs", "pareto-inputs.json");
const expectedPath = join(here, "..", "expected", "pareto-expected.json");

// =====================================================================
// 黄金唯一数据源（研究定义＋基线身份＋逐候选事实＋两套目标集）
// =====================================================================

const config = {
  schemaVersion: 1,
  stage: "stage-b",
  seed: 90210, // ≥1（I-OPT-2）
  strategyId: "opt.strategy.seeded-lhs",
  budget: {
    maxCandidates: 9, // 基线＋8 采样候选（＝事实表行数——逐位对齐）
    // 零筛选设计：Quick-Feasible（基线＋5 可行候选＝6）≤ 复核预算——
    // 幸存集＝全部 Quick-Feasible（黄金不复算编排筛选序——见文件头 B 面）。
    maxVerifiedCandidates: 6,
    maxGenerations: 1,
  },
};

const variables = [
  {
    bindingId: "mdl.joint[2].dh.a",
    kind: "continuous",
    unit: "m",
    lowerBound: 0.2,
    upperBound: 0.8,
    step: 0.0,
    enumValues: [],
    defaultValue: 0.5,
    defaultValueIndex: 0,
    locked: false,
    authorized: true,
    authorityFieldPath: "robot-design/joints[2]/dh/a",
    diagSubject: "obj-000000000000000000000000000000b2",
  },
  {
    bindingId: "mdl.joint[3].dh.a",
    kind: "continuous",
    unit: "m",
    lowerBound: 0.15,
    upperBound: 0.65,
    step: 0.0,
    enumValues: [],
    defaultValue: 0.4,
    defaultValueIndex: 0,
    locked: false,
    authorized: true,
    authorityFieldPath: "robot-design/joints[3]/dh/a",
    diagSubject: "obj-000000000000000000000000000000b3",
  },
];

// 基线身份（固定 canonical 文本——候选身份链的黄金输入）。
const baseline = {
  objectOid: "obj-000000000000000000000000000000b1",
  contentVersion:
    "cv-00000000000000000000000000000000000000000000000000000000000000be",
  label: "baseline",
};

// ---- 逐候选评估事实（投影接缝 P-OPT-2 裁决前的黄金事实源——测试以
//      patch 身份查表注入；kind 为 §7.5 判定流的封闭结局声明）：
//      feasible          证据齐备无违例 → Feasible；
//      limit-violation   关节限位 Must 违例（构型样本出界）→ Infeasible；
//      margin-missing    Must 工位裕量证据整体缺席 → MinJointMargin"—"
//                        → 激活目标缺失 → DataInsufficient（§7.4 编排执行点）。

function aabb(x0, x1, y0, y1, z0, z1) {
  return { xMin: x0, xMax: x1, yMin: y0, yMax: y1, zMin: z0, zMax: z1 };
}

const candidateFacts = [
  {
    key: "baseline",
    kind: "feasible",
    envelope: aabb(-0.5, 0.5, -0.5, 0.5, 0.0, 0.5), // 三向和＝3.0 m
    linkMassesKg: [5.5, 6.5], // Σ＝12.0 kg
    margins: [0.31, 0.44], // min＝0.31（无量纲）
    jointSampleRad: 0.0,
  },
  {
    key: "c1",
    kind: "feasible",
    envelope: aabb(-0.5, 0.5, -0.5, 0.5, 0.0, 0.4), // 2.4 m
    linkMassesKg: [5.0, 6.0], // 11.0 kg
    margins: [0.35, 0.47], // 0.35
    jointSampleRad: 0.0,
  },
  {
    key: "c2",
    kind: "feasible",
    envelope: aabb(-0.5, 0.5, -0.5, 0.5, 0.0, 0.4), // 2.4 m（与 c1 同包络）
    linkMassesKg: [4.5, 5.5], // 10.0 kg
    margins: [0.3, 0.52], // 0.30——与 c1 包络同、质量优、裕量差（零容差互不支配；
    //  显式容差 margin ε_abs 0.05 下 c2 支配 c1——黄金翻转记录主角）。
    jointSampleRad: 0.0,
  },
  {
    key: "c3",
    kind: "feasible",
    envelope: aabb(-0.5, 0.5, -0.3, 0.3, 0.0, 0.4), // 2.0 m
    linkMassesKg: [5.25, 5.25], // 10.5 kg
    margins: [0.28, 0.39], // 0.28
    jointSampleRad: 0.0,
  },
  {
    key: "c4",
    kind: "feasible",
    envelope: aabb(-0.4, 0.4, -0.4, 0.4, 0.0, 0.2), // 1.8 m（最小包络）
    linkMassesKg: [6.0, 6.0], // 12.0 kg（最重——与 c1/c2 互不支配）
    margins: [0.4, 0.55], // 0.40（最大裕量）
    jointSampleRad: 0.0,
  },
  {
    key: "c5",
    kind: "feasible",
    envelope: aabb(-0.7, 0.7, -0.5, 0.5, 0.0, 0.5), // 2.9 m
    linkMassesKg: [6.25, 6.25], // 12.5 kg
    margins: [0.29, 0.36], // 0.29（被 c1/c2 支配——rank 1 成员）
    jointSampleRad: 0.0,
  },
  {
    key: "v1",
    kind: "limit-violation",
    envelope: aabb(-0.6, 0.6, -0.6, 0.6, 0.0, 0.6),
    linkMassesKg: [5.0, 5.0],
    margins: [0.33, 0.41],
    jointSampleRad: 3.0, // 出界构型（限位 ±1 rad——Must 违例素材）
  },
  {
    key: "v2",
    kind: "limit-violation",
    envelope: aabb(-0.55, 0.55, -0.55, 0.55, 0.0, 0.55),
    linkMassesKg: [5.1, 5.1],
    margins: [0.32, 0.43],
    jointSampleRad: -2.5, // 负向出界（双符号覆盖）
  },
  {
    key: "m1",
    kind: "margin-missing",
    envelope: aabb(-0.45, 0.45, -0.45, 0.45, 0.0, 0.45),
    linkMassesKg: [4.8, 4.8],
    margins: [], // Must 工位裕量证据整体缺席——"—"不按 0 合成（NFR-COR-03）
    jointSampleRad: 0.0,
  },
];

// ---- 两套目标集（声明序＝config.opt.objectives 消费序；容差为**显式配置**
//      ——P-OPT-5 裁决前产品默认恒零容差，非零容差只存在于本数据集场景
//      声明并以显式配置执行〔任务卡 acceptance 3〕）。

const objectivesZero = [
  { metricId: "opt.metric.envelope", direction: "min", tolerance: { relative: 0.0, absolute: 0.0 } },
  { metricId: "opt.metric.structural-mass", direction: "min", tolerance: { relative: 0.0, absolute: 0.0 } },
  { metricId: "opt.metric.min-joint-margin", direction: "max", tolerance: { relative: 0.0, absolute: 0.0 } },
];

const objectivesExplicit = [
  { metricId: "opt.metric.envelope", direction: "min", tolerance: { relative: 0.0, absolute: 0.25 } },
  { metricId: "opt.metric.structural-mass", direction: "min", tolerance: { relative: 0.0, absolute: 0.0 } },
  { metricId: "opt.metric.min-joint-margin", direction: "max", tolerance: { relative: 0.0, absolute: 0.05 } },
];

const jointLimits = { qminRad: -1.0, qmaxRad: 1.0 }; // 黄金限位（探针判定素材）

// =====================================================================
// PRNG／身份链／指标换算（与 opt-lhs-golden 同款独立实现——每脚本自持）
// =====================================================================

const MASK64 = (1n << 64n) - 1n;

function splitmixNext(state) {
  const s = (state + 0x9e3779b97f4a7c15n) & MASK64;
  let z = s;
  z = ((z ^ (z >> 30n)) * 0xbf58476d1ce4e5b9n) & MASK64;
  z = ((z ^ (z >> 27n)) * 0x94d049bb133111ebn) & MASK64;
  z = (z ^ (z >> 31n)) & MASK64;
  return [z, s];
}

function unitFrom(u64) {
  return Number(u64 >> 11n) * (1.0 / 9007199254740992.0);
}

function sha256Hex(buf) {
  return createHash("sha256").update(buf).digest("hex");
}

function patchCanonicalBytes(items) {
  const sorted = [...items].sort((a, b) =>
    a.bindingId < b.bindingId ? -1 : a.bindingId > b.bindingId ? 1 : 0,
  );
  const parts = [];
  parts.push(Buffer.from("IRDOPTP1", "utf8"));
  const head = Buffer.alloc(8);
  head.writeUInt32LE(1, 0);
  head.writeUInt32LE(sorted.length, 4);
  parts.push(head);
  for (const it of sorted) {
    const idBuf = Buffer.from(it.bindingId, "utf8");
    const len = Buffer.alloc(2);
    len.writeUInt16LE(idBuf.length, 0);
    parts.push(len, idBuf, Buffer.from([0x53]));
    const v = Buffer.alloc(8);
    v.writeDoubleLE(Object.is(it.scalarValue, -0.0) ? 0.0 : it.scalarValue, 0);
    parts.push(v);
  }
  return Buffer.concat(parts);
}

// CandidateId ＝ SHA-256(oid 16B ‖ cv 32B ‖ patchId 32B)（§4.2 公式原文）。
function candidateIdHex(oidCanonical, cvCanonical, patchIdHex) {
  const oid = Buffer.from(oidCanonical.slice("obj-".length), "hex");
  const cv = Buffer.from(cvCanonical.slice("cv-".length), "hex");
  const pid = Buffer.from(patchIdHex, "hex");
  if (oid.length !== 16 || cv.length !== 32 || pid.length !== 32) {
    throw new Error("身份链输入长度非法（obj-16B/cv-32B/cid-32B）");
  }
  return sha256Hex(Buffer.concat([oid, cv, pid]));
}

// §7.1 行 1~3 封闭换算（暂定口径——P-OPT-5 登记中）。
function computeMetrics(f) {
  const env =
    f.envelope.xMax - f.envelope.xMin +
    f.envelope.yMax - f.envelope.yMin +
    f.envelope.zMax - f.envelope.zMin;
  const mass = f.linkMassesKg.reduce((a, b) => a + b, 0.0);
  const margin = f.margins.length === 0 ? null : Math.min(...f.margins);
  return { envelope: env, structuralMass: mass, minJointMargin: margin };
}

// ---- 生成候选批（§8.2 与 opt-lhs-golden 同款算法——两维连续）。

function generateBatch(cfg, vars) {
  const dims = [...vars].sort((a, b) =>
    a.bindingId < b.bindingId ? -1 : a.bindingId > b.bindingId ? 1 : 0,
  );
  const n = cfg.budget.maxCandidates - 1;
  const batch = [{ label: baseline.label, items: [], factKey: "baseline" }];
  let state = BigInt(cfg.seed);
  const nextU64 = () => {
    const [v, s] = splitmixNext(state);
    state = s;
    return v;
  };
  const layerTable = [];
  for (let d = 0; d < dims.length; ++d) {
    const perm = Array.from({ length: n }, (_, i) => i);
    for (let i = n - 1; i >= 1; --i) {
      const j = Number(nextU64() % BigInt(i + 1));
      const t = perm[i];
      perm[i] = perm[j];
      perm[j] = t;
    }
    layerTable.push({ bindingId: dims[d].bindingId, layers: perm });
  }
  // 事实按生成序铺位（factKey 循环取——9 事实对应 1 基线＋7 采样；末位
  // 余量裁断由调用方保证：本数据源事实数＝批大小，逐位对齐）。
  for (let i = 0; i < n; ++i) {
    const items = [];
    for (let d = 0; d < dims.length; ++d) {
      const b = dims[d];
      const layer = layerTable[d].layers[i];
      const u = unitFrom(nextU64());
      const v = b.lowerBound + ((layer + u) / n) * (b.upperBound - b.lowerBound);
      items.push({ bindingId: b.bindingId, kind: "continuous", scalarValue: v });
    }
    batch.push({ label: "cand-" + String(i), items, factKey: null });
  }
  return { dims, batch, layerTable };
}

const { dims, batch } = generateBatch(config, variables);
if (batch.length !== candidateFacts.length) {
  throw new Error(
    `事实表与批大小不对齐（batch=${batch.length} facts=${candidateFacts.length}）` +
      "——调整 maxCandidates 或事实表",
  );
}
// 非基线候选按生成序绑定事实键（c1..c5/v1/v2/m1 即 candidateFacts[1..7]）。
for (let i = 1; i < batch.length; ++i) {
  batch[i].factKey = candidateFacts[i].key;
}

// ---- 身份链与逐候选黄金（状态封闭推导——§7.5/§7.4 编排执行点）。

function statusOf(kind) {
  if (kind === "feasible") return "feasible";
  if (kind === "limit-violation") return "infeasible"; // Must 违例——不进可行集
  if (kind === "margin-missing") return "data-insufficient"; // 激活目标缺失
  throw new Error("未知事实 kind: " + kind);
}

const candidates = batch.map((c, i) => {
  const f = candidateFacts[i];
  const patchId = "cid-" + sha256Hex(patchCanonicalBytes(c.items));
  const cidHex = candidateIdHex(baseline.objectOid, baseline.contentVersion, patchId.slice(4));
  return {
    index: i - 1, // -1＝基线
    label: c.label,
    factKey: f.key,
    isBaseline: i === 0,
    patchId,
    candidateId: "cnd-" + cidHex,
    expectedStatus: statusOf(f.kind),
    metrics: computeMetrics(f),
  };
});

// =====================================================================
// Pareto 非支配独立复算（§7.4 定义式——支配/分层/稳定排序三键）
// =====================================================================

function closeWithin(a, b, tol) {
  // 附录 D C4：|value−reference| ≤ relative·|reference|＋absolute（参考元＝b）。
  return Math.abs(a - b) <= tol.relative * Math.abs(b) + tol.absolute;
}

function dominates(a, b, objectives) {
  // 第一步：∀i aᵢ 不劣于 bᵢ（min: a≤b∨closeWithin；max: a≥b∨closeWithin）。
  for (const o of objectives) {
    const av = a.metrics[o.field];
    const bv = b.metrics[o.field];
    const notWorse =
      o.direction === "min" ? av <= bv || closeWithin(av, bv, o.tolerance)
                            : av >= bv || closeWithin(av, bv, o.tolerance);
    if (!notWorse) return false;
  }
  // 第二步：∃j 严格优于（差异须超出容差——近似相等互不支配的自洽性）。
  for (const o of objectives) {
    const av = a.metrics[o.field];
    const bv = b.metrics[o.field];
    const close = closeWithin(av, bv, o.tolerance);
    const strictly =
      o.direction === "min" ? av < bv && !close : av > bv && !close;
    if (strictly) return true;
  }
  return false;
}

// 目标集声明（metricId token → 指标字段名）——声明序即排序键②消费序。
function bindObjectives(list) {
  const fieldOf = {
    "opt.metric.envelope": "envelope",
    "opt.metric.structural-mass": "structuralMass",
    "opt.metric.min-joint-margin": "minJointMargin",
  };
  return list.map((o) => ({ ...o, field: fieldOf[o.metricId] }));
}

// buildFront 独立复算：去重保首见 → NSGA 逐层（剩余集内互不支配）→
// 稳定排序三键（rank 升序→声明序严格字典序〔无容差〕→CandidateId 字典序）。
function buildFront(members, objectives) {
  const objectivesBound = bindObjectives(objectives);
  const unique = [];
  let duplicatesDropped = 0;
  for (const m of members) {
    if (unique.some((u) => u.candidateId === m.candidateId)) {
      ++duplicatesDropped;
      continue;
    }
    unique.push(m);
  }
  const n = unique.length;
  const rank = new Array(n).fill(0);
  const assigned = new Array(n).fill(false);
  let remaining = n;
  for (let r = 0; remaining > 0; ++r) {
    const front = [];
    for (let i = 0; i < n; ++i) {
      if (assigned[i]) continue;
      let dominatedByAny = false;
      for (let j = 0; j < n; ++j) {
        if (j === i || assigned[j]) continue;
        if (dominates(unique[j], unique[i], objectivesBound)) {
          dominatedByAny = true;
          break;
        }
      }
      if (!dominatedByAny) front.push(i);
    }
    if (front.length === 0) throw new Error("分层异常（空层）——黄金复算缺陷");
    for (const i of front) {
      rank[i] = r;
      assigned[i] = true;
      --remaining;
    }
  }
  const order = Array.from({ length: n }, (_, i) => i);
  order.sort((lhs, rhs) => {
    if (rank[lhs] !== rank[rhs]) return rank[lhs] - rank[rhs];
    for (const o of objectivesBound) {
      const vl = unique[lhs].metrics[o.field];
      const vr = unique[rhs].metrics[o.field];
      if (vl < vr) return o.direction === "min" ? -1 : 1;
      if (vr < vl) return o.direction === "min" ? 1 : -1;
    }
    return unique[lhs].candidateId < unique[rhs].candidateId
      ? -1
      : unique[lhs].candidateId > unique[rhs].candidateId
        ? 1
        : 0;
  });
  const entries = order.map((i) => ({
    candidateId: unique[i].candidateId,
    isBaseline: unique[i].isBaseline,
    nondominationRank: rank[i],
    paretoNondominated: rank[i] === 0,
  }));
  return {
    entries,
    nondominatedIds: entries.filter((e) => e.paretoNondominated).map((e) => e.candidateId),
    feasibleIds: entries.map((e) => e.candidateId),
    duplicatesDropped,
  };
}

// =====================================================================
// 组装黄金（零筛选设计下：可行集＝全部 expectedStatus==feasible 候选）
// =====================================================================

const feasibleMembers = candidates
  .filter((c) => c.expectedStatus === "feasible")
  .map((c) => ({
    candidateId: c.candidateId,
    isBaseline: c.isBaseline,
    metrics: c.metrics,
  }));

const zeroFront = buildFront(feasibleMembers, objectivesZero);
const explicitFront = buildFront(feasibleMembers, objectivesExplicit);

// 容差翻转记录：逐对比较两套前沿的支配关系差（零容差互不支配→显式容差
// 存在支配；或反向）。报告为观测记录（不构造——复算产物）。
function dominanceMatrix(front, objectives) {
  const bound = bindObjectives(objectives);
  const ids = front.feasibleIds;
  const at = new Map(front.entries.map((e) => [e.candidateId, e]));
  const cells = [];
  for (const a of ids) {
    for (const b of ids) {
      if (a === b) continue;
      const ma = { candidateId: a, metrics: feasibleMembers.find((m) => m.candidateId === a).metrics };
      const mb = { candidateId: b, metrics: feasibleMembers.find((m) => m.candidateId === b).metrics };
      if (dominates(ma, mb, bound)) cells.push({ a, b });
    }
  }
  void at;
  return cells;
}

const zeroPairs = new Set(
  dominanceMatrix(zeroFront, objectivesZero).map((p) => p.a + "->" + p.b),
);
const explicitPairs = new Set(
  dominanceMatrix(explicitFront, objectivesExplicit).map((p) => p.a + "->" + p.b),
);
const gained = [...explicitPairs].filter((p) => !zeroPairs.has(p)).sort();
const lost = [...zeroPairs].filter((p) => !explicitPairs.has(p)).sort();

// 审计口径（零筛选设计的封闭值——黄金复算与 T06 审计字段同名）。
const audit = {
  candidatesGenerated: candidates.length,
  quickEvaluated: candidates.length, // Quick 批全量评估（含基线）
  quickScreenedOut: 0, // Quick-Feasible（6）≤ maxVerifiedCandidates（6）——零筛选
  verifiedEvaluated: feasibleMembers.length,
  cacheLookups: 0,
  duplicatesDropped: 0,
};

const inputs = {
  dataset: "opt-pareto-golden@1.0.0",
  contract:
    "units/optimization.md §7.4/§7.5 Pareto 非支配与不可行不入可行集黄金面（WP-20-T11，AT-09）",
  config,
  variables,
  dimensionOrder: dims.map((d) => d.bindingId),
  baseline,
  jointLimits,
  candidateFacts,
  objectivesZeroTolerance: objectivesZero,
  objectivesExplicitTolerance: objectivesExplicit,
};

const expected = {
  dataset: "opt-pareto-golden@1.0.0",
  contract:
    "状态＝§7.5/§7.4 封闭推导；前沿＝§7.4 定义式独立复算（node——与产品零共享代码）；身份链＝§4.2/§5.5 SHA-256 独立复算",
  candidates,
  audit,
  run: {
    feasibleIds: zeroFront.feasibleIds,
    nondominatedIds: zeroFront.nondominatedIds,
    entries: zeroFront.entries,
    duplicatesDropped: zeroFront.duplicatesDropped,
  },
  explicitToleranceScenario: {
    entries: explicitFront.entries,
    nondominatedIds: explicitFront.nondominatedIds,
    dominancePairsGainedUnderTolerance: gained,
    dominancePairsLostUnderTolerance: lost,
  },
};

writeFileSync(inputsPath, JSON.stringify(inputs, null, 2) + "\n", "utf8");
writeFileSync(expectedPath, JSON.stringify(expected, null, 2) + "\n", "utf8");

// ---- 黄金自检：翻转记录必须非空（容差支配场景的设计目标——无翻转＝
//      数据集失去 AT-09"容差支配"观测价值，出库即失败）。
if (gained.length === 0 && lost.length === 0) {
  throw new Error("显式容差未产生任何支配关系翻转——数据集设计缺陷");
}
console.log(
  `opt-pareto-golden: candidates=${candidates.length} feasible=${feasibleMembers.length}` +
    ` zeroRank0=${zeroFront.nondominatedIds.length} explicitRank0=${explicitFront.nondominatedIds.length}` +
    ` flipGained=${gained.length} flipLost=${lost.length}`,
);
