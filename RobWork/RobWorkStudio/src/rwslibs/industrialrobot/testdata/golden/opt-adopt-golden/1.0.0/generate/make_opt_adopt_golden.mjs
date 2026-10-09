/**
 * make_opt_adopt_golden.mjs —— optimization 采用守卫黄金数据集的独立参考
 * 实现（WP-20-T11，OPT-08——"候选归属运行结果、采用经两步命令组合、基线
 * 字节不变"黄金面，AT-12）。
 *
 * 数据集：opt-adopt-golden（kind=contract-fixture）。黄金内容：
 *   A. 身份链（§4.2/§5.5 冻结公式——node crypto 独立计算）：补丁 canonical
 *      字节（"IRDOPTP1"编码）→ CandidatePatchId → CandidateId ＝ SHA-256
 *      (基线根对象 16B‖内容版本 32B‖补丁身份 32B)；默认方案分支显示名
 *      ＝"方案 "＋候选 id 文本前 12 位（T07 组装面实现口径的黄金锚）。
 *   B. 候选设计字节（P-OPT-3 裁决前的物化缝黄金）：测试注入缝按本脚本
 *      冻结的迷你规范格式物化候选设计——magic "IRDDSGN1"＋u32 格式版本 1
 *      ＋u16 字段数＋每字段（u16 名称字节长＋名称 UTF-8＋f64 小端 SI 值）
 *      按名称字典序；候选表＝基线表＋补丁覆盖（bindingId→设计字段映射
 *      为黄金输入的显式声明）；期望 payload 摘要＝SHA-256（黄金锚——
 *      C++ 侧物化缝同式构造后逐字节对账）。
 *   C. 两步命令组合语义（§10.2 组装面）：step1 建支语义（baseRevisionId＝
 *      运行输入修订、不复制对象）＋step2 应用语义（token＝modeling 既有
 *      注册面 "apply-robot-design"〔只引用不注册——DOPT-6〕、payload＝
 *      物化字节、expectedRevision＝运行输入修订〔新分支 tip＝建支基〕、
 *      双编译恒声明〔MDL-06〕）＋完整复算提示恒位（§10.1 RECALC）＋
 *      基线过期仅标记不阻塞（§10.3 第 4 项——tip 前进场景）。
 *   D. 差异预览（§10.4——MDL-08 消费投影）：基线表 vs 候选表的逐字段
 *      闭式 diff（modified 条目按字段名字典序；传动比条目在场→零警告
 *      ——P-OPT-8"不虚构"的反面锚）。
 *
 * 用法：node make_opt_adopt_golden.mjs（在本目录执行）。
 */

import { writeFileSync } from "node:fs";
import { createHash } from "node:crypto";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const here = dirname(fileURLToPath(import.meta.url));
const inputsPath = join(here, "..", "inputs", "adopt-inputs.json");
const expectedPath = join(here, "..", "expected", "adopt-expected.json");

// =====================================================================
// 黄金唯一数据源
// =====================================================================

// 身份面（固定 canonical 文本——黄金确定性输入；运行聚合由测试以同一组
// 身份组装，组装器只校验非保留值）。
const identity = {
  project: "prj-000000000000000000000000000000c1",
  branch: "brn-000000000000000000000000000000c2",
  revision: "rev-000000000000000000000000000000c3",
  snapshot: "cid-00000000000000000000000000000000000000000000000000000000000000c4",
  baselineRoot: "obj-000000000000000000000000000000c5",
  baselineCv:
    "cv-00000000000000000000000000000000000000000000000000000000000000c6",
};

// 基线状态（正面形态：tip＝运行输入修订、可写；过期场景单独声明）。
const baselineState = { tip: identity.revision, writable: true };
const baselineStateStale = {
  tip: "rev-000000000000000000000000000000c9", // ≠ 运行输入修订——过期预期
  writable: true,
};

// 研究绑定（§5.3 词表实例化：传动比 c 口径无量纲＋DH 长度 m）。
const variables = [
  {
    bindingId: "mdl.drivetrain.ratio[1]",
    kind: "continuous",
    unit: "1",
    lowerBound: 80.0,
    upperBound: 160.0,
    step: 0.0,
    defaultValue: 120.0,
    authorized: true,
    locked: false,
    authorityFieldPath: "robot-drivetrain/ratioPerJoint[j]",
    diagSubject: "obj-000000000000000000000000000000c7",
    // 补丁值→候选设计字段的覆盖落点（物化缝黄金的显式映射声明）。
    designField: "ratioPerJoint[1]",
  },
  {
    bindingId: "mdl.joint[2].dh.a",
    kind: "continuous",
    unit: "m",
    lowerBound: 0.2,
    upperBound: 0.4,
    step: 0.0,
    defaultValue: 0.3,
    authorized: true,
    locked: false,
    authorityFieldPath: "robot-design/joints[2]/dh/a",
    diagSubject: "obj-000000000000000000000000000000c8",
    designField: "dhA[2]",
  },
];

// 候选补丁（双变量复合候选——单命令整体提交，MDL-06）。
const patchItems = [
  { bindingId: "mdl.drivetrain.ratio[1]", kind: "continuous", scalarValue: 100.0 },
  { bindingId: "mdl.joint[2].dh.a", kind: "continuous", scalarValue: 0.25 },
];

// 基线设计表（迷你规范格式的字段面——SI 值；名称字典序由序列化排序）。
const baselineDesignTable = {
  "dhA[2]": 0.3,
  "linkMass[1]": 5.5,
  "ratioPerJoint[1]": 120.0,
  "reach[1]": 0.85,
};

// =====================================================================
// 身份链与迷你规范格式（§4.2/§5.5＋本脚本冻结的物化缝格式——独立实现）
// =====================================================================

const MASK64 = (1n << 64n) - 1n; // （占位——本数据集无随机性；保持与姊妹脚本同构）

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

function candidateIdHex(oidCanonical, cvCanonical, patchIdHex) {
  const oid = Buffer.from(oidCanonical.slice("obj-".length), "hex");
  const cv = Buffer.from(cvCanonical.slice("cv-".length), "hex");
  const pid = Buffer.from(patchIdHex, "hex");
  if (oid.length !== 16 || cv.length !== 32 || pid.length !== 32) {
    throw new Error("身份链输入长度非法（obj-16B/cv-32B/cid-32B）");
  }
  return sha256Hex(Buffer.concat([oid, cv, pid]));
}

// 迷你规范格式 "IRDDSGN1"：u32 版本 1＋u16 字段数＋每字段（u16 名称长＋
// 名称 UTF-8＋f64 小端）按名称字典序——物化缝黄金的确定性字节面。
function designTableBytes(table) {
  const names = Object.keys(table).sort((a, b) => (a < b ? -1 : a > b ? 1 : 0));
  const parts = [];
  parts.push(Buffer.from("IRDDSGN1", "utf8"));
  const head = Buffer.alloc(6);
  head.writeUInt32LE(1, 0);
  head.writeUInt16LE(names.length, 4);
  parts.push(head);
  for (const name of names) {
    const nameBuf = Buffer.from(name, "utf8");
    const len = Buffer.alloc(2);
    len.writeUInt16LE(nameBuf.length, 0);
    const val = Buffer.alloc(8);
    val.writeDoubleLE(Object.is(table[name], -0.0) ? 0.0 : table[name], 0);
    parts.push(len, nameBuf, val);
  }
  return Buffer.concat(parts);
}

// 闭式差异（§10.4 投影——modified 条目按字段名字典序；两侧同字段值不同）。
function diffEntries(baselineTable, candidateTable, rootOid) {
  const names = Object.keys(baselineTable)
    .filter((n) => n in candidateTable && baselineTable[n] !== candidateTable[n])
    .sort((a, b) => (a < b ? -1 : a > b ? 1 : 0));
  return names.map((n) => ({
    group: "parameters",
    kind: "modified",
    objectId: rootOid,
    subjectPath: n,
    field: n,
    valueChanged: true,
    provenanceChanged: false,
    baselineText: baselineTable[n].toFixed(6),
    candidateText: candidateTable[n].toFixed(6),
  }));
}

// =====================================================================
// 组装黄金
// =====================================================================

const patchId = "cid-" + sha256Hex(patchCanonicalBytes(patchItems));
const cidHex = candidateIdHex(
  identity.baselineRoot,
  identity.baselineCv,
  patchId.slice(4),
);
const candidateId = "cnd-" + cidHex;

// 候选设计表＝基线表＋补丁覆盖（bindingId→designField 显式映射）。
const candidateDesignTable = { ...baselineDesignTable };
for (const item of patchItems) {
  const binding = variables.find((v) => v.bindingId === item.bindingId);
  if (binding == null) throw new Error("补丁绑定不在研究定义: " + item.bindingId);
  candidateDesignTable[binding.designField] = item.scalarValue;
}

const candidateBytes = designTableBytes(candidateDesignTable);
const baselineBytes = designTableBytes(baselineDesignTable);

const diffPreview = {
  entries: diffEntries(baselineDesignTable, candidateDesignTable, identity.baselineRoot),
  warnings: [], // 传动比条目在场（ratioPerJoint[1] modified）——P-OPT-8 零警告
};

const inputs = {
  dataset: "opt-adopt-golden@1.0.0",
  contract:
    "units/optimization.md §10.1~§10.4 采用守卫组装面黄金（WP-20-T11，AT-12）——两步命令语义/身份链/物化缝字节/闭式差异",
  identity,
  baselineStates: { current: baselineState, stale: baselineStateStale },
  variables,
  patchItems,
  baselineDesignTable,
  // 物化缝迷你格式声明（测试注入缝按此构造——期望摘要见 expected）。
  designFormat: {
    magic: "IRDDSGN1",
    version: 1,
    encoding:
      "u32 版本＋u16 字段数＋每字段（u16 名称长＋名称 UTF-8＋f64 小端 SI 值）按名称字典序",
  },
};

const expected = {
  dataset: "opt-adopt-golden@1.0.0",
  contract:
    "身份链＝§4.2/§5.5 SHA-256 独立复算；物化摘要＝迷你规范格式 SHA-256；命令语义＝§10.2/§10.3 组装面封闭声明",
  patchId,
  candidateId,
  defaultBranchLabel: "方案 " + candidateId.slice(4, 16), // 候选 id 文本前 12 位
  plan: {
    allowApply: true, // 物化缝已注入＋项目可写＋前置 1~3 满足
    expectedStaleBaseline: false, // tip＝运行输入修订
    recalcRequired: true, // 完整复算提示恒位（§10.1 RECALC）
    step2: {
      commandToken: "apply-robot-design", // modeling 既有注册面（DOPT-6 只引用）
      payloadSha256: sha256Hex(candidateBytes),
      expectedRevisionEqualsRunRevision: true, // 新分支 tip＝建支基＝运行输入修订
      requiresDualCompile: true, // MDL-06 双编译恒声明
    },
    createBranch: {
      baseRevisionEqualsRunRevision: true, // 分支记录 baseRevisionId＝运行输入修订
      copiesObjects: false, // 建支不复制对象（新分支 tip 指向同一修订）
    },
    diffPreview,
  },
  staleScenario: {
    baselineState: baselineStateStale,
    expectedStaleBaseline: true, // 仅标记不阻塞（§10.3 第 4 项）
    allowApplyUnchanged: true, // 通道化状态不受过期标记影响
  },
  baselineInvariance: {
    baselineBytesSha256: sha256Hex(baselineBytes), // 组装前后基线字节摘要不变（AT-12）
  },
};

// 黄金自检：diff 必须含传动比条目（零警告锚的前提——否则 warnings 语义
// 失去对照面），且候选表必须与基线表不同（补丁确有覆盖）。
if (!diffPreview.entries.some((e) => e.field === "ratioPerJoint[1]")) {
  throw new Error("差异预览缺传动比条目——P-OPT-8 零警告锚不成立");
}
if (sha256Hex(baselineBytes) === sha256Hex(candidateBytes)) {
  throw new Error("候选设计字节与基线相同——补丁覆盖未生效");
}

writeFileSync(inputsPath, JSON.stringify(inputs, null, 2) + "\n", "utf8");
writeFileSync(expectedPath, JSON.stringify(expected, null, 2) + "\n", "utf8");
console.log(
  `opt-adopt-golden: candidateId=${candidateId.slice(0, 16)}… diffEntries=${diffPreview.entries.length}`,
);
