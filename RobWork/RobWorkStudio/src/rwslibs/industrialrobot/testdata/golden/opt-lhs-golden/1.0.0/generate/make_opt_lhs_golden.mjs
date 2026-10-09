/**
 * make_opt_lhs_golden.mjs —— optimization 确定性候选生成（seeded-lhs）黄金
 * 数据集的独立参考实现（WP-20-T11，OPT-01/02/06——"确定性种子拉丁超立方
 * 采样"的同种子确定性黄金面，AT-09）。
 *
 * 数据集：opt-lhs-golden（kind=analytic-case）。本脚本按 units/optimization.md
 * §8.2 冻结语义**逐字节独立复算**候选生成批：
 *   - 自持 splitmix64 PRNG（黄金常数 0x9E3779B97F4A7C15 / 0xBF58476D1CE4E5B9 /
 *     0x94D049BB133111EB——公开规范常数，无工程自由度；JS BigInt 精确 64 位
 *     无符号环绕运算，与 C++ std::uint64_t 语义一致）；
 *   - [0,1) 均匀数＝64 位输出右移 11 位（53 位尾数无损）乘 2⁻⁵³（精确二次幂
 *     ——双精度无舍入）；
 *   - 每维度独立 n 层层序置换（Fisher-Yates 自后向前：j＝nextU64() % (i+1)，
 *     i 从 n−1 到 1——消费顺序与 §8.2 算法第 5 步逐字一致）；
 *   - 候选主序取值（第 i 候选按维度字典序逐维消费一个均匀数）：枚举＝
 *     floor(|值域|·u) 钳末位；连续＝层内均匀点 lower＋(layer＋u)/n×(upper−
 *     lower)（IEEE 754 双精度同序运算——与产品实现位模式一致）；量化＝采样
 *     域收缩 [lower＋step, upper−step] 后同连续公式，窄域退化为默认值；
 *   - 量化对齐 round-half-even（k＝value/step 取最近整数格、恰半格舍入到偶
 *     格——§8.2 变量编码的确定性声明）。
 *
 * 身份链（§4.2/§5.5 冻结公式——与产品实现零共享代码，node crypto 独立计算）：
 *   补丁 canonical 字节＝magic "IRDOPTP1"＋u32 codecVersion 1＋u32 项数＋每项
 *   （u16 长度＋bindingId UTF-8＋值形态 tag＋载荷）按 bindingId 字典序；
 *   CandidatePatchId＝SHA-256(canonical 字节)；（本数据集只到补丁身份——
 *   候选身份链在 opt-pareto-golden 承载，避免两数据集重复同一段基线身份）。
 *
 * 用法：node make_opt_lhs_golden.mjs（在本目录执行；写出 inputs/ 与 expected/
 * 各一文件——同环境重跑逐字节一致，完整性 SHA-256 随 manifest 登记）。
 */

import { writeFileSync } from "node:fs";
import { createHash } from "node:crypto";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const here = dirname(fileURLToPath(import.meta.url));
const inputsPath = join(here, "..", "inputs", "lhs-inputs.json");
const expectedPath = join(here, "..", "expected", "lhs-expected.json");

// =====================================================================
// 研究定义（黄金唯一数据源——四变量三类别：连续×2＋量化×1＋枚举×1）
// =====================================================================
// 绑定 token 全部为 units/optimization.md §5.3 R1 词表条目的实例化形态
// （C++ 侧 matchDefinition 按 [i]/[j] 占位匹配——golden 值须落在词表内）。
// 材料枚举值域＝modeling PropertyEstimation 冻结材料键集（§5.3 #1 行）。

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
    diagSubject: "obj-000000000000000000000000000000a2",
  },
  {
    // 量化使用形态：§5.3 词表 Continuous 条目允许绑定声明 Quantized
    // （step>0 网格化——T03 实现口径④）。采样域收缩后含 0 网格点——
    // 黄金面承载零值/近零/正负三边角（附录 D C4）。
    bindingId: "mdl.joint[3].dh.d",
    kind: "quantized",
    unit: "m",
    lowerBound: -0.1,
    upperBound: 0.1,
    step: 0.05,
    enumValues: [],
    defaultValue: 0.0,
    defaultValueIndex: 0,
    locked: false,
    authorized: true,
    authorityFieldPath: "robot-design/joints[3]/dh/d",
    diagSubject: "obj-000000000000000000000000000000a3",
  },
  {
    bindingId: "mdl.link[4].material",
    kind: "enumeration",
    unit: "",
    lowerBound: 0.0,
    upperBound: 0.0,
    step: 0.0,
    enumValues: [
      "steel",
      "aluminum",
      "cast-iron",
      "titanium-alloy",
      "engineering-plastic",
    ],
    defaultValue: 0.0,
    defaultValueIndex: 1,
    locked: false,
    authorized: true,
    authorityFieldPath: "robot-design/links[4]/material",
    diagSubject: "obj-000000000000000000000000000000a4",
  },
  {
    bindingId: "mdl.link[4].section.size",
    kind: "continuous",
    unit: "m",
    lowerBound: 0.02,
    upperBound: 0.3,
    step: 0.0,
    enumValues: [],
    defaultValue: 0.1,
    defaultValueIndex: 0,
    locked: false,
    authorized: true,
    authorityFieldPath: "robot-design/links[4]/section/size",
    diagSubject: "obj-000000000000000000000000000000a4",
  },
];

const config = {
  schemaVersion: 1,
  stage: "stage-b",
  seed: 424242, // ≥1（I-OPT-2；0 非法不静默替换）
  strategyId: "opt.strategy.seeded-lhs",
  budget: { maxCandidates: 13, maxVerifiedCandidates: 5, maxGenerations: 1 },
};

// =====================================================================
// 自持 splitmix64 与黄金算法（§8.2 冻结语义的独立复算——见文件头）
// =====================================================================

const MASK64 = (1n << 64n) - 1n;

/** splitmix64 单步：状态推进＋三轮黄金常数混合（返回 [输出, 新状态]）。 */
function splitmixNext(state) {
  const s = (state + 0x9e3779b97f4a7c15n) & MASK64;
  let z = s;
  z = ((z ^ (z >> 30n)) * 0xbf58476d1ce4e5b9n) & MASK64;
  z = ((z ^ (z >> 27n)) * 0x94d049bb133111ebn) & MASK64;
  z = (z ^ (z >> 31n)) & MASK64;
  return [z, s];
}

/** [0,1) 均匀双精度：64 位输出右移 11 位乘 2⁻⁵³（两步均为精确缩放）。 */
function unitFrom(u64) {
  return Number(u64 >> 11n) * (1.0 / 9007199254740992.0);
}

/** 量化对齐 round-half-even（§8.2——k＝value/step 最近整数格，半格取偶）。 */
function quantizeHalfEven(value, step) {
  const k = value / step;
  const floorK = Math.floor(k);
  const frac = k - floorK;
  if (frac > 0.5) return (floorK + 1.0) * step;
  if (frac < 0.5) return floorK * step;
  // 恰半格：floorK 偶数留本格，奇数进上格（fmod==0 涵盖 ±0.0——与 C++ 一致）。
  const even = ((floorK % 2.0) + 2.0) % 2.0 === 0.0;
  return (even ? floorK : floorK + 1.0) * step;
}

// ---- 维度集：未锁定且授权的连续/量化/枚举绑定，bindingId 字典序（§8.2 第 2 步）。

function samplingDims(vars) {
  return vars
    .filter(
      (b) =>
        !b.locked &&
        b.authorized &&
        (b.kind === "continuous" || b.kind === "quantized" || b.kind === "enumeration"),
    )
    .sort((a, b) => (a.bindingId < b.bindingId ? -1 : a.bindingId > b.bindingId ? 1 : 0));
}

// =====================================================================
// 候选生成（§8.2 第 3~8 步——黄金主算面）
// =====================================================================

function generateBatch(cfg, vars) {
  const dims = samplingDims(vars);
  const n = cfg.budget.maxCandidates - 1;
  const batch = [];
  // 第 8 步前置：基线候选恒为批首（空补丁——§8.2/§5.5③）。
  batch.push({ label: "baseline", items: [] });
  if (n === 0) return { dims, batch, layerTable: [] };

  let state = BigInt(cfg.seed);
  const nextU64 = () => {
    const [v, s] = splitmixNext(state);
    state = s;
    return v;
  };

  // 第 5 步：每维度独立 n 层层序置换（Fisher-Yates 自后向前——j＝u64 % (i+1)）。
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

  // 第 6~7 步：候选主序逐维取值＋量化对齐（makeCandidatePatch 的构造规范
  // ——排序在 canonicalize 复算，此处 items 已按维度字典序产出）。
  for (let i = 0; i < n; ++i) {
    const items = [];
    for (let d = 0; d < dims.length; ++d) {
      const b = dims[d];
      const layer = layerTable[d].layers[i];
      const u = unitFrom(nextU64());
      if (b.kind === "enumeration") {
        // 枚举：floor(值域·u) 均匀取下标，钳末位（u<1 时不可达——防御一致）。
        let idx = Math.floor(b.enumValues.length * u);
        if (idx >= b.enumValues.length) idx = b.enumValues.length - 1;
        items.push({ bindingId: b.bindingId, kind: "enumeration", enumIndex: idx });
      } else if (b.kind === "quantized") {
        const lo = b.lowerBound + b.step;
        const hi = b.upperBound - b.step;
        const raw = lo <= hi ? lo + ((layer + u) / n) * (hi - lo) : b.defaultValue;
        const aligned = quantizeHalfEven(raw, b.step);
        items.push({
          bindingId: b.bindingId,
          kind: "quantized",
          scalarValue: aligned,
          rawSample: raw,
        });
      } else {
        const v = b.lowerBound + ((layer + u) / n) * (b.upperBound - b.lowerBound);
        items.push({ bindingId: b.bindingId, kind: "continuous", scalarValue: v });
      }
    }
    // 补丁项按 bindingId 字典序存储（与维度序一致——维度已按 bindingId 排序）。
    batch.push({ label: "cand-" + String(i), items });
  }
  return { dims, batch, layerTable };
}

// =====================================================================
// 补丁 canonical 字节与身份（§5.5 编码——node Buffer 逐字节独立实现）
// =====================================================================

function patchCanonicalBytes(items) {
  const sorted = [...items].sort((a, b) =>
    a.bindingId < b.bindingId ? -1 : a.bindingId > b.bindingId ? 1 : 0,
  );
  const parts = [];
  parts.push(Buffer.from("IRDOPTP1", "utf8")); // [0..7] magic
  const head = Buffer.alloc(8);
  head.writeUInt32LE(1, 0); // [8..11] codecVersion 1
  head.writeUInt32LE(sorted.length, 4); // [12..15] itemCount
  parts.push(head);
  for (const it of sorted) {
    const idBuf = Buffer.from(it.bindingId, "utf8");
    const len = Buffer.alloc(2);
    len.writeUInt16LE(idBuf.length, 0);
    parts.push(len, idBuf);
    // 值形态通道按**内容**选择（产品 canonicalize 的冻结实现语义——补丁项
    // 自持三成员：离散引用非空→'D'；枚举下标非零→'E'；其余→'S'。枚举下标
    // 0 的项与标量 0.0 在表达层同形，编码按 Scalar 通道承载——确定性且无损）。
    const discreteRef = it.discreteRef != null ? it.discreteRef : "";
    if (discreteRef !== "") {
      parts.push(Buffer.from([0x44])); // 'D'
      const refBuf = Buffer.from(discreteRef, "utf8");
      const refLen = Buffer.alloc(2);
      refLen.writeUInt16LE(refBuf.length, 0);
      parts.push(refLen, refBuf);
    } else if (it.kind === "enumeration" && it.enumIndex !== 0) {
      parts.push(Buffer.from([0x45])); // 'E'
      const v = Buffer.alloc(4);
      v.writeUInt32LE(it.enumIndex, 0);
      parts.push(v);
    } else {
      parts.push(Buffer.from([0x53])); // 'S'
      const v = Buffer.alloc(8);
      // -0.0 规范化为 +0.0（§5.5 构造规范化——位模式确定性）；枚举 0 项
      // 的标量通道载荷恒 0.0（非当前形态成员清零语义）。
      const raw = it.kind === "enumeration" ? 0.0 : it.scalarValue;
      v.writeDoubleLE(Object.is(raw, -0.0) ? 0.0 : raw, 0);
      parts.push(v);
    }
  }
  return Buffer.concat(parts);
}

function sha256Hex(buf) {
  return createHash("sha256").update(buf).digest("hex");
}

// =====================================================================
// 生成黄金（inputs＋expected 同源单点；重跑逐字节一致）
// =====================================================================

const { dims, batch, layerTable } = generateBatch(config, variables);

// ---- 边角覆盖核查（analytic-case 三布尔的数据面依据——manifest edgeCases）：
//      零值＝量化对齐恰落 0 网格；近零＝|对齐值| ≤ 0.05；正负抵消＝同维度
//      对齐值同时出现正负两侧。核查失败直接抛错（数据集设计缺陷——不静默
//      出库，附录 D C4 三布尔必须真实成立）。
const qItems = batch.flatMap((c) => c.items).filter((it) => it.kind === "quantized");
const zeroHit = qItems.some((it) => it.scalarValue === 0.0);
const nearZeroHit = qItems.some((it) => Math.abs(it.scalarValue) <= 0.05);
const bothSigns =
  qItems.some((it) => it.scalarValue > 0.0) && qItems.some((it) => it.scalarValue < 0.0);
if (!zeroHit || !nearZeroHit || !bothSigns) {
  throw new Error(
    `量化维度边角覆盖不足（zero=${zeroHit} nearZero=${nearZeroHit} bothSigns=${bothSigns}）` +
      "——调整种子/值域设计（附录 D C4 三布尔须真实成立）",
  );
}

const expectedBatch = batch.map((c) => {
  const canonical = patchCanonicalBytes(c.items);
  return {
    label: c.label,
    isBaseline: c.items.length === 0,
    patchId: "cid-" + sha256Hex(canonical),
    items: c.items.map((it) =>
      it.kind === "enumeration"
        ? {
            bindingId: it.bindingId,
            kind: "enumeration",
            enumIndex: it.enumIndex,
            enumValue: variables.find((v) => v.bindingId === it.bindingId).enumValues[
              it.enumIndex
            ],
          }
        : { bindingId: it.bindingId, kind: it.kind, scalarValue: it.scalarValue },
    ),
  };
});

const inputs = {
  dataset: "opt-lhs-golden@1.0.0",
  contract: "units/optimization.md §8.2 seeded-lhs 同种子确定性黄金面（WP-20-T11，AT-09）",
  config,
  variables,
  dimensionOrder: dims.map((d) => d.bindingId),
};

const expected = {
  dataset: "opt-lhs-golden@1.0.0",
  contract:
    "§8.2 冻结算法独立复算批（batch 序＝生成序：基线恒批首＋cand-<i>）——patchId 为 §5.5 canonical 字节 SHA-256（node crypto 独立计算）",
  seed: config.seed,
  dimensionOrder: dims.map((d) => d.bindingId),
  layerTable,
  batch: expectedBatch,
  // 审计口径（§8.2——生成面计数；评估面计数归 opt-pareto-golden）。
  audit: {
    candidatesGenerated: batch.length,
    nonBaselineCount: batch.length - 1,
    samplingDimensionCount: dims.length,
  },
};

writeFileSync(inputsPath, JSON.stringify(inputs, null, 2) + "\n", "utf8");
writeFileSync(expectedPath, JSON.stringify(expected, null, 2) + "\n", "utf8");
console.log(
  `opt-lhs-golden: batch=${batch.length} dims=${dims.length} seed=${config.seed} -> inputs/expected 已写出`,
);
