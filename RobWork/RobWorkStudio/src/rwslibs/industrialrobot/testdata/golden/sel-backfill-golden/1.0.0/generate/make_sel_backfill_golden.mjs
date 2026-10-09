/**
 * make_sel_backfill_golden.mjs —— selection 器件回填黄金数据集的独立参考实现
 * （WP-19-T11，AT-30"应用选型→更新传动配置→壳体/转子惯量回填不重复计入→
 * 新修订提示复算；复核前不沿用通过结论"）。
 *
 * 数据集：sel-backfill-golden（kind=analytic-case）。本脚本按 units/selection.md
 * §12.4 冻结的物性合成五步规则**标量直算**黄金合成值：
 *   第 1 步 输入卫生（质量/转子惯量 >0、全量有限）；
 *   第 2 步 各部件惯量向连杆系原点平行轴迁移 I_i^O＝I_i＋m_i·(|c_i|²E₃−c_i·c_iᵀ)；
 *   第 3 步 求和（M＝Σm；c＝Σ(m_i·c_i)/M 质量加权；I^O＝ΣI_i^O——合成项＝
 *           连杆原值＋电机壳体＋减速器壳体；**转子惯量不进任何合成分量**）；
 *   第 4 步 向合成质心回迁 I_syn＝I^O−M·(|c|²E₃−c·cᵀ)；
 *   第 5 步 MDL-06 断言①～③（M>0、SPD、三角不等式——本数据源正值合成全过）。
 * 与产品实现（selection/src/Backfill.cpp synthesizeAxisBodyProperties）零共享代码；
 * 双实现任一漂移即在容差对照用例显性失败（档案 sel-golden——附录 D 第 9 项）。
 *
 * 算例面：
 *   - assembled-j5：单轴回填（AT-30 主链路黄金——合成值逐分量＋转子独立登记
 *     ＋复算提示四域全量且不沿用原结论）；
 *   - missing-supplement：电机壳体惯量补充缺席（P-SEL-7 数据面缺口——组装
 *     整体失败 DataInsufficient，不产出半成品请求）；
 *   - mount-mismatch：安装关系与兼容表不一致（SEL-BACKFILL-MOUNT-MISMATCH
 *     语义面——组装拒绝）；
 *   - multi-axis-atomic：双轴整体原子（第二轴壳体惯量缺失 → 整体失败——
 *     §12.2 纪律 3"一个命令＝恰一新修订"的组装侧证明）。
 *
 * 用法：node make_sel_backfill_golden.mjs（在本目录执行）。
 */

import { writeFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const here = dirname(fileURLToPath(import.meta.url));
const inputsPath = join(here, "..", "inputs", "backfill-inputs.json");
const expectedPath = join(here, "..", "expected", "backfill-expected.json");

// =====================================================================
// 黄金目录引用（与 sel-catalog 同源器件——回填取值字段：电机 mass/rotor、
// 减速器 mass/housing、兼容对）
// =====================================================================

const identity = { catalogId: "cat-sel-golden", version: "1.0.0",
                   source: "黄金器件目录（selection 契约测试同源数据——WP-19-T11）" };
const motor101 = { modelId: "M-101", mass: 5.5, rotorInertia: 0.012 };
const gearbox201 = { modelId: "G-201", mass: 6.5, housingInertia: 0.012 };
const mountKind = "flange-mount";
// 目录锁定引用（固定 canonical 值——记录面追溯字段；contentVersion 为数值版本）。
const lockObject = "obj-00000000000000000000000000000b01";
const lockVersion = 7;

const axes = {
  j5: "obj-00000000000000000000000000000505",
  j6: "obj-00000000000000000000000000000606",
};

// =====================================================================
// 组装源（§12.4 合成输入——连杆原值＋锚点；全部 SI、连杆坐标系）
// =====================================================================

const ZERO_INERTIA = { ixx: 0, iyy: 0, izz: 0, ixy: 0, ixz: 0, iyz: 0 };

const j5Source = {
  axisRef: "j5", motorModelId: "M-101", gearboxModelId: "G-201", mountKind,
  linkMassKg: 12.0,
  linkComM: { x: 0.05, y: -0.02, z: 0.30 },
  linkInertia: { ixx: 0.35, iyy: 0.40, izz: 0.20, ixy: 0.01, ixz: -0.02, iyz: 0.015 },
  motorComAnchorM: { x: 0.0, y: 0.06, z: 0.12 },
  motorHousingInertiaSupplement: { ixx: 0.045, iyy: 0.050, izz: 0.030, ixy: 0, ixz: 0, iyz: 0 },
  gearboxComAnchorM: { x: 0.0, y: 0.0, z: 0.05 },
  appliedRatio: 0.02,   // c＝1/n（与组合校核联动口径同源——50:1 → 0.02）
};

// =====================================================================
// §12.4 五步合成（独立参考实现——标量直算）
// =====================================================================

function parallelAxis(m, d) {
  const d2 = d.x * d.x + d.y * d.y + d.z * d.z;
  return {
    ixx: m * (d2 - d.x * d.x), iyy: m * (d2 - d.y * d.y), izz: m * (d2 - d.z * d.z),
    ixy: -m * d.x * d.y, ixz: -m * d.x * d.z, iyz: -m * d.y * d.z,
  };
}
function addI(a, b) {
  return { ixx: a.ixx + b.ixx, iyy: a.iyy + b.iyy, izz: a.izz + b.izz,
           ixy: a.ixy + b.ixy, ixz: a.ixz + b.ixz, iyz: a.iyz + b.iyz };
}
function subI(a, b) {
  return { ixx: a.ixx - b.ixx, iyy: a.iyy - b.iyy, izz: a.izz - b.izz,
           ixy: a.ixy - b.ixy, ixz: a.ixz - b.ixz, iyz: a.iyz - b.iyz };
}

/** 五步合成（ok=false 时携带 failure 分类词面——与 Backfill.hpp 枚举对位）。 */
function synthesize(src) {
  // 第 1 步：输入卫生（黄金源全部合法——负值/非有限拒绝面由产品单测承载）。
  const motorHousingMass = src.motorModelId === "M-101" ? motor101.mass : NaN;
  const gearboxHousingMass = gearbox201.mass;
  const rotor = motor101.rotorInertia;
  // 第 2 步：平行轴迁移（电机壳体惯量＝组装源补充值——P-SEL-7 数据面；
  //          减速器壳体惯量＝目录值对角 0.012）。
  const motorInertia = src.motorHousingInertiaSupplement ?? null;
  if (motorInertia == null) return { ok: false, failure: "DataInsufficient" };
  const gbInertia = { ...ZERO_INERTIA,
                      ixx: gearbox201.housingInertia, iyy: gearbox201.housingInertia,
                      izz: gearbox201.housingInertia };
  const linkShifted = addI(src.linkInertia, parallelAxis(src.linkMassKg, src.linkComM));
  const motorShifted = addI(motorInertia, parallelAxis(motorHousingMass, src.motorComAnchorM));
  const gbShifted = addI(gbInertia, parallelAxis(gearboxHousingMass, src.gearboxComAnchorM));
  // 第 3 步：求和（质量/质心/原点系惯量——转子不进任何分量）。
  const totalMass = src.linkMassKg + motorHousingMass + gearboxHousingMass;
  const com = {
    x: (src.linkMassKg * src.linkComM.x + motorHousingMass * src.motorComAnchorM.x
        + gearboxHousingMass * src.gearboxComAnchorM.x) / totalMass,
    y: (src.linkMassKg * src.linkComM.y + motorHousingMass * src.motorComAnchorM.y
        + gearboxHousingMass * src.gearboxComAnchorM.y) / totalMass,
    z: (src.linkMassKg * src.linkComM.z + motorHousingMass * src.motorComAnchorM.z
        + gearboxHousingMass * src.gearboxComAnchorM.z) / totalMass,
  };
  const originInertia = addI(addI(linkShifted, motorShifted), gbShifted);
  // 第 4 步：向合成质心回迁。
  const inertia = subI(originInertia, parallelAxis(totalMass, com));
  // 第 5 步：MDL-06 断言①M>0（黄金源恒过；②SPD/③三角由正值合成保证——
  //          断言失败的黄金面由产品单测承载，此处 ok 面只登记正值结果）。
  return {
    ok: true,
    synthesis: {
      massKg: totalMass, comM: com, inertia,
      rotorInertiaKgM2: rotor,   // 独立登记（不重复计入的记录面证明）
    },
  };
}

/** 手算锚点（第三路）：质心 z 分量按分数算术独立复核。 */
const anchorComZ = (12.0 * 0.30 + 5.5 * 0.12 + 6.5 * 0.05) / 24.0;

// =====================================================================
// 算例面（组装成功/三类拒绝——kind 词面与 BackfillAssemblyOutcome 枚举对位）
// =====================================================================

const assembled = synthesize(j5Source);
if (!assembled.ok) throw new Error("assembled 算例必须成功——数据源缺陷");

const missingSupplement = {
  axisRef: "j5", motorModelId: "M-101", gearboxModelId: "G-201", mountKind,
  linkMassKg: 12.0, linkComM: j5Source.linkComM, linkInertia: j5Source.linkInertia,
  motorComAnchorM: j5Source.motorComAnchorM,
  motorHousingInertiaSupplement: null,          // 补充缺席 → 组装 DataInsufficient
  gearboxComAnchorM: j5Source.gearboxComAnchorM, appliedRatio: 0.02,
};

const mountMismatch = {
  ...j5Source, mountKind: "shaft-mount",        // 兼容表无该 (M-101,G-201,shaft-mount) 记录
};

const multiAxisAtomic = {
  axes: [
    j5Source,
    { axisRef: "j6", motorModelId: "M-101", gearboxModelId: "G-201", mountKind,
      linkMassKg: 8.0, linkComM: { x: 0.02, y: 0.0, z: 0.20 },
      linkInertia: { ixx: 0.20, iyy: 0.22, izz: 0.12, ixy: 0, ixz: 0, iyz: 0 },
      motorComAnchorM: { x: 0.0, y: 0.05, z: 0.10 },
      motorHousingInertiaSupplement: null,      // 第二轴缺失 → 整体失败（多轴原子）
      gearboxComAnchorM: { x: 0.0, y: 0.0, z: 0.04 }, appliedRatio: 0.02 },
  ],
};

// =====================================================================
// 写盘：inputs（组装源＋目录引用）＋expected（合成黄金＋拒绝分类＋复算提示）
// =====================================================================

const inputs = {
  schema: "sel-backfill-inputs/1",
  note: "回填黄金输入——黄金目录引用（电机 mass/rotor、减速器 mass/housing、兼容对、锁定引用）＋逐算例组装源。全部几何量在轴连杆坐标系下表示（kBackfillFrameLink=link-frame）、全部 SI（kg/m/kg·m²）；appliedRatio 无量纲 c＝1/n。",
  identity, axes, lockObject, lockVersion,
  motor101, gearbox201, mountKind,
  cases: { assembled: j5Source, missingSupplement, mountMismatch, multiAxisAtomic },
  recalcNotice: { domains: { kinematics: true, dynamics: true, selection: true,
                             optimization: true },
                  retainPriorConclusion: false },
  analyticAnchors: { comZ: anchorComZ, mass: 24.0 },
};
writeFileSync(inputsPath, JSON.stringify(inputs, null, 2) + "\n", "utf8");

const expected = {
  schema: "sel-backfill-expected/1",
  note: "回填黄金——由 generate/make_sel_backfill_golden.mjs 按 §12.4 五步规则标量直算（合成项＝连杆＋电机壳体＋减速器壳体；转子独立登记不重复计入；复算提示＝四域全量＋复核前不沿用原通过结论）。拒绝算例登记组装分类 kind 词面（Assembled/DataInsufficient/MountIncompatible/InvalidInput）。",
  assembled: {
    axisRef: "j5",
    kind: "Assembled",
    synthesis: assembled.synthesis,
    appliedRatio: 0.02,
    mountKind,
    catalogRef: { catalogId: identity.catalogId, catalogVersion: identity.version,
                  motorModelId: "M-101", gearboxModelId: "G-201" },
  },
  rejections: [
    { id: "missing-supplement", kind: "DataInsufficient",
      note: "电机壳体惯量补充缺席（P-SEL-7 数据面缺口——§12.4 缺失处理整体失败）" },
    { id: "mount-mismatch", kind: "MountIncompatible",
      note: "安装关系 shaft-mount 不在兼容表（SEL-BACKFILL-MOUNT-MISMATCH 语义面）" },
    { id: "multi-axis-atomic", kind: "DataInsufficient",
      note: "双轴整体原子：第二轴壳体惯量缺失 → 整体失败零产出（§12.2 纪律 3）" },
  ],
};
writeFileSync(expectedPath, JSON.stringify(expected, null, 2) + "\n", "utf8");

console.log("sel-backfill-golden: assembled mass =", assembled.synthesis.massKg,
            " com =", JSON.stringify(assembled.synthesis.comM));
console.log("  inertia =", JSON.stringify(assembled.synthesis.inertia));
console.log("  anchor comZ =", anchorComZ);
