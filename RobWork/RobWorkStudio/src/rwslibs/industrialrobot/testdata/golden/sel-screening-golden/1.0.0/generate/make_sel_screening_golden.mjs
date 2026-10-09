/**
 * make_sel_screening_golden.mjs —— selection 电机/减速器硬筛选黄金数据集的
 * 独立参考实现（WP-19-T11，AT-08"硬筛选→可行/不可行型号黄金表——每个淘汰项
 * 含实际值和阈值"）。
 *
 * 数据集：sel-screening-golden（kind=analytic-case）。本脚本从内置唯一数据源
 * （黄金目录源——与 sel-catalog-golden 同一器件数据）按 units/selection.md
 * §7.1/§7.2/§8.1/§8.2 冻结语义**逐维度独立复算**四个算例的逐候选逐轴判定：
 *   - case-heavy：全维度启用的重载工况（逐维淘汰/缺口黄金——转矩/转速/功率/
 *     过载时长/制动保持/工作制/电压/安装/回隙/寿命/效率/速比/外载荷力臂核算）；
 *   - case-light：全维度不启用的轻载工况（六候选基准可行面——Feasible 黄金）；
 *   - sf-edge：安全系数复判（SF=1.25）的"恰好满足"边界面（工作点×SF 恰达
 *     目录能力——容差内不误淘汰）与加严不足面；
 *   - axis-oos：移动关节轴范围外阻断（SEL-09——DataInsufficient＋
 *     SEL-INPUT-AXIS-OUT-OF-SCOPE，不执行任何旋转传动维度）。
 *
 * 判定复刻的公式来源（本脚本的唯一权威——按卡面文字直写，与产品实现
 * selection/src/Screening.cpp 零共享代码；阈值比较容差＝附录 D C7 core
 * runtimeAbsoluteTolerance：torque/角速度 1e-9、angle/dimensionless 1e-12、
 * 其余 0）：
 *   - exceeds(actual, limit, kind) ＝ actual > limit + tol(kind)（"≤ limit 为满足"）；
 *   - below(actual, limit, kind)  ＝ actual < limit − tol(kind)（"目录 ≥ 要求"）；
 *   - 功率能力双口径：条目声明 speed→power 曲线时按 §6.2 分段线性插值（闭区间，
 *     默认禁止外推——区间外拒绝＝数据不足分轨）；无该曲线时退目录额定功率；
 *   - 温度降额档位公式（§19.3 T04 细化④）：f＝factorPerRef^ceil(max(0,T_env−T_ref))，
 *     f<1 时对连续/峰值转矩复判（本数据源 ref=60 且 T_env=55 → f=1 无折减；
 *     折减不足面由既有 T04 单测承载，黄金面不重复）；
 *   - 外载荷力臂核算（§19.3 T04 细化⑥）：F_allow＝F_rated×L_rated/L_actual
 *     （仅当 L_actual＞L_rated＞0 时折减；轴向直比）；
 *   - 减速器输入转速：ω_m 优先取映射事实 motorSpeedPeak，未供给时 ω_m＝ω_joint_peak/
 *     ratio（§8.2 明文允许的唯一自算；本数据集 facts 均供映射事实——黄金面钉
 *     "ω_m 来自映射工作点"的阈值来源词面）；
 *   - verdict 汇总：reasons 非空→Rejected；否则 gaps 非空→DataInsufficient；
 *     否则 Feasible（§19.3 T04 细化⑦）；
 *   - 记录序＝候选快照序（modelId 升序）×轴 facts 首现序；原因稳定排序＝
 *     token 词表序→caseId→atTime（stable——§10.4）；缺口序＝维度执行序。
 *
 * 用法：node make_sel_screening_golden.mjs（在本目录执行）。
 */

import { writeFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const here = dirname(fileURLToPath(import.meta.url));
const inputsPath = join(here, "..", "inputs", "screening-inputs.json");
const expectedPath = join(here, "..", "expected", "screening-expected.json");

// =====================================================================
// 黄金目录源（与 sel-catalog-golden/motors.csv・gearboxes.csv 同一器件数据
// ——同源单点；以业务模型字段承载，消费测试据此构造目录条目/快照）
// =====================================================================

const identity = { catalogId: "cat-sel-golden", version: "1.0.0",
                   source: "黄金器件目录（selection 契约测试同源数据——WP-19-T11）" };

const motors = [
  { modelId: "M-101", ratedTorque: 8.0, peakTorque: 20.0, ratedSpeed: 150.0,
    maxSpeed: 300.0, ratedPower: 2200.0, overload: { torque: 20.0, duration: 12.0 },
    dutyClass: "S1", ratedVoltage: 220.0, thermal: { refTemp: 60.0, factorPerRef: 0.90 },
    brakeTorque: 25.0, holdingTorque: 30.0, rotorInertia: 0.012, mass: 5.5,
    flangeKind: "flangeA", shaftKind: "shaftB",
    powerCurveId: "c101-power" },
  { modelId: "M-102", ratedTorque: 3.0, peakTorque: 8.0, ratedSpeed: 150.0,
    maxSpeed: 250.0, ratedPower: 600.0, overload: { torque: 8.0, duration: 6.0 },
    dutyClass: "S1", ratedVoltage: 220.0, thermal: null,
    brakeTorque: 10.0, holdingTorque: 12.0, rotorInertia: 0.020, mass: 3.2,
    flangeKind: "flangeA", shaftKind: "shaftB", powerCurveId: null },
  { modelId: "M-103", ratedTorque: 10.0, peakTorque: 25.0, ratedSpeed: 200.0,
    maxSpeed: 400.0, ratedPower: 3500.0, overload: { torque: 25.0, duration: 15.0 },
    dutyClass: "S2", ratedVoltage: 380.0, thermal: null,
    brakeTorque: null, holdingTorque: null, rotorInertia: 0.030, mass: 7.0,
    flangeKind: "flangeC", shaftKind: "shaftB", powerCurveId: null },
];

const gearboxes = [
  { modelId: "G-201", ratedOutputTorque: 120.0, peakOutputTorque: 240.0,
    maxInputSpeed: 300.0, ratio: 50.0, efficiency: 0.92, backlash: 0.0015,
    ratedLife: 6000, mountingOrientation: "any",
    extLoad: { radial: 1300.0, axial: 800.0, distance: 0.16 },
    mass: 6.5, housingInertia: 0.012, flangeKind: "flangeA", shaftKind: "shaftB" },
  { modelId: "G-202", ratedOutputTorque: 60.0, peakOutputTorque: 120.0,
    maxInputSpeed: 250.0, ratio: 100.0, efficiency: 0.80, backlash: 0.003,
    ratedLife: 4000, mountingOrientation: "any",
    extLoad: { radial: 500.0, axial: 600.0, distance: 0.05 },
    mass: 3.0, housingInertia: 0.006, flangeKind: "flangeA", shaftKind: "shaftB" },
  { modelId: "G-203", ratedOutputTorque: 200.0, peakOutputTorque: 400.0,
    maxInputSpeed: 300.0, ratio: 30.0, efficiency: 0.95, backlash: null,
    ratedLife: 8000, mountingOrientation: "up",
    extLoad: null,
    mass: 9.0, housingInertia: 0.020, flangeKind: "flangeA", shaftKind: "shaftB" },
];

// c101-power：M-101 的 speed→power 曲线（严格线性段——插值解析值手算可验：
// y(x)＝800＋8·(x−100) 在 [100,200]；y(x)＝1600＋8·(x−200) 在 [200,300]）。
const powerCurve = { curveId: "c101-power", xQuantity: "speed", yQuantity: "power",
                     xUnit: "rad/s", yUnit: "W",
                     points: [ { x: 100, y: 800 }, { x: 200, y: 1600 }, { x: 300, y: 2400 } ] };

// 黄金轴（固定 canonical ID——记录键 "<modelId>|<canonical>" 的确定性承载；
// 非全零即合法——core ObjectId 保留值纪律只禁全零）。
const axes = {
  j1: "obj-00000000000000000000000000000101",
  j2: "obj-00000000000000000000000000000202",
};

// =====================================================================
// 判定复刻引擎（卡面 §7.1/§8.1 逐维独立——token 枚举序＝词表序）
// =====================================================================

// 附录 D C7 运行侧绝对容差（量纲 token → 值；未列＝0 精确）。
const C7 = { torque: 1e-9, angularVelocity: 1e-9, angle: 1e-12, dimensionless: 1e-12 };
const tol = (k) => C7[k] ?? 0.0;
const exceeds = (actual, limit, kind) => actual > limit + tol(kind);
const below = (actual, limit, kind) => actual < limit - tol(kind);

// ReasonToken 词表序（Screening.hpp 枚举序——稳定排序键首位）。
const TOKEN = {
  TorqueContinuousInsufficient: 0, TorquePeakInsufficient: 1, SpeedInsufficient: 2,
  PowerInsufficient: 3, OverloadTimeInsufficient: 4, DutyMismatch: 5,
  VoltageMismatch: 6, ThermalDeratingInsufficient: 7, BrakeInsufficient: 8,
  HoldingInsufficient: 9, SafetyFactorInsufficient: 10,
  GearboxRatedTorqueInsufficient: 11, GearboxPeakTorqueInsufficient: 12,
  InputSpeedExceeded: 13, RatioMismatch: 14, EfficiencyInsufficient: 15,
  BacklashExceeded: 16, LifeInsufficient: 17, MountingIncompatible: 18,
  ExternalLoadExceeded: 19,
};

/** 分段线性插值（§6.2——闭区间含端点；区间外返回 null＝拒绝轨）。 */
function interp(curve, x) {
  if (x < curve.points[0].x || x > curve.points[curve.points.length - 1].x) return null;
  for (let i = 0; i + 1 < curve.points.length; ++i) {
    const a = curve.points[i], b = curve.points[i + 1];
    if (x >= a.x && x <= b.x) {
      if (b.x === a.x) return a.y;
      return a.y + (b.y - a.y) * (x - a.x) / (b.x - a.x);
    }
  }
  return null;
}

/** 曲线插值解析值（inputs 手算锚点通道——双路防两路同错）。 */
function interpAnalytic(curve, x) { return interp(curve, x); }

function reason(token, modelId, axis, caseId, atTime, segmentId,
                actual, required, unit, thresholdSource, extra = {}) {
  return { token, tokenOrdinal: TOKEN[token], candidateModelId: modelId, axisRef: axis, caseId, atTime,
           segmentId, actual, required, unit, thresholdSource, ...extra };
}
function gap(dimension, caseId, diagCode = null) {
  return { dimension, caseId, diagCode };
}

/** 电机单工况全维度复算（执行序＝§7.1：安装→转矩→转速→功率→过载→工作制
 *  →电压→温度→制动保持→安全系数；候选能力维度不短路——全部执行完才汇总）。 */
function screenMotorCase(m, f, cr, catalog) {
  const reasons = [];
  const gaps = [];
  const at = f.atTime, seg = f.segmentId, cs = f.caseId;
  // ② 安装（flange/shaft 两子项——orientation 为减速器专用）。
  const mr = f.mountRequirement;
  if (mr) {
    if (mr.flangeKind !== "") {
      if (m.flangeKind === "") gaps.push(gap("mounting-flange", cs));
      else if (m.flangeKind !== mr.flangeKind) {
        reasons.push(reason("MountingIncompatible", m.modelId, f.axisRef, cs, at, seg,
          0.0, 0.0, "", "关节安装关系（轴侧事实 mountRequirement.flangeKind）",
          { actualText: m.flangeKind, requiredText: mr.flangeKind }));
      }
    }
    if (mr.shaftKind !== "") {
      if (m.shaftKind === "") gaps.push(gap("mounting-shaft", cs));
      else if (m.shaftKind !== mr.shaftKind) {
        reasons.push(reason("MountingIncompatible", m.modelId, f.axisRef, cs, at, seg,
          0.0, 0.0, "", "关节安装关系（轴侧事实 mountRequirement.shaftKind）",
          { actualText: m.shaftKind, requiredText: mr.shaftKind }));
      }
    }
  }
  // 维 1：连续转矩。
  if (f.motorTorqueRms == null) gaps.push(gap("continuous-torque", cs));
  else if (exceeds(f.motorTorqueRms, m.ratedTorque, "torque")) {
    reasons.push(reason("TorqueContinuousInsufficient", m.modelId, f.axisRef, cs, at, seg,
      f.motorTorqueRms, m.ratedTorque, "N*m", "目录 rated_torque_nm（额定连续转矩）"));
  }
  // 维 2：峰值转矩。
  if (f.motorTorquePeak == null) gaps.push(gap("peak-torque", cs));
  else if (exceeds(f.motorTorquePeak, m.peakTorque, "torque")) {
    reasons.push(reason("TorquePeakInsufficient", m.modelId, f.axisRef, cs, at, seg,
      f.motorTorquePeak, m.peakTorque, "N*m", "目录 peak_torque_nm（峰值转矩）"));
  }
  // 维 3：转速（peak vs max、rms vs rated 两子项独立）。
  if (f.motorSpeedPeak == null) gaps.push(gap("speed-peak", cs));
  else if (exceeds(f.motorSpeedPeak, m.maxSpeed, "angularVelocity")) {
    reasons.push(reason("SpeedInsufficient", m.modelId, f.axisRef, cs, at, seg,
      f.motorSpeedPeak, m.maxSpeed, "rad/s", "目录 max_speed（最高转速）"));
  }
  if (f.motorSpeedRms == null) gaps.push(gap("speed-rms", cs));
  else if (exceeds(f.motorSpeedRms, m.ratedSpeed, "angularVelocity")) {
    reasons.push(reason("SpeedInsufficient", m.modelId, f.axisRef, cs, at, seg,
      f.motorSpeedRms, m.ratedSpeed, "rad/s", "目录 rated_speed（额定转速）"));
  }
  // 维 4：功率（peak/rms 两子项；曲线优先、额定兜底、拒绝＝缺口）。
  const powerLimit = (omega) => {
    if (m.powerCurveId != null) {
      const y = omega == null ? null : interp(powerCurve, omega);
      if (y == null) return { refused: true };
      return { limit: y, source: "能力曲线 c101-power（speed→power，工作点转速查询）" };
    }
    if (omega == null && m.powerCurveId != null) return { refused: true };
    return { limit: m.ratedPower, source: "目录 rated_power_w（额定功率——固定额定值口径）" };
  };
  for (const which of ["peak", "rms"]) {
    const p = which === "peak" ? f.motorPowerPeak : f.motorPowerRms;
    const omega = which === "peak" ? f.motorSpeedPeak : f.motorSpeedRms;
    if (p == null) { gaps.push(gap(`power-${which}`, cs)); continue; }
    const lim = powerLimit(omega);
    if (lim.refused) { gaps.push(gap("power-curve", cs, "SEL-CURVE-EXTRAPOLATION-DENIED")); continue; }
    if (exceeds(p, lim.limit, "power")) {
      reasons.push(reason("PowerInsufficient", m.modelId, f.axisRef, cs, at, seg,
        p, lim.limit, "W", lim.source));
    }
  }
  // 维 5：过载持续时间（触发式——峰值进入过载区才核查）。
  if (f.motorTorquePeak != null && f.motorTorqueRms != null
      && exceeds(f.motorTorquePeak, m.ratedTorque, "torque")) {
    if (m.overload == null) gaps.push(gap("overload-duration", cs));
    else if (f.peakDuration == null) gaps.push(gap("overload-duration", cs));
    else if (exceeds(f.peakDuration, m.overload.duration, "time")) {
      reasons.push(reason("OverloadTimeInsufficient", m.modelId, f.axisRef, cs, at, seg,
        f.peakDuration, m.overload.duration, "s", "目录 overload_duration_s（过载持续时间）"));
    }
  }
  // 维 6：工作制（条件缺失＝不适用）。
  if (cr.requiredDutyClass !== "" && m.dutyClass !== cr.requiredDutyClass) {
    reasons.push(reason("DutyMismatch", m.modelId, f.axisRef, cs, at, seg,
      0.0, 0.0, "", "目录 duty_class（工作制词表值）",
      { actualText: m.dutyClass, requiredText: cr.requiredDutyClass }));
  }
  // 维 7：电压（|V_rated−V_req| ≤ tol·|V_req|；条件启用而目录未声明→缺口）。
  if (cr.requiredVoltage != null) {
    if (m.ratedVoltage == null) gaps.push(gap("voltage", cs));
    else if (Math.abs(m.ratedVoltage - cr.requiredVoltage)
             > cr.voltageRelativeTolerance * Math.abs(cr.requiredVoltage) + 0.0) {
      reasons.push(reason("VoltageMismatch", m.modelId, f.axisRef, cs, at, seg,
        m.ratedVoltage, cr.requiredVoltage, "V",
        "目录 rated_voltage_v（额定电压；匹配容差＝筛选条件相对容差）"));
    }
  }
  // 维 8：温度降额（f<1 时对连续/峰值转矩复判；本数据源 f=1 全通过面）。
  if (cr.ambientTemp != null) {
    if (m.thermal == null) gaps.push(gap("thermal-derating", cs));
    else {
      const over = cr.ambientTemp - m.thermal.refTemp;
      const steps = over <= 0 ? 0 : Math.ceil(over);
      const factor = Math.pow(m.thermal.factorPerRef, steps);
      if (factor < 1.0) {
        if (f.motorTorqueRms != null
            && exceeds(f.motorTorqueRms, m.ratedTorque * factor, "torque")) {
          reasons.push(reason("ThermalDeratingInsufficient", m.modelId, f.axisRef, cs, at, seg,
            f.motorTorqueRms, m.ratedTorque * factor, "N*m",
            `目录 thermal_factor_per_ref 折减后额定连续转矩（f=${factor.toFixed(6)}）`));
        }
        if (f.motorTorquePeak != null
            && exceeds(f.motorTorquePeak, m.peakTorque * factor, "torque")) {
          reasons.push(reason("ThermalDeratingInsufficient", m.modelId, f.axisRef, cs, at, seg,
            f.motorTorquePeak, m.peakTorque * factor, "N*m",
            `目录 thermal_factor_per_ref 折减后峰值转矩（f=${factor.toFixed(6)}）`));
        }
      }
    }
  }
  // 维 9：制动/保持（保持需求触发；两能力子项独立）。
  if (f.requiredHoldingTorque != null) {
    if (m.brakeTorque == null) gaps.push(gap("brake", cs));
    else if (exceeds(f.requiredHoldingTorque, m.brakeTorque, "torque")) {
      reasons.push(reason("BrakeInsufficient", m.modelId, f.axisRef, cs, at, seg,
        f.requiredHoldingTorque, m.brakeTorque, "N*m", "目录 brake_torque_nm（制动能力）"));
    }
    if (m.holdingTorque == null) gaps.push(gap("holding", cs));
    else if (exceeds(f.requiredHoldingTorque, m.holdingTorque, "torque")) {
      reasons.push(reason("HoldingInsufficient", m.modelId, f.axisRef, cs, at, seg,
        f.requiredHoldingTorque, m.holdingTorque, "N*m", "目录 holding_torque_nm（保持能力）"));
    }
  }
  // 维 10：安全系数（SF>1 启用；对已供给的 τ/ω/P 逐项 ×SF 复判）。
  if (cr.safetyFactor > 1.0) {
    const sf = cr.safetyFactor;
    const sfSource = (tail) => `安全系数条件（SF=${sf.toFixed(6)}）复判——${tail}`;
    if (f.motorTorqueRms != null
        && exceeds(f.motorTorqueRms * sf, m.ratedTorque, "torque")) {
      reasons.push(reason("SafetyFactorInsufficient", m.modelId, f.axisRef, cs, at, seg,
        f.motorTorqueRms * sf, m.ratedTorque, "N*m", sfSource("目录 rated_torque_nm")));
    }
    if (f.motorTorquePeak != null
        && exceeds(f.motorTorquePeak * sf, m.peakTorque, "torque")) {
      reasons.push(reason("SafetyFactorInsufficient", m.modelId, f.axisRef, cs, at, seg,
        f.motorTorquePeak * sf, m.peakTorque, "N*m", sfSource("目录 peak_torque_nm")));
    }
    if (f.motorSpeedPeak != null
        && exceeds(f.motorSpeedPeak * sf, m.maxSpeed, "angularVelocity")) {
      reasons.push(reason("SafetyFactorInsufficient", m.modelId, f.axisRef, cs, at, seg,
        f.motorSpeedPeak * sf, m.maxSpeed, "rad/s", sfSource("目录 max_speed")));
    }
    if (f.motorSpeedRms != null
        && exceeds(f.motorSpeedRms * sf, m.ratedSpeed, "angularVelocity")) {
      reasons.push(reason("SafetyFactorInsufficient", m.modelId, f.axisRef, cs, at, seg,
        f.motorSpeedRms * sf, m.ratedSpeed, "rad/s", sfSource("目录 rated_speed")));
    }
    for (const which of ["peak", "rms"]) {
      const p = which === "peak" ? f.motorPowerPeak : f.motorPowerRms;
      const omega = which === "peak" ? f.motorSpeedPeak : f.motorSpeedRms;
      if (p == null) continue;
      const lim = powerLimit(omega);
      if (lim.refused) continue; // 查询拒绝已由维 4 记缺口——SF 复核跳过不重复记。
      if (exceeds(p * sf, lim.limit, "power")) {
        reasons.push(reason("SafetyFactorInsufficient", m.modelId, f.axisRef, cs, at, seg,
          p * sf, lim.limit, "W", sfSource(lim.source)));
      }
    }
  }
  return { reasons, gaps };
}

/** 减速器单工况全维度复算（执行序＝§8.1：安装→速比→额定→峰值→输入转速
 *  →效率→回隙→寿命→外载荷）。 */
function screenGearboxCase(g, f, cr) {
  const reasons = [];
  const gaps = [];
  const at = f.atTime, seg = f.segmentId, cs = f.caseId;
  // ① 安装（法兰/轴伸/安装方向三子项）。
  const mr = f.mountRequirement;
  if (mr) {
    if (mr.flangeKind !== "") {
      if (g.flangeKind === "") gaps.push(gap("mounting-flange", cs));
      else if (g.flangeKind !== mr.flangeKind) {
        reasons.push(reason("MountingIncompatible", g.modelId, f.axisRef, cs, at, seg,
          0.0, 0.0, "", "关节安装关系（轴侧事实 mountRequirement.flangeKind）",
          { actualText: g.flangeKind, requiredText: mr.flangeKind }));
      }
    }
    if (mr.shaftKind !== "") {
      if (g.shaftKind === "") gaps.push(gap("mounting-shaft", cs));
      else if (g.shaftKind !== mr.shaftKind) {
        reasons.push(reason("MountingIncompatible", g.modelId, f.axisRef, cs, at, seg,
          0.0, 0.0, "", "关节安装关系（轴侧事实 mountRequirement.shaftKind）",
          { actualText: g.shaftKind, requiredText: mr.shaftKind }));
      }
    }
    if (mr.orientation !== "" && g.mountingOrientation !== mr.orientation) {
      reasons.push(reason("MountingIncompatible", g.modelId, f.axisRef, cs, at, seg,
        0.0, 0.0, "", "目录 mounting_orientation（安装方向词表值）vs 关节安装关系",
        { actualText: g.mountingOrientation, requiredText: mr.orientation }));
    }
  }
  // ② 速比（条件未配置＝不适用；越界侧边界值入 required）。
  if (cr.ratioRange != null) {
    const r = g.ratio;
    if (r < cr.ratioRange.min || r > cr.ratioRange.max) {
      const bound = r < cr.ratioRange.min ? cr.ratioRange.min : cr.ratioRange.max;
      reasons.push(reason("RatioMismatch", g.modelId, f.axisRef, cs, at, seg,
        r, bound, "1", "筛选条件 ratio_range（该轴允许传动比范围，闭区间）"));
    }
  }
  // ③ 额定输出转矩。
  if (f.jointTorqueRms == null) gaps.push(gap("gb-rated-torque", cs));
  else if (exceeds(f.jointTorqueRms, g.ratedOutputTorque, "torque")) {
    reasons.push(reason("GearboxRatedTorqueInsufficient", g.modelId, f.axisRef, cs, at, seg,
      f.jointTorqueRms, g.ratedOutputTorque, "N*m", "目录 rated_output_torque_nm（额定输出转矩）"));
  }
  // ④ 峰值输出转矩。
  if (f.jointTorquePeak == null) gaps.push(gap("gb-peak-torque", cs));
  else if (exceeds(f.jointTorquePeak, g.peakOutputTorque, "torque")) {
    reasons.push(reason("GearboxPeakTorqueInsufficient", g.modelId, f.axisRef, cs, at, seg,
      f.jointTorquePeak, g.peakOutputTorque, "N*m", "目录 peak_output_torque_nm（峰值输出转矩）"));
  }
  // ⑤ 输入转速（ω_m 优先映射事实；未供给时 ω_m＝ω_joint_peak/ratio）。
  let omegaMotor = null;
  let omegaSource = "";
  if (f.motorSpeedPeak != null) {
    omegaMotor = f.motorSpeedPeak;
    omegaSource = "映射工作点事实 motorSpeedPeak";
  } else if (f.jointSpeedPeak != null) {
    omegaMotor = f.jointSpeedPeak / g.ratio;
    omegaSource = "关节峰值角速度 ÷ 目录速比（候选传动参数换算）";
  }
  if (omegaMotor == null) gaps.push(gap("gb-input-speed", cs));
  else if (exceeds(omegaMotor, g.maxInputSpeed, "angularVelocity")) {
    reasons.push(reason("InputSpeedExceeded", g.modelId, f.axisRef, cs, at, seg,
      omegaMotor, g.maxInputSpeed, "rad/s",
      `目录 max_input_speed（允许输入转速；ω_m 来源：${omegaSource}）`));
  }
  // ⑥ 效率（目录 ≥ 要求）。
  if (cr.minEfficiency != null
      && below(g.efficiency, cr.minEfficiency, "dimensionless")) {
    reasons.push(reason("EfficiencyInsufficient", g.modelId, f.axisRef, cs, at, seg,
      g.efficiency, cr.minEfficiency, "1", "筛选条件 min_efficiency（最低效率要求）"));
  }
  // ⑦ 回程间隙（条件启用而目录缺失→缺口）。
  if (cr.maxBacklash != null) {
    if (g.backlash == null) gaps.push(gap("backlash", cs));
    else if (exceeds(g.backlash, cr.maxBacklash, "angle")) {
      reasons.push(reason("BacklashExceeded", g.modelId, f.axisRef, cs, at, seg,
        g.backlash, cr.maxBacklash, "rad",
        "目录 backlash（回程间隙，SI rad）vs 筛选条件 max_backlash"));
    }
  }
  // ⑧ 寿命。
  if (cr.requiredLife != null) {
    if (g.ratedLife == null) gaps.push(gap("life", cs));
    else if (below(g.ratedLife, cr.requiredLife, "dimensionless")) {
      reasons.push(reason("LifeInsufficient", g.modelId, f.axisRef, cs, at, seg,
        g.ratedLife, cr.requiredLife, "1",
        "目录 rated_life（额定寿命，循环数）vs 筛选条件 required_life"));
    }
  }
  // ⑨ 允许外载荷（轴向直比＋径向力臂核算）。
  if (f.externalLoad != null) {
    if (g.extLoad == null) gaps.push(gap("external-load", cs));
    else {
      if (exceeds(f.externalLoad.axial, g.extLoad.axial, "force")) {
        reasons.push(reason("ExternalLoadExceeded", g.modelId, f.axisRef, cs, at, seg,
          f.externalLoad.axial, g.extLoad.axial, "N", "目录 external_load_axial_n（允许轴向力）"));
      }
      let radialAllow = g.extLoad.radial;
      let radialSource = "目录 external_load_radial_n（允许径向力）";
      if (g.extLoad.distance > 0 && f.externalLoad.distance > g.extLoad.distance) {
        radialAllow = g.extLoad.radial * g.extLoad.distance / f.externalLoad.distance;
        radialSource = "目录 external_load_radial_n @ external_load_dist_m"
                     + "（作用点力臂核算 F_allow＝F_rated×L_rated/L_actual）";
      }
      if (exceeds(f.externalLoad.radial, radialAllow, "force")) {
        reasons.push(reason("ExternalLoadExceeded", g.modelId, f.axisRef, cs, at, seg,
          f.externalLoad.radial, radialAllow, "N", radialSource));
      }
    }
  }
  return { reasons, gaps };
}

/** 原因稳定排序（token 词表序→caseId→atTime——stable 同键保执行序）。 */
function sortReasons(reasons) {
  const withIdx = reasons.map((r, i) => ({ r, i }));
  withIdx.sort((a, b) => {
    const ta = TOKEN[a.r.token], tb = TOKEN[b.r.token];
    if (ta !== tb) return ta - tb;
    if (a.r.caseId !== b.r.caseId) return a.r.caseId < b.r.caseId ? -1 : 1;
    if (a.r.atTime !== b.r.atTime) return a.r.atTime - b.r.atTime;
    return a.i - b.i; // stable——保留执行序。
  });
  return withIdx.map((x) => x.r);
}

const verdictOf = (reasons, gaps) =>
  reasons.length > 0 ? "Rejected" : gaps.length > 0 ? "DataInsufficient" : "Feasible";

// =====================================================================
// 四个黄金算例（criteria/facts 与期望记录全由复刻引擎推导）
// =====================================================================

const baseCriteria = { safetyFactor: 1.0, requiredDutyClass: "", requiredVoltage: null,
                       voltageRelativeTolerance: 0.0, ambientTemp: null,
                       maxBacklash: null, requiredLife: null, minEfficiency: null,
                       ratioRange: null };

/** 算例 1：case-heavy——全维度启用的重载工况（淘汰/缺口逐维黄金）。 */
const heavyFacts = {
  axisRef: "j1", caseId: "case-A", jointKind: "revolute",
  motorTorqueRms: 6.0, motorTorquePeak: 15.0, motorSpeedPeak: 320.0,
  motorSpeedRms: 130.0, motorPowerPeak: 5200.0, motorPowerRms: 1000.0,
  peakDuration: 14.0,
  jointTorqueRms: 55.0, jointTorquePeak: 130.0, jointSpeedPeak: 6.4,
  requiredHoldingTorque: 28.0,
  mountRequirement: { flangeKind: "flangeA", shaftKind: "shaftB", orientation: "any" },
  externalLoad: { radial: 1000.0, axial: 500.0, distance: 0.10 },
  atTime: 2.5, segmentId: "seg-A1",
};
const heavyCriteria = { ...baseCriteria, requiredDutyClass: "S1", requiredVoltage: 220.0,
                        voltageRelativeTolerance: 0.05, ambientTemp: 55.0,
                        maxBacklash: 0.002, requiredLife: 5000, minEfficiency: 0.85,
                        ratioRange: { min: 20, max: 120 } };

/** 算例 2：case-light——全维度不启用的轻载工况（六候选基准可行面）。 */
const lightFacts = {
  axisRef: "j1", caseId: "case-B", jointKind: "revolute",
  motorTorqueRms: 2.0, motorTorquePeak: 5.0, motorSpeedPeak: 150.0,
  motorSpeedRms: 110.0, motorPowerPeak: 450.0, motorPowerRms: 240.0,
  peakDuration: 2.0,
  jointTorqueRms: 12.0, jointTorquePeak: 28.0, jointSpeedPeak: 3.0,
  requiredHoldingTorque: null, mountRequirement: null, externalLoad: null,
  atTime: 1.0, segmentId: "seg-B1",
};

/** 算例 3：sf-edge——SF=1.25 复判（恰好满足边界＋加严不足）。 */
const sfFacts = {
  axisRef: "j1", caseId: "case-C", jointKind: "revolute",
  motorTorqueRms: 6.0, motorTorquePeak: 15.0, motorSpeedPeak: 240.0,
  motorSpeedRms: 120.0, motorPowerPeak: 1500.0, motorPowerRms: 700.0,
  peakDuration: 3.0,
  jointTorqueRms: 20.0, jointTorquePeak: 45.0, jointSpeedPeak: 4.0,
  requiredHoldingTorque: null, mountRequirement: null, externalLoad: null,
  atTime: 0.5, segmentId: "seg-C1",
};
const sfCriteria = { ...baseCriteria, safetyFactor: 1.25 };

/** 算例 4：axis-oos——移动关节轴范围外（SEL-09；全空工作点）。 */
const oosFacts = {
  axisRef: "j2", caseId: "case-B", jointKind: "prismatic",
  motorTorqueRms: null, motorTorquePeak: null, motorSpeedPeak: null,
  motorSpeedRms: null, motorPowerPeak: null, motorPowerRms: null,
  peakDuration: null,
  jointTorqueRms: null, jointTorquePeak: null, jointSpeedPeak: null,
  requiredHoldingTorque: null, mountRequirement: null, externalLoad: null,
  atTime: 0.0, segmentId: "",
};

function runCase(id, criteria, factsList, analyticAnchors) {
  const records = [];
  for (const m of motors) {          // 候选序＝modelId 升序
    for (const f of factsList) {     // 轴序＝facts 首现序
      if (f.jointKind === "prismatic") {
        // SEL-09 范围外：不执行任何 §7 维度——记录 DataInsufficient＋恰一缺口。
        records.push({
          id: `${m.modelId}|${axes[f.axisRef]}`, deviceKind: "Motor",
          candidateModelId: m.modelId, axisRef: f.axisRef, verdict: "DataInsufficient",
          reasons: [], gaps: [ { dimension: "axis-out-of-scope", caseId: "",
                                diagCode: "SEL-INPUT-AXIS-OUT-OF-SCOPE" } ],
        });
        continue;
      }
      const { reasons, gaps } = screenMotorCase(m, f, criteria);
      records.push({
        id: `${m.modelId}|${axes[f.axisRef]}`, deviceKind: "Motor",
        candidateModelId: m.modelId, axisRef: f.axisRef,
        verdict: verdictOf(reasons, gaps),
        reasons: sortReasons(reasons), gaps,
      });
    }
  }
  for (const g of gearboxes) {
    for (const f of factsList) {
      if (f.jointKind === "prismatic") {
        records.push({
          id: `${g.modelId}|${axes[f.axisRef]}`, deviceKind: "Gearbox",
          candidateModelId: g.modelId, axisRef: f.axisRef, verdict: "DataInsufficient",
          reasons: [], gaps: [ { dimension: "axis-out-of-scope", caseId: "",
                                diagCode: "SEL-INPUT-AXIS-OUT-OF-SCOPE" } ],
        });
        continue;
      }
      const { reasons, gaps } = screenGearboxCase(g, f, criteria);
      records.push({
        id: `${g.modelId}|${axes[f.axisRef]}`, deviceKind: "Gearbox",
        candidateModelId: g.modelId, axisRef: f.axisRef,
        verdict: verdictOf(reasons, gaps),
        reasons: sortReasons(reasons), gaps,
      });
    }
  }
  return { id, criteria, facts: factsList, records, analyticAnchors };
}

// 手算锚点（第三路防"参考实现与产品实现两路同错"——消费测试独立断言）：
//   - 曲线插值解析：@130 → 800+8×30＝1040 W（case-heavy M-101 P_rms 的能力值）；
//   - 力臂核算解析：500×0.05/0.10＝250 N（case-heavy G-202 径向允许力）；
//   - SF 恰好边界：240×1.25＝300.0（＝M-101 maxSpeed，C7 角速度容差 1e-9 内不淘汰）。
const analyticAnchors = {
  powerCurveAt130: interpAnalytic(powerCurve, 130.0),          // → 1040
  radialAllowG202: 500.0 * 0.05 / 0.10,                        // → 250
  sfEdgeSpeedPeak: sfFacts.motorSpeedPeak * sfCriteria.safetyFactor, // → 300
};

const cases = [
  runCase("case-heavy", heavyCriteria, [heavyFacts], analyticAnchors),
  runCase("case-light", baseCriteria, [lightFacts], {}),
  runCase("sf-edge", sfCriteria, [sfFacts], {}),
  runCase("axis-oos", baseCriteria, [oosFacts], {}),
];

// =====================================================================
// 写盘：inputs（目录业务模型＋轴＋曲线）＋expected（逐算例黄金记录）
// =====================================================================

const inputs = {
  schema: "sel-screening-inputs/1",
  note: "硬筛选黄金输入——黄金目录业务模型（与 sel-catalog-golden 器件数据同源）＋固定 canonical 轴 ID＋逐算例筛选条件与工作点事实。电机侧工作点由调用方（本数据集）值传递供给——P-SEL-1 提议契约 v1 的筛选器直调形态（不自算映射量，§8.2 纪律）。",
  identity, axes,
  motors, gearboxes,
  curves: [powerCurve],
};
writeFileSync(inputsPath, JSON.stringify(inputs, null, 2) + "\n", "utf8");

const expected = {
  schema: "sel-screening-expected/1",
  note: "硬筛选黄金表——由 generate/make_sel_screening_golden.mjs 按单元卡 §7.1/§7.2/§8.1/§8.2 冻结语义逐维独立复算产出（阈值容差＝附录 D C7；原因稳定排序＝token 词表序→caseId→atTime；记录序＝候选 modelId 升序×轴 facts 首现序；缺口序＝维度执行序）。每个淘汰项含 actual/required/unit/thresholdSource 全字段（ERR-01）。thresholdSource 为登记呈现词面（DiagCodes/筛选实现单点登记）——对照断言按字面比较。",
  cases,
};
writeFileSync(expectedPath, JSON.stringify(expected, null, 2) + "\n", "utf8");

const totalReasons = cases.reduce((n, c) => n + c.records.reduce((k, r) => k + r.reasons.length, 0), 0);
const totalGaps = cases.reduce((n, c) => n + c.records.reduce((k, r) => k + r.gaps.length, 0), 0);
console.log(`sel-screening-golden: 4 cases, ${cases.reduce((n, c) => n + c.records.length, 0)} records, ${totalReasons} reasons, ${totalGaps} gaps`);
console.log("  anchors:", JSON.stringify(analyticAnchors));
