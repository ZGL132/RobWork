// =============================================================================
// make_pol_policy_fixture.mjs — pol-policy-codec-fixture 独立期望生成器
// （POL-T11，≙WP-07-T11；units/policy.md §5.3 编码规则的 JS 独立重实现）。
//
// 设计依据：
//   - units/policy.md §11 矩阵尾注：解析/编码契约夹具按 testkit manifest
//     schema 登记（contract-fixture 类）；§12 POL-T11 行（数据集交付面）；
//   - units/testkit.md §4.2.2（manifest 字段表）、§4.5（integrity 申报）；
//   - ev-slice-fixture 先例（EV-T11）：expected 由独立重实现产出，与产品
//     C++ 编码器双实现互证——任一侧漂移即消费测试显性失败。
//
// 独立性声明：本脚本只依赖 node:crypto（SHA-256——与 core ContentDigester
// 同算法的标准实现）与 node:fs，不 import 任何产品代码；编码布局按
// units/policy.md §5.3 契约文字逐条重写（帧头/大端/长度前缀/presence 字节/
// IEEE754 位模式/规范序），不是 PolicyInput.cpp 的转写。
//
// 产出：
//   - expected/identity.json（fullEncodingHex / semanticProjectionHex /
//     contentIdentityHex / siValues——消费测试 PolicyCodecFixtureContractTest
//     的全部对照面）；
//   - stdout 打印 manifest integrity 申报表（path/sizeBytes/sha256）——
//     登记 manifest.json 时人工誊入（TK-T03 先例同款流程）。
//
// 用法：node generate/make_pol_policy_fixture.mjs（在版本目录或仓库任意
// 位置运行——输出写在脚本同目录的 ../expected/identity.json）。
// =============================================================================

import { createHash } from "node:crypto";
import { readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { Buffer } from "node:buffer";

const scriptDir = dirname(fileURLToPath(import.meta.url));
const versionDir = join(scriptDir, "..");

// ---------------------------------------------------------------------------
// 字节装配原语（大端/长度前缀/presence/位模式——§5.3 编码总纲）。
// ---------------------------------------------------------------------------

/** u32 大端 4 字节。 */
function u32(v) {
  const b = Buffer.alloc(4);
  b.writeUInt32BE(v >>> 0, 0);
  return b;
}

/** 单字节（布尔/枚举/presence）。 */
function u8(v) {
  return Buffer.from([v & 0xff]);
}

/** presence 字节：0＝缺失 / 1＝存在（缺失≠空值≠零——CR-02 纪律）。 */
function presence(has) {
  return u8(has ? 1 : 0);
}

/** u32 长度前缀 UTF-8 文本（字节直载，无转码）。 */
function lpText(s) {
  const payload = Buffer.from(s, "utf8");
  return Buffer.concat([u32(payload.length), payload]);
}

/** 16 字节身份块（hex 字符串→字节直出）。 */
function id128(hex) {
  const b = Buffer.from(hex, "hex");
  if (b.length !== 16) throw new Error(`id128 需要 16 字节 hex: ${hex}`);
  return b;
}

/** IEEE754 双精度 8 字节位模式（大端——canonicalF64 的 JS 对偶）。 */
function f64(v) {
  const b = Buffer.alloc(8);
  b.writeDoubleBE(v, 0);
  return b;
}

// ---------------------------------------------------------------------------
// 词表字节映射（枚举字节一经交付冻结——PolicyInput.cpp 同表；数值来自
// §5.3 契约，此处按词表文字重写）。
// ---------------------------------------------------------------------------

const DOMAIN_BYTE = { Self: 0, Environment: 1, Tool: 2, Scene: 3 };
const SCOPE_KIND_BYTE = { Object: 0, Role: 1, Group: 2 };
const LEVEL_BYTE = { Must: 0, Should: 1 };
const ORIGIN_KIND_BYTE = { Template: 0, Imported: 1, UserEdited: 2, SystemDefault: 3 };
const MODE_BYTE = { Preview: 0, Quick: 1, Verified: 2 };

/** 显示单位→SI 因子（core Units 注册表的 JS 镜像——只含本夹具用到的行）。 */
const SI_FACTOR = { m: 1.0, mm: 0.001, rad: 1.0, "": 1.0 };

/** SI 归一（core convert 的逐位对偶：(v·from)/to——mm→m 即 (v·0.001)/1）。 */
function toSi(value, unit) {
  const factorFrom = SI_FACTOR[unit];
  if (factorFrom === undefined) throw new Error(`未登记单位因子: "${unit}"`);
  return (value * factorFrom) / 1.0; // to＝SI 单位（factor 1）
}

// ---------------------------------------------------------------------------
// 规范序（POL-ID-2 的机制定义——§5.3"编码规范序"的 JS 重写）。
// ---------------------------------------------------------------------------

/** UTF-8 字节字典序比较（无 locale——与 std::string operator< 同序）。 */
function bytesLess(a, b) {
  return Buffer.compare(Buffer.from(a, "utf8"), Buffer.from(b, "utf8")) < 0;
}

/** ScopeTarget 规范键比较：(kind, kind 内载荷)——Object 字节序/Role/Group 串序。 */
function scopeTargetLess(a, b) {
  if (a.kind !== b.kind) return SCOPE_KIND_BYTE[a.kind] < SCOPE_KIND_BYTE[b.kind];
  if (a.kind === "Object") return a.objectHex < b.objectHex; // 定长小写 hex＝字节序
  if (a.kind === "Role") return bytesLess(a.roleToken, b.roleToken);
  return bytesLess(a.groupName, b.groupName);
}

/** PairRule 规范化：无序对端排序＋列表按 (first,second,level,reason) 升序。 */
function normalizePairRules(rules) {
  const ordered = rules.map((r) => {
    const [first, second] = scopeTargetLess(r.first, r.second)
      ? [r.first, r.second]
      : [r.second, r.first];
    return { ...r, first, second };
  });
  ordered.sort((a, b) => {
    if (scopeTargetLess(a.first, b.first)) return -1;
    if (scopeTargetLess(b.first, a.first)) return 1;
    if (scopeTargetLess(a.second, b.second)) return -1;
    if (scopeTargetLess(b.second, a.second)) return 1;
    if (a.level !== b.level) return LEVEL_BYTE[a.level] < LEVEL_BYTE[b.level] ? -1 : 1;
    return bytesLess(a.reason, b.reason) ? -1 : 1;
  });
  return ordered;
}

/** 集合规范化：升序排序＋去重（比较器/等值器共用同一序——集合语义）。 */
function normalizedSet(items, less) {
  const sorted = [...items].sort(less);
  return sorted.filter((x, i) => i === 0 || less(sorted[i - 1], x));
}

// ---------------------------------------------------------------------------
// 逐字段编码（scopeTarget/pairRule/集合/阈值——两形态复用）。
// ---------------------------------------------------------------------------

function encScopeTarget(t) {
  if (t.kind === "Object") return Buffer.concat([u8(SCOPE_KIND_BYTE.Object), id128(t.objectHex)]);
  if (t.kind === "Role") return Buffer.concat([u8(SCOPE_KIND_BYTE.Role), lpText(t.roleToken)]);
  return Buffer.concat([u8(SCOPE_KIND_BYTE.Group), lpText(t.groupName)]);
}

function encPairRule(r) {
  return Buffer.concat([
    encScopeTarget(r.first),
    encScopeTarget(r.second),
    u8(LEVEL_BYTE[r.level]),
    lpText(r.reason),
  ]);
}

function encPairRuleList(rules) {
  const normalized = normalizePairRules(rules);
  return Buffer.concat([u32(normalized.length), ...normalized.map(encPairRule)]);
}

/** 集合编码：count 前缀＋逐条（域/模式按枚举字节升序去重）。 */
function encEnumSet(items, byteTable) {
  // 比较器必须返回数值（JS sort 的比较器语义——布尔返回会导致排序不确定）。
  const normalized = normalizedSet(items, (a, b) => byteTable[a] - byteTable[b]);
  return Buffer.concat([u32(normalized.length), ...normalized.map((x) => u8(byteTable[x]))]);
}

/** 对象身份集合编码：bytes 字节序升序去重。 */
function encIdSet(hexList) {
  const normalized = normalizedSet(hexList, (a, b) => a < b);
  return Buffer.concat([u32(normalized.length), ...normalized.map(id128)]);
}

/** full 形态的原始阈值槽位（原始值位模式＋显示单位——POL-ID-3 承载面）。 */
function encOptionalRawThreshold(t) {
  if (t === null) return presence(false);
  return Buffer.concat([presence(true), f64(t.value), lpText(t.unit)]);
}

/** 语义投影的阈值槽位（presence＋SI 真值——无单位/来源/域槽位）。 */
function encOptionalSemanticThreshold(t) {
  if (t === null) return presence(false);
  return Buffer.concat([presence(true), f64(toSi(t.value, t.unit))]);
}

/** 帧头组装（magic 7B＋形态 1B＋schema 4B＋载荷长度 4B）。 */
function buildFrame(form, schemaVersion, payload) {
  return Buffer.concat([
    Buffer.from("IRDPOL1", "ascii"),
    u8(form),
    u32(schemaVersion),
    u32(payload.length),
    payload,
  ]);
}

// ---------------------------------------------------------------------------
// 夹具语义模型（单一事实——inputs/policy-input.json 与
// inputs/policy-input-variant.json 是它的两种承载序/显示单位表述；
// 本脚本按规范语义直接编码，两变体都必须坍缩到同这些字节）。
// ---------------------------------------------------------------------------

const FIXTURE = {
  schemaVersion: 1,
  policyObjectHex: "0102030405060708090a0b0c0d0e0f10",
  collision: {
    enabled: true,
    enabledDomains: ["Tool", "Environment", "Self"], // 承载序乱序——编码前规范化
    safetyClearance: { value: 0.01, unit: "m" }, // 规范语义＝SI 0.01 m
    excludeAdjacentLinksByDefault: true,
    mandatoryPairs: [
      {
        first: { kind: "Object", objectHex: "11010101010101010101010101010101" },
        second: { kind: "Object", objectHex: "22020202020202020202020202020202" },
        level: "Must",
        reason: "机械臂末端接近料架通道必检（POL-T11 契约夹具）",
      },
      {
        first: { kind: "Role", roleToken: "Tool" },
        second: { kind: "Role", roleToken: "EnvironmentObject" },
        level: "Should",
        reason: "工具与环境对象默认间距巡检",
      },
    ],
    excludedPairs: [
      {
        first: { kind: "Group", groupName: "static-jigs" },
        second: { kind: "Role", roleToken: "EnvironmentObject" },
        level: "Should",
        reason: "静态工装与环境对象互滤（显式组展开面）",
      },
    ],
  },
  jointThresholds: {
    nearLimitRatio: { value: 0.05, unit: "" },
    conditionNumberWarning: { value: 10.0, unit: "" },
    finiteRotationTravelLimit: { value: 12.566370614359172, unit: "rad" }, // 4π 字面量
    travelLimitCheckEnabled: true,
  },
  applicability: {
    modes: ["Verified", "Quick"], // 承载序乱序——编码前规范化
    modelObjects: ["33030303030303030303030303030303"],
    taskObjects: [],
    caseObjects: [],
  },
  numericContractAnchor: "appendixD@v1.16",
  origin: { kind: "UserEdited", sourceObjectHex: null, note: "契约夹具基线策略（POL-T11）" },
  compatibilityNotes: "夹具首版——不承载迁移语义",
};

// ---------------------------------------------------------------------------
// 两形态编码器（§5.3 载荷布局——字段规范序）。
// ---------------------------------------------------------------------------

/** full 形态（0x00）：对象字节——语义闭包原始形态＋管理审计字段。 */
function encodeFull(m) {
  const origin = m.origin;
  const payload = Buffer.concat([
    id128(m.policyObjectHex), // 身份块（入字节、不入身份——CR-02 排除字段）
    u8(ORIGIN_KIND_BYTE[origin.kind]),
    origin.sourceObjectHex ? Buffer.concat([presence(true), id128(origin.sourceObjectHex)]) : presence(false),
    origin.note ? Buffer.concat([presence(true), lpText(origin.note)]) : presence(false),
    m.compatibilityNotes ? Buffer.concat([presence(true), lpText(m.compatibilityNotes)]) : presence(false),
    lpText(m.numericContractAnchor),
    // 碰撞规则原始输入。
    u8(m.collision.enabled ? 1 : 0),
    encEnumSet(m.collision.enabledDomains, DOMAIN_BYTE),
    encOptionalRawThreshold(m.collision.safetyClearance),
    u8(m.collision.excludeAdjacentLinksByDefault ? 1 : 0),
    encPairRuleList(m.collision.mandatoryPairs),
    encPairRuleList(m.collision.excludedPairs),
    // 关节限位/行程阈值原始输入（槽位显式 presence）。
    encOptionalRawThreshold(m.jointThresholds.nearLimitRatio),
    encOptionalRawThreshold(m.jointThresholds.conditionNumberWarning),
    encOptionalRawThreshold(m.jointThresholds.finiteRotationTravelLimit),
    u8(m.jointThresholds.travelLimitCheckEnabled ? 1 : 0),
    // 适用范围（集合规范化）。
    encEnumSet(m.applicability.modes, MODE_BYTE),
    encIdSet(m.applicability.modelObjects),
    encIdSet(m.applicability.taskObjects),
    encIdSet(m.applicability.caseObjects),
  ]);
  return buildFrame(0x00, m.schemaVersion, payload);
}

/**
 * 语义闭包投影（0x01）：仅语义闭包字段的归一形态（SI 真值；无显示单位/
 * 管理审计字段）——contentIdentity 的唯一摘要输入（CR-02）。
 */
function encodeSemanticProjection(m) {
  const c = m.collision;
  const j = m.jointThresholds;
  const payload = Buffer.concat([
    u8(c.enabled ? 1 : 0),
    encEnumSet(c.enabledDomains, DOMAIN_BYTE),
    encOptionalSemanticThreshold(c.safetyClearance),
    u8(c.excludeAdjacentLinksByDefault ? 1 : 0),
    encPairRuleList(c.mandatoryPairs), // level/reason 入身份（§4.3）
    encPairRuleList(c.excludedPairs),
    encOptionalSemanticThreshold(j.nearLimitRatio),
    encOptionalSemanticThreshold(j.conditionNumberWarning),
    f64(toSi(j.finiteRotationTravelLimit.value, j.finiteRotationTravelLimit.unit)), // 必有值
    u8(j.travelLimitCheckEnabled ? 1 : 0),
    encEnumSet(m.applicability.modes, MODE_BYTE),
    encIdSet(m.applicability.modelObjects),
    encIdSet(m.applicability.taskObjects),
    encIdSet(m.applicability.caseObjects),
    lpText(m.numericContractAnchor),
  ]);
  return buildFrame(0x01, m.schemaVersion, payload);
}

// ---------------------------------------------------------------------------
// 产出与自检。
// ---------------------------------------------------------------------------

const full = encodeFull(FIXTURE);
const projection = encodeSemanticProjection(FIXTURE);
const identityHex = createHash("sha256").update(projection).digest("hex");

const expected = {
  note:
    "由 generate/make_pol_policy_fixture.mjs 独立重实现 §5.3 编码规则产出（node:crypto " +
    "SHA-256——与 core ContentDigester 同算法标准实现）；本文件不是产品代码产物。" +
    "消费测试（PolicyCodecFixtureContractTest）将产品 PolicyCodec 的输出与本文件逐字节/逐串对照" +
    "——双实现互证，任一侧漂移即失败。",
  semanticModel: {
    policyObjectHex: FIXTURE.policyObjectHex,
    safetyClearance: "0.01 m（variant 以 10 mm 表述同值——SI 归一 (v·factor)/factor 逐位口径）",
    travelLimit: "12.566370614359172 rad（附录 D 第 11 项 4π 字面量）",
    rules: "mandatory 2＋excluded 1（Object/Role/Group 三类目标全覆盖——规范化排序后入身份）",
  },
  fullEncodingHex: full.toString("hex"),
  semanticProjectionHex: projection.toString("hex"),
  contentIdentityHex: identityHex,
  siValues: {
    safetyClearanceSi: toSi(FIXTURE.collision.safetyClearance.value, FIXTURE.collision.safetyClearance.unit),
    nearLimitRatioSi: toSi(FIXTURE.jointThresholds.nearLimitRatio.value, FIXTURE.jointThresholds.nearLimitRatio.unit),
    conditionNumberWarningSi: toSi(
      FIXTURE.jointThresholds.conditionNumberWarning.value,
      FIXTURE.jointThresholds.conditionNumberWarning.unit,
    ),
    travelLimitSi: toSi(
      FIXTURE.jointThresholds.finiteRotationTravelLimit.value,
      FIXTURE.jointThresholds.finiteRotationTravelLimit.unit,
    ),
  },
};

// 自检 1：身份＝SHA-256(投影字节)（CR-02"身份与投影不可独立漂移"的自证）。
if (createHash("sha256").update(Buffer.from(expected.semanticProjectionHex, "hex")).digest("hex")
  !== expected.contentIdentityHex) {
  throw new Error("自检失败：contentIdentity ≠ SHA-256(semanticProjection)");
}

// 自检 2：帧几何（magic/形态/载荷长度声明与实际一致）。
for (const [name, bytes, form] of [
  ["full", full, 0x00],
  ["projection", projection, 0x01],
]) {
  if (bytes.subarray(0, 7).toString("ascii") !== "IRDPOL1") throw new Error(`${name}: magic 不符`);
  if (bytes[7] !== form) throw new Error(`${name}: 形态字节不符`);
  if (bytes.readUInt32BE(12) !== bytes.length - 16) throw new Error(`${name}: 载荷长度声明不符`);
}

const outPath = join(versionDir, "expected", "identity.json");
writeFileSync(outPath, JSON.stringify(expected, null, 2) + "\n", "utf8");

// 复核输入文件与语义模型的一致性面（提示性——权威对照在 C++ 消费测试）。
for (const rel of ["inputs/policy-input.json", "inputs/policy-input-variant.json"]) {
  try {
    readFileSync(join(versionDir, rel), "utf8");
  } catch {
    console.warn(`提醒：${rel} 尚未就位（消费测试将以本脚本产出的期望为准）`);
  }
}

// integrity 申报表（人工誊入 manifest.json——TK-T03 先例同款流程）。
console.log("== integrity 申报表（path / sizeBytes / sha256） ==");
for (const rel of [
  "inputs/policy-input.json",
  "inputs/policy-input-variant.json",
  "expected/identity.json",
  "generate/make_pol_policy_fixture.mjs",
]) {
  const data = readFileSync(join(versionDir, rel));
  console.log(
    `  { "path": "${rel}", "sha256": "${createHash("sha256").update(data).digest("hex")}", ` +
      `"sizeBytes": ${data.length} },`,
  );
}
console.log(`expected/identity.json 已写出：${outPath}`);
console.log(`fullEncoding 字节数=${full.length}，projection 字节数=${projection.length}`);
console.log(`contentIdentityHex=${identityHex}`);
