// =====================================================================
// make_ev_slice_fixture.mjs —— ev-slice-fixture 数据集生成脚本
// （contract-fixture；units/evidence.md §12 EV-T11 产物行"切片契约夹具
// 数据集"、§5.2 canonical 编码规则表、§5.1 双层身份）。
//
// 用法：
//   node make_ev_slice_fixture.mjs    （脚本位于 <版本目录>/generate/ 下）
//
// 生成逻辑（按设计文档的布局规范**独立重实现**——与产品 C++ 编码器
// 同源规则的两条实现，互证字节级确定性；任何一侧漂移都会让
// SliceFixtureContractTest 失败，这正是夹具的防线价值）：
//   1. 读 inputs/slice.json（切片语义描述——单一事实来源）；
//   2. 按快照 refs-only 编码布局（Snapshot.cpp 头注释"SnapshotCodec
//      字节布局"：magic IRDSNAP1＋版本 1＋形态 0＋身份三元组＋闭包/
//      配置/策略/名称映射/工况/外部资源/采样计划/复现块——本夹具仅
//      非零段：身份三元组＋1 闭包条目＋策略/名称映射＋复现块）拼装
//      字节 → snapshotId = "cid-" + sha256；
//   3. 按切片 full 编码布局（Slice.cpp 头注释"SliceCodec 字节布局"：
//      magic IRDSLCE1＋版本 1＋形态 0＋评估面＋快照身份块＋全部冻结
//      条目）拼装字节 → sliceId = "cid-" + sha256；
//   4. baseline-projection 形态（形态 1；快照身份块＋基准类条目子集——
//      Object/SampleSet/NameMap/基准类 Environment 参与，Configuration/
//      Policy/UpstreamResult 排除）→ inputBaselineId = "cid-" + sha256；
//   5. 复核 inputs 声明的一致性（Configuration.contentIdentity 必须
//      等于 SHA-256(canonicalBytes)）；
//   6. 写 expected/slice-identity.json（两形态编码 hex＋三身份），并
//      打印 manifest integrity 申报表（path/sizeBytes/sha256）。
//
// 期望值由脚本生成后人工复核入库（数据集变更须重新审核——testkit.md
// §4.7）；全量文件为 LF 文本（testdata/.gitattributes 钉死行尾）。
// =====================================================================

import { createHash } from "node:crypto";
import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const inputPath = join(here, "..", "inputs", "slice.json");
const outputPath = join(here, "..", "expected", "slice-identity.json");

const model = JSON.parse(readFileSync(inputPath, "utf8"));

// ---------- 字节拼装原语（§5.2：定宽整型大端、长度前缀、无填充） ----------

/** 规范文本 → 原始字节（Id128/Digest256 家族：tag 后 32/64 位小写 hex，
 *  字节序＝文本序——core Identity.cpp tryParseId128 对称规则）。 */
function idBytes(canonical) {
  const body = canonical.slice(canonical.indexOf("-") + 1);
  if (!/^[0-9a-f]+$/.test(body) || body.length % 2 !== 0) {
    throw new Error(`规范文本 hex 体非法: ${canonical}`);
  }
  return Buffer.from(body, "hex");
}

function hexToBytes(hex) {
  if (!/^[0-9a-f]*$/.test(hex) || hex.length % 2 !== 0) {
    throw new Error(`hex 非法: ${hex}`);
  }
  return Buffer.from(hex, "hex");
}

function bytesToHex(bytes) {
  return Buffer.from(bytes).toString("hex");
}

function sha256Hex(bytes) {
  return createHash("sha256").update(bytes).digest("hex");
}

/** 追加 16 位无符号整型（大端）。 */
function appendU16(out, v) {
  out.push((v >> 8) & 0xff, v & 0xff);
}

/** 追加 32 位无符号整型（大端）。 */
function appendU32(out, v) {
  out.push((v >>> 24) & 0xff, (v >>> 16) & 0xff, (v >>> 8) & 0xff, v & 0xff);
}

/** 追加 64 位无符号整型（大端——采样计划计数字段的承载，本夹具零段）。 */
function appendU64(out, v) {
  const big = BigInt(v);
  for (let shift = BigInt(56); shift >= BigInt(0); shift -= BigInt(8)) {
    out.push(Number((big >> shift) & BigInt(0xff)));
  }
}

/** 追加原始字节。 */
function appendRaw(out, bytes) {
  for (const b of bytes) {
    out.push(b & 0xff);
  }
}

/** 追加"2 字节长度＋UTF-8 字符串"（§5.2 字符串规则：禁 NUL、≤65535）。 */
function appendLengthString(out, s) {
  const bytes = Buffer.from(s, "utf8");
  if (bytes.includes(0)) {
    throw new Error(`字符串含 NUL（§5.2 编码安全）: ${s}`);
  }
  if (bytes.length > 0xffff) {
    throw new Error(`字符串超出 65535 字节: ${s}`);
  }
  appendU16(out, bytes.length);
  appendRaw(out, bytes);
}

// ---------- 依赖条目编码（两形态共用——单点实现不漂移） ----------

/** DependencyKind 枚举声明序值（Dependency.hpp 权威序——kind 字节的
 *  唯一来源；新增枚举值时此处同步）。 */
const KIND_VALUE = {
  object: 0,
  configuration: 1,
  policy: 2,
  "name-map": 3,
  "sample-set": 4,
  "upstream-result": 5,
  environment: 6,
};

/** 单条依赖条目编码（Slice.cpp 头注释"条目编码"布局：
 *  [1 kind][2 len key][1 applied][1 reason presence][载荷按 kind]）。 */
function encodeEntry(entry) {
  const out = [];
  const kindValue = KIND_VALUE[entry.kind];
  if (kindValue === undefined) {
    throw new Error(`未知依赖 kind: ${entry.kind}`);
  }
  out.push(kindValue);
  appendLengthString(out, entry.key);
  out.push(entry.applied ? 1 : 0);
  if (entry.notAppliedReason !== null && entry.notAppliedReason !== undefined) {
    if (entry.applied !== false) {
      throw new Error(`applied/notAppliedReason 配对矛盾: ${entry.key}`);
    }
    out.push(1);
    appendLengthString(out, entry.notAppliedReason);
  } else {
    out.push(0);
  }

  // 载荷（按 kind——本夹具实现三类的编码面；其余 kind 的布局见
  // Slice.cpp 头注释，夹具未覆盖段落不实现——缺失实现即显性报错）。
  switch (entry.kind) {
    case "object": {
      // [16 objectId][32 contentVersion][2 len objectTypeToken]
      appendRaw(out, idBytes(entry.objectId));
      appendRaw(out, idBytes(entry.contentVersion));
      appendLengthString(out, entry.objectTypeToken);
      break;
    }
    case "configuration": {
      // [2 len configKindToken][4 len canonicalBytes][32 cid]
      appendLengthString(out, entry.configKindToken);
      const bytes = hexToBytes(entry.canonicalBytesHex);
      appendU32(out, bytes.length);
      appendRaw(out, bytes);
      appendRaw(out, idBytes(entry.contentIdentity));
      break;
    }
    case "policy": {
      // [32 policyContentIdentity]
      appendRaw(out, idBytes(entry.policyContentIdentity));
      break;
    }
    default:
      throw new Error(`夹具生成器未实现 kind 的编码: ${entry.kind}`);
  }
  return out;
}

// ---------- 快照 refs-only 编码（Snapshot.cpp 布局的独立重实现） ----------

function encodeSnapshotRefsOnly(snap) {
  const out = [];
  // [0..7] magic "IRDSNAP1"；[8] codec 版本 =1；[9] 形态字节 0=refs-only。
  appendRaw(out, Buffer.from("IRDSNAP1", "ascii"));
  out.push(1, 0);

  // 身份三元组（revisionSeq 不编码——I-1）。
  appendRaw(out, idBytes(snap.identity.project));
  appendRaw(out, idBytes(snap.identity.branch));
  appendRaw(out, idBytes(snap.identity.revision));

  // 对象引用闭包（冻结序＝objectId 序——本夹具单条目）。
  const objects = [...snap.objects].sort((a, b) =>
    a.objectId.localeCompare(b.objectId),
  );
  appendU32(out, objects.length);
  for (const e of objects) {
    appendRaw(out, idBytes(e.objectId));
    appendRaw(out, idBytes(e.contentVersion));
    appendLengthString(out, e.objectTypeToken);
    appendRaw(out, hexToBytes(e.digestHex));
  }

  // 分析配置引用（本夹具零条目）。
  appendU32(out, 0);

  // 策略与名称映射内容身份（必填）。
  appendRaw(out, idBytes(snap.policyContentIdentity));
  appendRaw(out, idBytes(snap.nameMapContentIdentity));

  // 必验工况集合（本夹具零条目——空必验集合法，EV-CASESET 同口径）。
  appendU32(out, 0);

  // 外部资源（零条目）。
  appendU32(out, 0);

  // 采样计划（零条目）。
  appendU32(out, 0);

  // 复现块（必填标量＋保序版本族＋两个可选字段 presence 字节）。
  appendLengthString(out, snap.reproduction.productVersion);
  appendLengthString(out, snap.reproduction.evidenceContractVersion);
  appendU32(out, snap.reproduction.codecVersions.length);
  for (const v of snap.reproduction.codecVersions) {
    appendLengthString(out, v);
  }
  if (snap.reproduction.compilerContractVersion !== null
      && snap.reproduction.compilerContractVersion !== undefined) {
    out.push(1);
    appendLengthString(out, snap.reproduction.compilerContractVersion);
  } else {
    out.push(0);
  }
  if (snap.reproduction.collisionBackendVersion !== null
      && snap.reproduction.collisionBackendVersion !== undefined) {
    out.push(1);
    appendLengthString(out, snap.reproduction.collisionBackendVersion);
  } else {
    out.push(0);
  }
  return Uint8Array.from(out);
}

// ---------- 切片双形态编码（Slice.cpp 布局的独立重实现） ----------

/** full 形态：magic IRDSLCE1＋版本 1＋形态 0＋[2 len key][4 ver][32
 *  snapshotId][4 N][entries...]。 */
function encodeSliceFull(slice, snapshotIdBytes, entries) {
  const out = [];
  appendRaw(out, Buffer.from("IRDSLCE1", "ascii"));
  out.push(1, 0);
  appendLengthString(out, slice.evaluationKey);
  appendU32(out, slice.evaluatorContractVersion);
  appendRaw(out, snapshotIdBytes);
  appendU32(out, entries.length);
  // 冻结序＝(kind,key) 字典序（builder 规范化——生成器同规则排序）。
  const sorted = [...entries].sort((a, b) => {
    const ka = KIND_VALUE[a.kind];
    const kb = KIND_VALUE[b.kind];
    return ka !== kb ? ka - kb : a.key.localeCompare(b.key);
  });
  for (const e of sorted) {
    appendRaw(out, encodeEntry(e));
  }
  return Uint8Array.from(out);
}

/** baseline-projection 形态：形态 1＋[32 snapshotId][4 M][基准类条目]。
 *  基准类参与集（D-1）：Object/SampleSet/NameMap 全量＋基准类 Environment；
 *  Configuration/Policy/UpstreamResult 排除。 */
function encodeSliceBaseline(slice, snapshotIdBytes, entries) {
  const out = [];
  appendRaw(out, Buffer.from("IRDSLCE1", "ascii"));
  out.push(1, 1);
  appendRaw(out, snapshotIdBytes);
  const baselineKinds = new Set(["object", "sample-set", "name-map"]);
  const baseline = [...entries]
    .filter((e) => baselineKinds.has(e.kind))
    .sort((a, b) => {
      const ka = KIND_VALUE[a.kind];
      const kb = KIND_VALUE[b.kind];
      return ka !== kb ? ka - kb : a.key.localeCompare(b.key);
    });
  appendU32(out, baseline.length);
  for (const e of baseline) {
    appendRaw(out, encodeEntry(e));
  }
  return Uint8Array.from(out);
}

// ---------- 主流程：一致性复核 → 身份计算 → expected 落盘 ----------

// 复核 Configuration 载荷的一致性（contentIdentity 必须＝SHA-256(bytes)）。
for (const entry of model.entries) {
  if (entry.kind === "configuration") {
    const actual = "cid-" + sha256Hex(hexToBytes(entry.canonicalBytesHex));
    if (actual !== entry.contentIdentity) {
      throw new Error(
        `Configuration.contentIdentity 与 SHA-256(canonicalBytes) 不符: `
          + `声明 ${entry.contentIdentity}，实算 ${actual}`);
    }
  }
}

const snapshotEncoding = encodeSnapshotRefsOnly(model.snapshot);
const snapshotId = "cid-" + sha256Hex(snapshotEncoding);

const fullEncoding = encodeSliceFull(
  model, hexToBytes(snapshotId.slice(4)), model.entries);
const sliceId = "cid-" + sha256Hex(fullEncoding);

const baselineEncoding = encodeSliceBaseline(
  model, hexToBytes(snapshotId.slice(4)), model.entries);
const inputBaselineId = "cid-" + sha256Hex(baselineEncoding);

const expected = {
  schema: "ev-slice-fixture-expected/1",
  description:
    "切片契约夹具期望值——三身份（snapshotId/sliceId/inputBaselineId）与 "
    + "SliceCodec 双形态规范编码（hex，大端位模式逐字节）。由 "
    + "generate/make_ev_slice_fixture.mjs 按 §5.2 布局规则独立重实现产出，"
    + "C++ 契约测试（SliceFixtureContractTest）以产品编码器对照本文件"
    + "逐字节核对（双实现互证——任何一侧漂移即显性失败）。",
  snapshotId,
  sliceId,
  inputBaselineId,
  encodingFullHex: bytesToHex(fullEncoding),
  encodingBaselineProjectionHex: bytesToHex(baselineEncoding),
};

mkdirSync(dirname(outputPath), { recursive: true });
writeFileSync(outputPath, JSON.stringify(expected, null, 2) + "\n", "utf8");

// ---------- manifest integrity 申报表打印（登记用） ----------

function printIntegrity(relPath) {
  const bytes = readFileSync(join(here, "..", relPath));
  console.log(
    `    { "path": "${relPath}", "sha256": "${sha256Hex(bytes)}", `
      + `"sizeBytes": ${bytes.length} }`,
  );
}

console.log("生成完成：expected/slice-identity.json");
console.log(`  snapshotId      = ${snapshotId}`);
console.log(`  sliceId         = ${sliceId}`);
console.log(`  inputBaselineId = ${inputBaselineId}`);
console.log("manifest integrity 申报表（manifest.json 登记用）：");
printIntegrity("inputs/slice.json");
printIntegrity("expected/slice-identity.json");
printIntegrity("generate/make_ev_slice_fixture.mjs");
