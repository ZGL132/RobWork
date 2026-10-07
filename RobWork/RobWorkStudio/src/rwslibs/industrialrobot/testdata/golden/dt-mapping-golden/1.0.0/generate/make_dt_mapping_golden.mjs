/**
 * make_dt_mapping_golden.mjs —— 传动映射黄金数据集的独立参考实现（WP-18-T04）。
 *
 * 数据集：dt-mapping-golden（kind=analytic-case——NFR-COR-01"解析算例为独立
 * 正确性依据首选"）。本脚本从 inputs/dt-mapping-inputs.json 读取唯一数据源，
 * 按 units/drivetrain.md 冻结公式**逐元素标量直算**（非矩阵库路径——§14.1
 * 独立参考实现纪律）产出 expected/dt-mapping-expected.json，与产品实现
 * （drivetrain/src/MappingCore.cpp，C++ 路径）零共享代码；两侧任一漂移即在
 * 黄金对照用例显性失败。
 *
 * 参考实现公式来源（本脚本的唯一权威，禁止引用产品实现行为）：
 *   - §6.2 R1 对角映射表：θ=θ_off+q/c；θ̇=q̇/c；θ̈=q̈/c；τ_ideal=c·τ_j；
 *     τ_m=τ_ideal+J_rotor·θ̈；P_joint=τ_j·q̇；P_rotor=J·θ̈·θ̇
 *   - §10.2 效率方向折算：P_joint>0 → P_trans=P_joint/η⁺；<0 → P_trans=
 *     P_joint·η⁻；=0（精确零）→ P_trans=0 且效率不适用
 *   - §10.3：P_motor=P_trans+P_rotor
 *   - §10.4：RMS(x)=√(∫x²dt/T)（梯形、含驻留；峰值带时刻/段）
 *   - §10.5：能量分项全部梯形积分；效率不适用样本全列置 0 进入积分
 *     （§8.3 零功率剔除语义——与单元卡登记口径一致）
 *   - §10.6 四象限：ω=θ̇、P_quad=τ_m·θ̇ 符号组合；零速/零功率/效率不适用
 *     → 零速桶（只计时间占比与计数）；象限能量＝样本区间 ∫P_joint dt
 *     （左矩形约定——单元卡实现注记）
 *   - §9.2/§9.5：J_reflected=J_rotor/c²；惯量比=c²·J_load@joint/J_rotor
 *
 * 用法：node make_dt_mapping_golden.mjs（在本目录执行——相对路径定位 inputs）。
 */

import { readFileSync, writeFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const here = dirname(fileURLToPath(import.meta.url));
const inputsPath = join(here, "..", "inputs", "dt-mapping-inputs.json");
const expectedPath = join(here, "..", "expected", "dt-mapping-expected.json");

const inputs = JSON.parse(readFileSync(inputsPath, "utf8"));

// =====================================================================
// 独立参考实现（标量公式——与产品 C++ 零共享）
// =====================================================================

/** 梯形积分（§10.5——逐对区间，缺样本区间不外推）。 */
function trapezoid(ts, ys) {
  let sum = 0;
  for (let i = 1; i < ts.length; ++i) {
    sum += 0.5 * (ys[i - 1] + ys[i]) * (ts[i] - ts[i - 1]);
  }
  return sum;
}

/** 单算例参考实现：模型＋序列 → 逐样本映射＋工作点统计（§6.2/§10 全列）。 */
function mapCase(model, cs) {
  const n = model.ratios.length;
  const perAxis = [];
  for (let k = 0; k < n; ++k) {
    const c = model.ratios[k]; // 无量纲（c＝Δq_joint/Δθ_motor）
    const off = model.zeroOffset[k]; // rad（电机零位偏置）
    const jRot = model.rotorInertia[k]; // kg·m²（电机轴系）
    const etaF = model.etaForward[k]; // 无量纲 (0,1]
    const etaB = model.etaBackward[k]; // 无量纲 (0,1]
    const samples = cs.samples.map((s) => {
      // ---- §6.2 逐样本映射（封闭代数——无迭代无近似）。
      const theta = off + s.q / c; // rad
      const thetaDot = s.qd / c; // rad/s
      const thetaDDot = s.qdd / c; // rad/s²
      const tauIdeal = c * s.tauJoint; // N·m（虚功对偶对角形）
      const tauMotor = tauIdeal + jRot * thetaDDot; // N·m（M-12 全量口径）
      const pJoint = s.tauJoint * s.qd; // W（逐元素 τ·q̇）
      const pRotor = jRot * thetaDDot * thetaDot; // W（转子分项）
      // ---- §10.2 方向折算（精确符号判据——无阈值）。
      let pTransmission = 0;
      let efficiencyApplicable = false;
      if (s.tauJoint * s.qd > 0) {
        pTransmission = pJoint / etaF; // 驱动方向
        efficiencyApplicable = true;
      } else if (s.tauJoint * s.qd < 0) {
        pTransmission = pJoint * etaB; // 再生方向
        efficiencyApplicable = true;
      }
      const pMotor = pTransmission + pRotor; // W（§10.3 总机械功率）
      return {
        t: s.t,
        theta,
        thetaDot,
        thetaDDot,
        tauIdeal,
        tauMotor,
        pJoint,
        pTransmission,
        pRotor,
        pMotor,
        efficiencyApplicable,
      };
    });
    perAxis.push(samples);
  }

  // ---- 循环完整性（§10.4 数据不足行——本数据集全部算例序列≥2 且递增）。
  const ts = cs.samples.map((s) => s.t);
  const cycleT = ts[ts.length - 1] - ts[0];

  // ---- 工作点统计（§10.4/§10.6——逐轴）。
  const points = perAxis.map((samples, k) => {
    const rated = model.ratedTorque?.[k] ?? null;

    // 峰值（§10.4——正/负 τ 分列；|θ̇|、|P| 取带符号实测值；扫描首样本起）。
    let iMax = 0, iMin = 0, iOmega = 0, iPower = 0;
    for (let i = 1; i < samples.length; ++i) {
      if (samples[i].tauMotor > samples[iMax].tauMotor) iMax = i;
      if (samples[i].tauMotor < samples[iMin].tauMotor) iMin = i;
      if (Math.abs(samples[i].thetaDot) > Math.abs(samples[iOmega].thetaDot)) iOmega = i;
      if (Math.abs(samples[i].pMotor) > Math.abs(samples[iPower].pMotor)) iPower = i;
    }
    const peak = (i, value, kind) => ({
      present: kind === "pos" ? value > 0 : kind === "neg" ? value < 0 : true,
      value,
      t: samples[i].t,
      segmentId: cs.samples[i].segmentId,
    });

    // RMS（§10.4——τ_m²/θ̇² 梯形含驻留，分母＝完整循环时长）。
    const tauRms = Math.sqrt(
      trapezoid(ts, samples.map((s) => s.tauMotor * s.tauMotor)) / cycleT,
    );
    const omegaRms = Math.sqrt(
      trapezoid(ts, samples.map((s) => s.thetaDot * s.thetaDot)) / cycleT,
    );
    const loadRatio = rated !== null && rated > 0 ? tauRms / rated : null;
    const reflectedInertia = model.rotorInertia[k] / (model.ratios[k] ** 2);

    // 能量分项（§10.5——效率不适用样本全列置 0 后梯形）。
    const col = (pick) => samples.map((s) => (s.efficiencyApplicable ? pick(s) : 0));
    const energy = {
      eMotor: trapezoid(ts, col((s) => s.pMotor)),
      eJoint: trapezoid(ts, col((s) => s.pJoint)),
      eLoss: trapezoid(ts, col((s) => s.pTransmission - s.pJoint)),
      eRegen: trapezoid(ts, col((s) => Math.max(0, -s.pJoint))),
      ePos: trapezoid(ts, col((s) => Math.max(0, s.pJoint))),
      eRotor: trapezoid(ts, col((s) => s.pRotor)),
    };

    // 四象限（§10.6——P_quad=τ_m·θ̇；区间归属左样本，末样本 span=0）。
    const quad = () => ({ sampleCount: 0, timeShare: 0, energy: 0 });
    const q = { q1: quad(), q2: quad(), q3: quad(), q4: quad(), zeroDwell: quad() };
    for (let i = 0; i < samples.length; ++i) {
      const s = samples[i];
      const span = i + 1 < samples.length ? samples[i + 1].t - s.t : 0;
      const pQuad = s.tauMotor * s.thetaDot; // W（象限判定口径——§10.6 表头）
      const zero = s.thetaDot === 0 || pQuad === 0 || !s.efficiencyApplicable;
      let target;
      if (zero) {
        target = q.zeroDwell;
      } else if (s.thetaDot > 0 && pQuad > 0) {
        target = q.q1;
      } else if (s.thetaDot > 0 && pQuad < 0) {
        target = q.q2;
      } else if (s.thetaDot < 0 && pQuad < 0) {
        target = q.q3;
      } else {
        target = q.q4;
      }
      target.sampleCount += 1;
      target.timeShare += span / cycleT;
      if (!zero) {
        target.energy += s.pJoint * span; // J（左矩形——实现注记口径）
      }
    }

    return {
      tauRms,
      omegaRms,
      loadRatio,
      reflectedInertia,
      tauPeakPos: peak(iMax, samples[iMax].tauMotor, "pos"),
      tauPeakNeg: peak(iMin, samples[iMin].tauMotor, "neg"),
      omegaPeak: peak(iOmega, samples[iOmega].thetaDot, "signed"),
      powerPeak: peak(iPower, samples[iPower].pMotor, "signed"),
      q1: q.q1,
      q2: q.q2,
      q3: q.q3,
      q4: q.q4,
      zeroDwell: q.zeroDwell,
      energy,
    };
  });

  return { samples: perAxis, points };
}

// =====================================================================
// 逐算例期望值生成
// =====================================================================

const expectedCases = [];
const expectedInertia = [];
for (const cs of inputs.cases) {
  const model = inputs.models[cs.model];
  if (!model) {
    throw new Error(`算例 ${cs.id} 引用未知模型 ${cs.model}`);
  }
  const mapped = mapCase(model, cs);

  const entry = { id: cs.id, samples: mapped.samples, points: mapped.points };

  // AT-38 正命题面（virtualwork-multi）：把对角映射按"常矩阵 C"的矩阵语义
  // 重算一遍（τ_m＝Cᵀ·τ_j、θ̇＝C⁻¹·q̇——完整双重循环形式），与逐轴标量
  // 路径互为独立参考；消费测试断言产品输出同时与两路一致（"无耦合链等价
  // 于对角传动比映射"——DYN-04/M-12 的黄金验证口径）。
  if (cs.id === "virtualwork-multi") {
    const n = model.ratios.length;
    const matrixForm = { thetaDot: [], tauIdeal: [], wMotor: [], wJoint: [] };
    for (let i = 0; i < cs.samples.length; ++i) {
      const thetaDotRow = [];
      const tauIdealRow = [];
      const wMotorRow = [];
      const wJointRow = [];
      for (let k = 0; k < n; ++k) {
        // Cᵀ 的第 k 行 τ_m[k]＝Σ_j Cᵀ(k,j)·τ_j[j]＝Σ_j C(j,k)·τ_j[j]
        //（对角形 C(j,k) 非对角恒 0——只有 j==k 贡献一次；显式写出全和
        // 形式以保留"矩阵语义参考实现"的独立性——非逐轴捷径）。
        let tauK = 0;
        let thetaDotK = 0;
        for (let j = 0; j < n; ++j) {
          const cjk = j === k ? model.ratios[k] : 0; // C(j,k)（对角形）
          tauK += cjk * cs.samples[i].tauJoint;
          if (k === j) {
            thetaDotK += cs.samples[i].qd / model.ratios[k]; // C⁻¹ 对角求逆
          }
        }
        tauIdealRow.push(tauK);
        thetaDotRow.push(thetaDotK);
        // 虚功对偶（§8.1 理想口径——逐样本逐元素恒等）。
        wMotorRow.push(tauK * thetaDotK);
        wJointRow.push(cs.samples[i].tauJoint * cs.samples[i].qd);
      }
      matrixForm.thetaDot.push(thetaDotRow);
      matrixForm.tauIdeal.push(tauIdealRow);
      matrixForm.wMotor.push(wMotorRow);
      matrixForm.wJoint.push(wJointRow);
    }
    entry.matrixForm = matrixForm;
  }

  // DT-G8 惯量面：J_reflected＝J_rotor/c²；惯量比＝c²·J_load@joint/J_rotor
  //（§9.5 对角式；负载条目缺失→不适用 null——§10.7 显式标记）。
  if (cs.id === "inertia-golden") {
    const axes = model.ratios.map((c, k) => {
      const hasLoad = cs.loadInertia && k < cs.loadInertia.length;
      return {
        jointIndex: k,
        jReflected: model.rotorInertia[k] / (c * c), // kg·m²（关节轴系）
        inertiaRatio: hasLoad
          ? (c * c * cs.loadInertia[k]) / model.rotorInertia[k]
          : null,
      };
    });
    expectedInertia.push({ id: cs.id, axes });
  }

  expectedCases.push(entry);
}

const expected = {
  schema: "dt-mapping-expected/1",
  note: "传动映射黄金期望值——由 generate/make_dt_mapping_golden.mjs 独立参考实现产出（单元卡 §6.2/§9/§10 冻结公式标量直算，与产品 C++ 实现零共享）。能量分项单位 J（档案通道不承载 J——消费测试以独立积分重算＋恒等式断言复核本文件能量值）。",
  cases: expectedCases,
  inertiaCases: expectedInertia,
};

writeFileSync(expectedPath, JSON.stringify(expected, null, 2) + "\n", "utf8");
console.log(`dt-mapping-golden: wrote ${expectedPath}`);
console.log(`  cases: ${expectedCases.map((c) => c.id).join(", ")}`);
