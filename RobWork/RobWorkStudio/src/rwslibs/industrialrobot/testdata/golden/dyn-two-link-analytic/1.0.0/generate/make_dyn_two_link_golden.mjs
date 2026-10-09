/**
 * make_dyn_two_link_golden.mjs —— 动力学二连杆黄金数据集的独立参考实现（WP-17-T10）。
 *
 * 数据集：dyn-two-link-analytic（kind=analytic-case——NFR-COR-01"解析算例为
 * 独立正确性依据首选"）。本脚本从 inputs/dyn-two-link-inputs.json 读取唯一
 * 数据源，按平面二连杆拉格朗日封闭式**逐元素标量直算**（非任何动力学库
 * 路径）产出 expected/dyn-two-link-expected.json，与产品实现
 * （dynamics/src/InverseDynamics.cpp 的 RNEA 递归牛顿—欧拉 + Envelope.cpp
 * 统计面，C++ 路径）零共享代码；两侧任一漂移即在黄金对照用例显性失败。
 *
 * 参考实现公式来源（本脚本的唯一权威，禁止引用产品实现行为；推导路线＝
 * 拉格朗日解析力学——与产品 RNEA 牛顿—欧拉递推**不同推导路线**，独立性由
 * 公式来源层面保证；符号约定与 units/dynamics.md §5/T03 解析谱系一致：
 * 关节轴沿 +Y、正 q 使臂尖沿 −Z、重力经基座系投影 gz 单点进入）：
 *   - M11 = m1·c1² + I1y + m2·(L1² + c2² + 2·L1·c2·cos q2) + I2y
 *     M12 = m2·(c2² + L1·c2·cos q2) + I2y；M22 = m2·c2² + I2y
 *     τ_inertia = M(q)·q̈（惯性通道＝零重力零速通道）
 *   - h = m2·L1·c2·sin q2
 *     τ_coriolis = [−h·(2·q̇1·q̇2 + q̇2²), +h·q̇1²]（科氏/离心通道——含交叉项）
 *   - τ_gravity = [gz·(m1·c1·cos q1 + m2·(L1·cos q1 + c2·cos(q1+q2))),
 *                  gz·m2·c2·cos(q1+q2)]（gz＝基座系重力 Z 分量：地面 −9.81、
 *                  倒挂 +9.81——R_x(π)ᵀ·g_world 编译投影，AT-37）
 *   - τ_friction = fv·q̇ + fc·sgn₀(q̇) + bias（黄金模型三元组全零 → 恒 0）
 *   - τ_total = 五分项之和；P = τ_total·q̇（W）；E(t) 逐关节梯形累积
 *     E ← E + ½(P_prev+P_cur)·Δt（首样本 E=0）
 *   - 峰值（§7.2）：六 token（τ⁺=max τ、τ⁻=max(−τ)、|q̇|、|q̈|、P⁺=max P、
 *     P⁻=max(−P)）；并列取时间轴首个；持续时间窗＝峰值样本向两侧扩展的
 *     **位等值**连续 run（无阈值——D-DYN-8；JS `===` 即双精度位等值）
 *   - RMS（§7.3）：√(∫τ²dt / T_cycle)（梯形、含驻留、T=有效行全跨度）
 *   - 能量（§7.5）：E⁺=∫max(P,0)dt、E⁻=∫min(P,0)dt（梯形、含驻留）
 *   - 包络合并（§7.4/T07 登记口径）：工况处理序＝conditionId 字节字典序
 *     升序；逐 token 严格大于才替换（并列取 conditionId 更小工况）；
 *     powerPeak＝逐工况 max(max P, max(−P)) 幅值再跨工况取 max；rmsTau＝
 *     跨工况 max（NaN 不参与）；contributingConditions＝该关节有 Ok 行的
 *     工况集（处理序升序）；envelopeComplete＝全部来源 Complete
 *
 * 用法：node make_dyn_two_link_golden.mjs（在本目录执行——相对路径定位 inputs）。
 */

import { readFileSync, writeFileSync } from "node:fs";
import { createHash } from "node:crypto";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const here = dirname(fileURLToPath(import.meta.url));
const inputsPath = join(here, "..", "inputs", "dyn-two-link-inputs.json");
const expectedPath = join(here, "..", "expected", "dyn-two-link-expected.json");

const inputs = JSON.parse(readFileSync(inputsPath, "utf8"));

// =====================================================================
// 独立参考实现（标量公式——与产品 C++ 零共享）
// =====================================================================

/** 工况 id 的派生（与消费测试同一约定：SHA-256(seed) 前 16 字节——测试
 *  侧 core::ContentDigester 同源标准算法；包络并列决胜按字节字典序）。 */
function conditionIdHex(seed) {
  return createHash("sha256").update(seed, "utf8").digest().subarray(0, 16)
    .toString("hex");
}

/** 梯形积分（逐对相邻样本区间——与产品统计面对采样网格同口径）。 */
function trapezoid(ts, ys) {
  let sum = 0;
  for (let i = 1; i < ts.length; ++i) {
    sum += 0.5 * (ys[i - 1] + ys[i]) * (ts[i] - ts[i - 1]);
  }
  return sum;
}

/**
 * 单样本封闭式动力学（平面二连杆——文件头公式直写；返回逐关节分项）。
 * @param m 模型（质量/质心/惯量/杆长/摩擦）
 * @param gz 基座系重力 Z 分量（m/s²——地面 −9.81 / 倒挂 +9.81）
 * @param q qd qdd 逐关节列（rad、rad/s、rad/s²）
 */
function rneaReference(m, gz, q, qd, qdd) {
  const [m1, m2] = [m.links[0].massKg, m.links[1].massKg];
  const [c1, c2] = [m.links[0].comFromJointM, m.links[1].comFromJointM];
  const [i1, i2] = [m.links[0].inertiaYyAboutCom, m.links[1].inertiaYyAboutCom];
  const L1 = m.link1LengthM;
  const out = [];
  for (let j = 0; j < 2; ++j) {
    const cosQ2 = Math.cos(q[1]);
    const M11 = m1 * c1 * c1 + i1 + m2 * (L1 * L1 + c2 * c2 + 2 * L1 * c2 * cosQ2) + i2;
    const M12 = m2 * (c2 * c2 + L1 * c2 * cosQ2) + i2;
    const M22 = m2 * c2 * c2 + i2;
    const grav = j === 0
      ? gz * (m1 * c1 * Math.cos(q[0]) + m2 * (L1 * Math.cos(q[0]) + c2 * Math.cos(q[0] + q[1])))
      : gz * m2 * c2 * Math.cos(q[0] + q[1]);
    const inertia = j === 0 ? M11 * qdd[0] + M12 * qdd[1] : M12 * qdd[0] + M22 * qdd[1];
    const h = m2 * L1 * c2 * Math.sin(q[1]);
    const cori = j === 0 ? -h * (2 * qd[0] * qd[1] + qd[1] * qd[1]) : h * qd[0] * qd[0];
    const sgn0 = qd[j] > 0 ? 1 : qd[j] < 0 ? -1 : 0; // sgn₀(0)=0（D-DYN-7）
    const fric = m.friction.provided
      ? m.friction.viscous * qd[j] + m.friction.coulomb * sgn0 + m.friction.bias
      : 0; // 摩擦缺失＝分项按 0 继续（MDL-16/DYN-06——降级标记由产品面承载）
    const total = grav + inertia + cori + fric + 0; // 外力项 R1 恒 0（P-DYN-3）
    out.push({ tauGravity: grav, tauInertia: inertia, tauCoriolisCentrifugal: cori,
               tauFriction: fric, tauExternal: 0, tauTotal: total });
  }
  return out;
}

/** 六 token 统计量取值（与单元卡 §7.2 枚举序一致——位等值 run 的比较量）。 */
function statValue(row, token) {
  switch (token) {
    case 0: return row.tauTotal;           // τ⁺＝max(τ)（带符号实际值）
    case 1: return -row.tauTotal;          // τ⁻＝max(−τ)（反向幅值形态）
    case 2: return Math.abs(row.qd);       // max|q̇|
    case 3: return Math.abs(row.qdd);      // max|q̈|
    case 4: return row.mechanicalPower;    // P⁺＝max(P)
    case 5: return -row.mechanicalPower;   // P⁻＝max(−P)
    default: throw new Error("token 越界");
  }
}

/** 单算例参考实现：模型＋gz＋样本 → 逐样本行＋统计面（峰值/RMS/能量）。 */
function mapCase(m, gz, cs, condHex) {
  const rows = cs.samples.map((s) => {
    const parts = rneaReference(m, gz, s.q, s.qd, s.qdd);
    return {
      t: s.t, segmentIndex: s.segment,
      joints: parts.map((p, j) => ({
        jointIndex: j, q: s.q[j], qd: s.qd[j], qdd: s.qdd[j],
        ...p,
        mechanicalPower: p.tauTotal * s.qd[j], // P=τ_total·q̇（W——§7.5）
      })),
    };
  });
  // 能量积分状态（逐关节梯形累积——首样本 E=0；与产品评估器同口径）。
  for (let j = 0; j < 2; ++j) {
    let e = 0;
    let prev = 0;
    rows.forEach((r, i) => {
      const p = r.joints[j].mechanicalPower;
      if (i > 0) {
        e += 0.5 * (prev + p) * (r.t - rows[i - 1].t);
      }
      prev = p;
      r.joints[j].energyIntegralJ = e; // J（净能量）
    });
  }
  // 峰值（§7.2——每关节 6 token：严格大于替换取首个；窗＝位等值 run）。
  const peaks = [];
  const rms = [];
  for (let j = 0; j < 2; ++j) {
    const jointRows = rows; // 全部样本行（时间升序——inputs 冻结；全 Ok 无剔除）
    for (let token = 0; token < 6; ++token) {
      const val = (r) => statValue(r.joints[j], token);
      let best = 0;
      for (let k = 1; k < jointRows.length; ++k) {
        if (val(jointRows[k]) > val(jointRows[best])) {
          best = k; // 严格大于——并列保留时间轴首个（确定性口径）
        }
      }
      let lo = best;
      let hi = best;
      while (lo > 0 && val(jointRows[lo - 1]) === val(jointRows[best])) --lo; // 位等值扩展
      while (hi + 1 < jointRows.length && val(jointRows[hi + 1]) === val(jointRows[best])) ++hi;
      peaks.push({ jointIndex: j, token, value: val(jointRows[best]),
                   tPeakS: jointRows[best].t,
                   segmentIndex: jointRows[best].segmentIndex,
                   windowStartS: jointRows[lo].t, windowEndS: jointRows[hi].t,
                   conditionIdHex: condHex }); // 来源工况（包络合并归因对账面）
    }
    // RMS（§7.3——τ_total² 梯形时间加权、T=有效行全跨度、含驻留）。
    const ts = jointRows.map((r) => r.t);
    const tau2 = jointRows.map((r) => r.joints[j].tauTotal * r.joints[j].tauTotal);
    const cycle = ts[ts.length - 1] - ts[0];
    rms.push({ jointIndex: j, rmsTau: Math.sqrt(trapezoid(ts, tau2) / cycle) });
  }
  // 能量分项（§7.5——梯形、含驻留；powerPeak＝幅值形态首位等值窗）。
  const energyJoints = [];
  for (let j = 0; j < 2; ++j) {
    const ts = rows.map((r) => r.t);
    const ps = rows.map((r) => r.joints[j].mechanicalPower);
    const ePos = trapezoid(ts, ps.map((p) => (p > 0 ? p : 0)));
    const eNeg = trapezoid(ts, ps.map((p) => (p < 0 ? p : 0)));
    // 幅值峰值（max(|P|)——并列取首个；窗＝幅值位等值 run，与产品 T04 口径同）。
    const amp = ps.map((p) => (p >= 0 ? p : -p));
    let best = 0;
    for (let k = 1; k < amp.length; ++k) if (amp[k] > amp[best]) best = k;
    let lo = best;
    let hi = best;
    while (lo > 0 && amp[lo - 1] === amp[best]) --lo;
    while (hi + 1 < amp.length && amp[hi + 1] === amp[best]) ++hi;
    energyJoints.push({
      jointIndex: j,
      positiveEnergyJ: ePos, negativeEnergyJ: eNeg, netEnergyJ: ePos + eNeg,
      meanPowerW: (ePos + eNeg) / (ts[ts.length - 1] - ts[0]),
      powerPeak: { value: amp[best], tPeakS: rows[best].t,
                   segmentIndex: rows[best].segmentIndex,
                   windowStartS: rows[lo].t, windowEndS: rows[hi].t },
    });
  }
  return {
    id: cs.id,
    samples: rows,
    stats: {
      plannedSampleCount: cs.samples.length,
      cycleDurationS: rows[rows.length - 1].t - rows[0].t,
      peaks, rmsTau: rms,
      powerEnergy: { joints: energyJoints, includesDwell: true, timeParamAvailable: true },
    },
  };
}

// =====================================================================
// 包络合并参考（§7.4/T07 登记口径——见文件头公式来源末条）
// =====================================================================

function mergeEnvelopeReference(caseIds, perCase) {
  const order = caseIds.slice().sort((a, b) => (a.hex < b.hex ? -1 : a.hex > b.hex ? 1 : 0));
  const joints = [];
  const byIndex = new Map();
  for (const c of order) {
    const st = perCase.get(c.id);
    for (let j = 0; j < 2; ++j) {
      let acc = byIndex.get(j);
      if (!acc) {
        acc = { jointIndex: j, contributing: [], allComplete: true,
                tauMaxPositive: null, tauMaxNegative: null, velocityPeak: null,
                accelerationPeak: null, powerPeak: null, rmsTau: NaN, rmsValid: false };
        byIndex.set(j, acc);
        joints.push(acc);
      }
      const offer = (slot, cand) => {
        if (acc[slot] === null || cand.value > acc[slot].value) acc[slot] = cand;
      };
      const pk = st.stats.peaks.filter((p) => p.jointIndex === j);
      const token = (t) => pk.find((p) => p.token === t);
      offer("tauMaxPositive", token(0));
      offer("tauMaxNegative", token(1));
      offer("velocityPeak", token(2));
      offer("accelerationPeak", token(3));
      const pPos = token(4);
      const pNeg = token(5);
      offer("powerPeak", pPos.value >= pNeg.value ? pPos : pNeg);
      const rms = st.stats.rmsTau.find((r) => r.jointIndex === j).rmsTau;
      if (!Number.isNaN(rms) && (!acc.rmsValid || rms > acc.rmsTau)) {
        acc.rmsTau = rms; // NaN 不参与 max；全无效→NaN（显式无效）
        acc.rmsValid = true;
      }
      acc.contributing.push(c.hex); // 处理序升序追加＝ObjectId 字典序
      acc.allComplete = acc.allComplete && true; // 黄金工况全部 Complete
    }
  }
  return {
    conditionCount: caseIds.length,
    coversAllMandatory: true, // 三工况全部 enabled∧mandatory 且全部在场（EVI-02 呈现参考）
    joints: joints.map((a) => ({
      jointIndex: a.jointIndex,
      tauMaxPositive: a.tauMaxPositive, tauMaxNegative: a.tauMaxNegative,
      velocityPeak: a.velocityPeak, accelerationPeak: a.accelerationPeak,
      powerPeak: a.powerPeak, rmsTau: a.rmsValid ? a.rmsTau : NaN,
      contributingConditionHexes: a.contributing, envelopeComplete: a.allComplete,
    })),
  };
}

// =====================================================================
// 主流程：逐算例参考实现 → 包络合并参考 → 写 expected
// =====================================================================

const m = inputs.model;
const perCase = new Map();
const caseIds = [];
const expectedCases = [];
for (const cs of inputs.cases) {
  const gz = cs.mounting === "inverted" ? -inputs.gravityWorld[2] : inputs.gravityWorld[2];
  const hex = conditionIdHex("dyn-golden-cond-" + cs.id);
  const entry = mapCase(m, gz, cs, hex);
  perCase.set(cs.id, entry);
  caseIds.push({ id: cs.id, hex });
  entry.conditionIdHex = hex;
  expectedCases.push(entry);
}
const envelope = mergeEnvelopeReference(caseIds, perCase);

const expected = {
  schema: "dyn-two-link-expected/1",
  note: "二连杆解析算例黄金期望值——由 generate/make_dyn_two_link_golden.mjs 独立参考实现产出（平面二连杆拉格朗日封闭式 M/C/G 标量直算＋§7.2/§7.3/§7.5 统计口径＋§7.4 包络合并参考，与产品 RNEA/统计 C++ 实现零共享）。锚点（手算第三路——静力矩平衡 r×F）登记于 inputs 各算例 anchors，由消费测试以 EXPECT_NEAR 独立钉扎量级（防参考实现与产品实现两路同错）。",
  cases: expectedCases,
  envelopeMerge: envelope,
};

writeFileSync(expectedPath, JSON.stringify(expected, null, 2) + "\n", "utf8");
console.log(`dyn-two-link-analytic: wrote ${expectedPath}`);
console.log(`  cases: ${expectedCases.map((c) => c.id).join(", ")}`);
console.log(`  condition ids: ${caseIds.map((c) => c.id + "=" + c.hex).join(", ")}`);
