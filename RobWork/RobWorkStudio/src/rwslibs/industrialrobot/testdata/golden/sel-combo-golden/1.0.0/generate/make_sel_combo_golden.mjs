/**
 * make_sel_combo_golden.mjs —— selection 组合校核黄金数据集的独立参考实现
 * （WP-19-T11，SEL-05"经共享映射校核电机—减速器—负载惯量、组合兼容和每轴
 * 工作点"——黄金联动面）。
 *
 * 数据集：sel-combo-golden（kind=analytic-case）。本脚本从内置唯一数据源
 * （黄金目录源——与 sel-catalog/sel-screening 同一器件数据）按 units/selection.md
 * §9.3/§9.4 冻结语义**逐组合逐工况复算**四个组合的校核判定：
 *   - K1＝双轴 M-101+G-201：全维通过（Feasible 黄金——totalMass/格 note 带
 *     惯量比未判定标注）；
 *   - K2＝双轴 M-101+G-202：映射 c=0.01 下电机峰值转速超限＋减速器输入转速
 *     超限（Rejected——逐轴格 Fail，定位面＝首条原因工作点）；
 *   - K3＝双轴 M-102+G-201：J3 轴电机额定转速 250 边界超限（275>250——格
 *     Fail），J4 轴恰达边界（250=250——"恰好满足"黄金面，格 Pass）；
 *   - K4＝J3 轴 M-102+G-202（兼容表无记录）＋J4 轴 M-101+G-201：组合兼容
 *     维度 ComboIncompatible（构造面过滤之外的双保险核对——校核直调面）。
 *
 * 黄金联动口径（P-SEL-1 提议契约 v1）：映射事实（电机侧六量）由本脚本按
 * drivetrain 卡 §5.3 归一化传动比口径 c＝Δq_joint/Δθ_motor＝1/n 独立换算
 * （τ_m＝c·τ_joint、ω_m＝ω_joint/c、P＝τ_m·ω_m 解析直算；ω_m_rms/惯量比/
 * 反射惯量给定值——J_rotor/c²），与产品实现（selection/src/CombinationCheck.cpp
 * 零映射公式＋drivetrain 映射核心）零共享代码；组合校核对映射事实只消费
 * 不重算（§9.1 纪律）——本数据集即其消费面的数值事实源。
 *
 * 判定复刻公式来源（§9.3 清单行序；阈值容差＝附录 D C7 同 screening）：
 *   ①组合目录一致性 ②组合兼容（无记录即不兼容） ③轴映射完整性 ④轴级能力
 *   判定（§7/§8 全维——复用 screening 同款公式） ⑤多工况格聚合（任一轴失败
 *   即格 Fail；定位＝首条失败记录首条原因的工作点） ⑥映射完整性 ⑦质量核算
 *   （Σ 各轴电机＋减速器质量） ⑧verdict 汇总（原因→缺口→可行）；
 *   惯量比维度：规则未配置（R1 默认）→ 显式"未判定"（Feasible 格 note 追加
 *   "inertia-ratio-policy-unsettled"——§11.3/O-11）。
 *
 * 用法：node make_sel_combo_golden.mjs（在本目录执行）。
 */

import { writeFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const here = dirname(fileURLToPath(import.meta.url));
const inputsPath = join(here, "..", "inputs", "combo-inputs.json");
const expectedPath = join(here, "..", "expected", "combo-expected.json");

// =====================================================================
// 黄金目录源（与 sel-screening-golden 同一器件数据——同源单点）
// =====================================================================

const identity = { catalogId: "cat-sel-golden", version: "1.0.0",
                   source: "黄金器件目录（selection 契约测试同源数据——WP-19-T11）" };

const motors = [
  { modelId: "M-101", ratedTorque: 8.0, peakTorque: 20.0, ratedSpeed: 150.0,
    maxSpeed: 300.0, ratedPower: 2200.0, mass: 5.5, rotorInertia: 0.012,
    powerCurveId: "c101-power" },
  { modelId: "M-102", ratedTorque: 3.0, peakTorque: 8.0, ratedSpeed: 150.0,
    maxSpeed: 250.0, ratedPower: 600.0, mass: 3.2, rotorInertia: 0.020,
    powerCurveId: null },
];
const gearboxes = [
  { modelId: "G-201", ratedOutputTorque: 120.0, peakOutputTorque: 240.0,
    maxInputSpeed: 300.0, ratio: 50.0, efficiency: 0.92, mass: 6.5 },
  { modelId: "G-202", ratedOutputTorque: 60.0, peakOutputTorque: 120.0,
    maxInputSpeed: 250.0, ratio: 100.0, efficiency: 0.80, mass: 3.0 },
];
const compatPairs = [
  { motorId: "M-101", gearboxId: "G-201" },
  { motorId: "M-101", gearboxId: "G-202" },
  { motorId: "M-101", gearboxId: "G-203" },
  { motorId: "M-102", gearboxId: "G-201" },
  { motorId: "M-103", gearboxId: "G-201" },
];
const powerCurve = { curveId: "c101-power", points: [ { x: 100, y: 800 },
  { x: 200, y: 1600 }, { x: 300, y: 2400 } ] };  // speed→power（M-101）

const axes = {
  j3: "obj-00000000000000000000000000000303",
  j4: "obj-00000000000000000000000000000404",
};

// =====================================================================
// 组合集与映射事实（c＝1/n 解析换算——黄金联动单点）
// =====================================================================

const cOf = { "G-201": 1 / 50, "G-202": 1 / 100 };   // c＝1/n（drivetrain §5.3 口径）

/** 关节侧工作点（DYN-03 口径——与组合无关）。 */
const jointFacts = {
  j3: { caseId: "case-A", jointTorqueRms: 40.0, jointTorquePeak: 100.0,
        jointSpeedPeak: 5.5, atTime: 1.5, segmentId: "seg-A" },
  j4: { caseId: "case-A", jointTorqueRms: 8.0, jointTorquePeak: 20.0,
        jointSpeedPeak: 5.0, atTime: 1.5, segmentId: "seg-A" },
};

/** 电机侧映射事实（组合口径解析换算：τ_m＝c·τ_j、ω_m＝ω_j/c、P＝τ_m·ω_m；
 *  ω_m_rms/惯量比/反射惯量给定——负载周期非满速的合理事实值）。 */
const OMEGA_RMS = { j3: 100.0, j4: 110.0 };        // ω_m_rms（rad/s；给定事实——j4 须 ≥ 曲线下界 100，否则 P_rms 插值拒绝成缺口）
const INERTIA_RATIO = { j3: 3.0, j4: 1.5 };          // 惯量比（无量纲；给定事实）
const PEAK_DURATION = { j3: 2.0, j4: 1.0 };          // 峰值段时长（s）

const combos = [
  { id: "K1", axes: [ { axis: "j3", motor: "M-101", gearbox: "G-201" },
                      { axis: "j4", motor: "M-101", gearbox: "G-201" } ] },
  { id: "K2", axes: [ { axis: "j3", motor: "M-101", gearbox: "G-202" },
                      { axis: "j4", motor: "M-101", gearbox: "G-202" } ] },
  { id: "K3", axes: [ { axis: "j3", motor: "M-102", gearbox: "G-201" },
                      { axis: "j4", motor: "M-102", gearbox: "G-201" } ] },
  { id: "K4", axes: [ { axis: "j3", motor: "M-102", gearbox: "G-202" },
                      { axis: "j4", motor: "M-101", gearbox: "G-201" } ] },
];

/** 逐组合逐轴映射事实（脚本解析换算——黄金联动的数值面）。 */
function mappingFactOf(combo, ax) {
  const jf = jointFacts[ax.axis];
  const c = cOf[ax.gearbox];
  const m = motors.find((x) => x.modelId === ax.motor);
  const tauPeak = jf.jointTorquePeak * c;
  const tauRms = jf.jointTorqueRms * c;
  const omegaPeak = jf.jointSpeedPeak / c;
  return {
    combinationId: combo.id, axisRef: ax.axis, caseId: jf.caseId,
    motorTorqueRms: tauRms, motorTorquePeak: tauPeak,
    motorSpeedPeak: omegaPeak, motorSpeedRms: OMEGA_RMS[ax.axis],
    motorPowerPeak: tauPeak * omegaPeak, motorPowerRms: tauRms * OMEGA_RMS[ax.axis],
    peakDuration: PEAK_DURATION[ax.axis],
    peakAtTime: jf.atTime, peakSegmentId: jf.segmentId,
    inertiaRatio: INERTIA_RATIO[ax.axis],
    reflectedInertia: m.rotorInertia / (c * c),   // J_rotor/c²（解析）
    efficiencyApplied: true,
  };
}

// =====================================================================
// 判定复刻引擎（§9.3 清单——与 screening 同款维度公式；此处聚焦组合级维度）
// =====================================================================

const C7 = { torque: 1e-9, angularVelocity: 1e-9, angle: 1e-12, dimensionless: 1e-12 };
const tol = (k) => C7[k] ?? 0.0;
const exceeds = (a, l, k) => a > l + tol(k);

// 枚举名→词表 kebab 文本（与产品 reasonTokenText 唯一映射点对齐——格 note
// 与原因呈现面词面契约；camelCase→kebab-case 机械转换）。
const kebab = (n) => n.replace(/([a-z0-9])([A-Z])/g, "$1-$2").toLowerCase();

const TOKEN = {
  TorqueContinuousInsufficient: 0, TorquePeakInsufficient: 1, SpeedInsufficient: 2,
  PowerInsufficient: 3, OverloadTimeInsufficient: 4, DutyMismatch: 5,
  VoltageMismatch: 6, ThermalDeratingInsufficient: 7, BrakeInsufficient: 8,
  HoldingInsufficient: 9, SafetyFactorInsufficient: 10,
  GearboxRatedTorqueInsufficient: 11, GearboxPeakTorqueInsufficient: 12,
  InputSpeedExceeded: 13, RatioMismatch: 14, EfficiencyInsufficient: 15,
  BacklashExceeded: 16, LifeInsufficient: 17, MountingIncompatible: 18,
  ExternalLoadExceeded: 19,
  ComboIncompatible: 20, AxisMappingIncomplete: 21,
};

function interp(curve, x) {
  if (x < curve.points[0].x || x > curve.points[curve.points.length - 1].x) return null;
  for (let i = 0; i + 1 < curve.points.length; ++i) {
    const a = curve.points[i], b = curve.points[i + 1];
    if (x >= a.x && x <= b.x) {
      return b.x === a.x ? a.y : a.y + (b.y - a.y) * (x - a.x) / (b.x - a.x);
    }
  }
  return null;
}

/** 轴级能力判定（本数据集 criteria 全不启用——只工作点驱动维度；安装/速比/
 *  效率/回隙/寿命/外载荷/工作制/电压/温度/SF 均不适用）。 */
function screenAxis(ax, jf, mf, cr) {
  const reasons = [];
  const gaps = [];
  const at = mf.peakAtTime, seg = mf.peakSegmentId, cs = jf.caseId;
  const m = motors.find((x) => x.modelId === ax.motor);
  const g = gearboxes.find((x) => x.modelId === ax.gearbox);
  // 电机维（§7）：
  if (exceeds(mf.motorTorqueRms, m.ratedTorque, "torque")) {
    reasons.push({ token: "TorqueContinuousInsufficient", tokenOrdinal: TOKEN["TorqueContinuousInsufficient"], candidateModelId: m.modelId,
      axisRef: ax.axis, caseId: cs, atTime: at, segmentId: seg, actual: mf.motorTorqueRms,
      required: m.ratedTorque, unit: "N*m", thresholdSource: "目录 rated_torque_nm（额定连续转矩）" });
  }
  if (exceeds(mf.motorTorquePeak, m.peakTorque, "torque")) {
    reasons.push({ token: "TorquePeakInsufficient", tokenOrdinal: TOKEN["TorquePeakInsufficient"], candidateModelId: m.modelId,
      axisRef: ax.axis, caseId: cs, atTime: at, segmentId: seg, actual: mf.motorTorquePeak,
      required: m.peakTorque, unit: "N*m", thresholdSource: "目录 peak_torque_nm（峰值转矩）" });
  }
  if (exceeds(mf.motorSpeedPeak, m.maxSpeed, "angularVelocity")) {
    reasons.push({ token: "SpeedInsufficient", tokenOrdinal: TOKEN["SpeedInsufficient"], candidateModelId: m.modelId,
      axisRef: ax.axis, caseId: cs, atTime: at, segmentId: seg, actual: mf.motorSpeedPeak,
      required: m.maxSpeed, unit: "rad/s", thresholdSource: "目录 max_speed（最高转速）" });
  }
  if (exceeds(mf.motorSpeedRms, m.ratedSpeed, "angularVelocity")) {
    reasons.push({ token: "SpeedInsufficient", tokenOrdinal: TOKEN["SpeedInsufficient"], candidateModelId: m.modelId,
      axisRef: ax.axis, caseId: cs, atTime: at, segmentId: seg, actual: mf.motorSpeedRms,
      required: m.ratedSpeed, unit: "rad/s", thresholdSource: "目录 rated_speed（额定转速）" });
  }
  for (const which of ["peak", "rms"]) {
    const p = which === "peak" ? mf.motorPowerPeak : mf.motorPowerRms;
    const omega = which === "peak" ? mf.motorSpeedPeak : mf.motorSpeedRms;
    if (m.powerCurveId != null) {
      const y = interp(powerCurve, omega);
      if (y == null) { gaps.push({ dimension: "power-curve", caseId: cs,
        diagCode: "SEL-CURVE-EXTRAPOLATION-DENIED" }); continue; }
      if (exceeds(p, y, "power")) {
        reasons.push({ token: "PowerInsufficient", tokenOrdinal: TOKEN["PowerInsufficient"], candidateModelId: m.modelId,
          axisRef: ax.axis, caseId: cs, atTime: at, segmentId: seg, actual: p,
          required: y, unit: "W",
          thresholdSource: "能力曲线 c101-power（speed→power，工作点转速查询）" });
      }
    } else if (exceeds(p, m.ratedPower, "power")) {
      reasons.push({ token: "PowerInsufficient", tokenOrdinal: TOKEN["PowerInsufficient"], candidateModelId: m.modelId,
        axisRef: ax.axis, caseId: cs, atTime: at, segmentId: seg, actual: p,
        required: m.ratedPower, unit: "W",
        thresholdSource: "目录 rated_power_w（额定功率——固定额定值口径）" });
    }
  }
  // 过载触发维（τ_peak > 额定连续才核查时长）。
  if (exceeds(mf.motorTorquePeak, m.ratedTorque, "torque") && mf.peakDuration == null) {
    gaps.push({ dimension: "overload-duration", caseId: cs, diagCode: null });
  }
  // 减速器维（§8）：
  if (exceeds(jf.jointTorqueRms, g.ratedOutputTorque, "torque")) {
    reasons.push({ token: "GearboxRatedTorqueInsufficient", tokenOrdinal: TOKEN["GearboxRatedTorqueInsufficient"], candidateModelId: g.modelId,
      axisRef: ax.axis, caseId: cs, atTime: at, segmentId: seg, actual: jf.jointTorqueRms,
      required: g.ratedOutputTorque, unit: "N*m",
      thresholdSource: "目录 rated_output_torque_nm（额定输出转矩）" });
  }
  if (exceeds(jf.jointTorquePeak, g.peakOutputTorque, "torque")) {
    reasons.push({ token: "GearboxPeakTorqueInsufficient", tokenOrdinal: TOKEN["GearboxPeakTorqueInsufficient"], candidateModelId: g.modelId,
      axisRef: ax.axis, caseId: cs, atTime: at, segmentId: seg, actual: jf.jointTorquePeak,
      required: g.peakOutputTorque, unit: "N*m",
      thresholdSource: "目录 peak_output_torque_nm（峰值输出转矩）" });
  }
  if (exceeds(mf.motorSpeedPeak, g.maxInputSpeed, "angularVelocity")) {
    reasons.push({ token: "InputSpeedExceeded", tokenOrdinal: TOKEN["InputSpeedExceeded"], candidateModelId: g.modelId,
      axisRef: ax.axis, caseId: cs, atTime: at, segmentId: seg, actual: mf.motorSpeedPeak,
      required: g.maxInputSpeed, unit: "rad/s",
      thresholdSource: "目录 max_input_speed（允许输入转速；ω_m 来源：映射工作点事实 motorSpeedPeak）" });
  }
  return { reasons, gaps };
}

function sortReasons(reasons) {
  const withIdx = reasons.map((r, i) => ({ r, i }));
  withIdx.sort((a, b) => {
    const ta = TOKEN[a.r.token], tb = TOKEN[b.r.token];
    if (ta !== tb) return ta - tb;
    if (a.r.caseId !== b.r.caseId) return a.r.caseId < b.r.caseId ? -1 : 1;
    if (a.r.atTime !== b.r.atTime) return a.r.atTime - b.r.atTime;
    return a.i - b.i;
  });
  return withIdx.map((x) => x.r);
}

/** 逐组合校核复算（§9.3 行序；惯量比规则未配置——未判定态）。 */
function checkCombo(combo) {
  const reasons = [];
  const gaps = [];
  // ② 组合兼容（任一轴无记录即不兼容——组合级原因，caseId 空/atTime 0）。
  for (const ax of combo.axes) {
    const known = compatPairs.some((p) => p.motorId === ax.motor && p.gearboxId === ax.gearbox);
    if (!known) {
      reasons.push({ token: "ComboIncompatible", tokenOrdinal: TOKEN["ComboIncompatible"], candidateModelId: ax.motor,
        axisRef: ax.axis, caseId: "", atTime: 0.0, segmentId: "",
        actual: 0.0, required: 0.0, unit: "",
        actualText: ax.motor + "+" + ax.gearbox,
        requiredText: "compatibility 表存在该型号对记录",
        thresholdSource: "compatibility.csv" });
    }
  }
  // ④⑤ 逐工况格（本数据集单工况 case-A）＋轴级判定合并。
  const caseIds = [...new Set(Object.values(jointFacts).map((f) => f.caseId))];
  const coverage = [];
  for (const cs of caseIds) {
    const cell = { combinationId: combo.id, caseId: cs, verdict: "Feasible",
                   axisRef: null, atTime: 0.0, segmentId: "", note: "pass" };
    const cellAxisRecords = [];
    for (const ax of combo.axes) {
      const jf = { ...jointFacts[ax.axis], caseId: cs };
      const mf = mappingFactOf(combo, ax);
      const { reasons: axReasons, gaps: axGaps } = screenAxis(ax, jf, mf, {});
      cellAxisRecords.push({ verdict: axReasons.length ? "Rejected"
        : axGaps.length ? "DataInsufficient" : "Feasible",
        reasons: axReasons, gaps: axGaps });
    }
    for (const rec of cellAxisRecords) {
      reasons.push(...rec.reasons);
      gaps.push(...rec.gaps);
    }
    // 格聚合（原因优先→缺口次之→通过；定位＝首条失败记录首条原因工作点）。
    const failRec = cellAxisRecords.find((r) => r.verdict === "Rejected" && r.reasons.length);
    const gapRec = cellAxisRecords.find((r) => r.gaps.length);
    if (failRec) {
      const first = sortReasons(failRec.reasons)[0];
      cell.verdict = "Rejected";
      cell.axisRef = first.axisRef; cell.atTime = first.atTime; cell.segmentId = first.segmentId;
      cell.note = kebab(first.token);
    } else if (gapRec) {
      cell.verdict = "DataInsufficient"; cell.note = "data-gap";
    }
    // 惯量比未判定标注（规则未配置＋Feasible 格——§11.3 呈现面）。
    if (cell.verdict === "Feasible") {
      cell.note += ";inertia-ratio-policy-unsettled";
    }
    coverage.push(cell);
  }
  // ⑦ 质量核算（Σ 各轴电机＋减速器质量——kg）。
  let totalMass = 0.0;
  for (const ax of combo.axes) {
    const m = motors.find((x) => x.modelId === ax.motor);
    const g = gearboxes.find((x) => x.modelId === ax.gearbox);
    totalMass += m.mass + g.mass;
  }
  // ⑧ verdict 汇总。
  const verdict = reasons.length ? "Rejected" : gaps.length ? "DataInsufficient" : "Feasible";
  return { combinationId: combo.id, verdict, totalMass,
           reasons: sortReasons(reasons), gaps,
           inertiaRatioUnsettled: true, coverage };
}

// =====================================================================
// 写盘：inputs（目录＋组合指派＋关节侧事实）＋expected（逐组合黄金）
// =====================================================================

const inputs = {
  schema: "sel-combo-inputs/1",
  note: "组合校核黄金输入——黄金目录业务模型（与 sel-catalog/sel-screening 同源）＋组合指派表＋关节侧工作点事实＋筛选条件（全不启用）。电机侧映射事实由生成脚本按 c＝1/n 解析换算（expected 侧同源推导；本 inputs 不重复承载——消费测试从 expected.cases[].mappingFacts 读取并组装 MappingBatchFacts，防止双处漂移）。",
  identity, axes, motors, gearboxes, compatPairs,
  combos: combos.map((c) => ({ id: c.id, axes: c.axes })),
  jointFacts,
  criteria: { note: "全不启用（SF=1.0；工作制/电压/温度/回隙/寿命/效率/速比范围均未配置）",
              safetyFactor: 1.0, requiredDutyClass: "" },
  inertiaRule: { referenceMaxRatio: null, source: "" },
  mappingBatchIdentity: { mappingContractVersion: 1, mappingAlgorithmVersion: 1,
                          upstreamSliceId: "0".repeat(64),
                          completeness: "Complete", mappingFailed: false },
};
writeFileSync(inputsPath, JSON.stringify(inputs, null, 2) + "\n", "utf8");

const expected = {
  schema: "sel-combo-expected/1",
  note: "组合校核黄金——由 generate/make_sel_combo_golden.mjs 按 §9.3/§9.4 冻结语义复算（组合级原因 token 词表序稳定排序；格聚合原因优先→缺口次之；惯量比未配置＝未判定态——Feasible 格 note 追加 inertia-ratio-policy-unsettled；totalMass＝Σ 各轴电机＋减速器质量）。mappingFacts 为消费测试组装 MappingBatchFacts 的数值事实源（c＝1/n 解析换算——黄金联动单点在本脚本）。",
  cases: combos.map((combo) => ({
    id: combo.id,
    mappingFacts: combo.axes.map((ax) => mappingFactOf(combo, ax)),
    check: checkCombo(combo),
  })),
};
writeFileSync(expectedPath, JSON.stringify(expected, null, 2) + "\n", "utf8");

for (const c of expected.cases) {
  console.log(`K ${c.id}: ${c.check.verdict}  mass=${c.check.totalMass}  `
    + `reasons=[${c.check.reasons.map((r) => r.token).join(",")}]  `
    + `coverage=[${c.check.coverage.map((x) => x.verdict + "/" + x.note).join(" ")}]`);
}
