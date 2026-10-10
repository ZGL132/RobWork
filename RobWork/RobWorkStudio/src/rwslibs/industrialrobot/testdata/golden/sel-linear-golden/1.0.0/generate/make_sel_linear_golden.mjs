// sel-linear-golden 黄金期望生成脚本（独立参考实现——node，与产品实现零共享）。
//
// 依据：units/selection.md §5.1/§5.3（导入装配与业务校验八码族冻结语义）、
// §6.2/§6.3（分段线性插值禁外推＋曲线四码）、§10.3/§10.4（淘汰原因词表
// 与稳定排序）、§17.2（SEL-09-S1 直线传动选型层——WP-19-T12）。
// 全部期望值按单元卡文字规则闭式推导（行号＝firstDataRowNo(3)＋数据行序），
// 不引用 selection 产品代码。运行：node generate/make_sel_linear_golden.mjs
// （自仓库 industrialrobot/testdata/golden/sel-linear-golden/1.0.0 目录）。

import { readFileSync, writeFileSync } from "node:fs";
import { createHash } from "node:crypto";

// ---------- CSV 读取（方言行 ird-catalog-v1＋表头＋数据，无引号转义） ----------

function readCsv(rel) {
  const text = readFileSync(new URL(rel, import.meta.url), "utf8");
  const lines = text.split(/\r?\n/).filter((l) => l.length > 0);
  if (lines[0] !== "ird-catalog-v1") throw new Error("方言标识行不符: " + rel);
  const header = lines[1].split(",");
  const rows = lines.slice(2).map((l) => {
    const cells = l.split(",");
    while (cells.length < header.length) cells.push(""); // 行尾空列补齐
    return cells;
  });
  return { header, rows };
}

function col(header, name) {
  const i = header.indexOf(name);
  if (i < 0) throw new Error("缺列: " + name);
  return i;
}

const valid = {
  manifest: JSON.parse(readFileSync(new URL("../inputs/valid/manifest.json", import.meta.url), "utf8")),
  motors: readCsv("../inputs/valid/motors.csv"),
  gearboxes: readCsv("../inputs/valid/gearboxes.csv"),
  curves: readCsv("../inputs/valid/capability_curves.csv"),
  compatibility: readCsv("../inputs/valid/compatibility.csv"),
  linear: readCsv("../inputs/valid/linear_drives.csv"),
};
const LIN_KINDS = ["ball-screw", "rack-pinion", "timing-belt", "linear-motor"];
const QUANTITY_SI = {
  "speed": "rad/s", "torque": "N*m", "power": "W",
  "linear-speed": "m/s", "load": "N", "force": "N",
};
const OWNER_KINDS = ["motor", "gearbox", "linear-drive"];

function num(text) {
  const v = Number(text.trim());
  if (!Number.isFinite(v)) throw new Error("非有限数值: " + text);
  return v;
}

// ---------- ① 装配面期望（§5.1：字段映射→missing 显式清单→排序→status） ----------

const linearCols = valid.linear.header;
const linearDrives = valid.linear.rows.map((r) => {
  const get = (c) => r[col(linearCols, c)].trim();
  const missing = [];
  for (const c of linearCols) {
    // curve_ref 空白≠missing（§6.4"固定额定值口径"合法态——与产品装配
    // 语义一致：装配入口对空 curve_ref 不入缺失清单）。
    if (c === "curve_ref") continue;
    if (get(c) === "" && valid.manifest.fieldDictionary
        .find((d) => d.targetFile === "linear_drives.csv")
        .fields.find((f) => f.column === c).required === false) {
      missing.push({ column: c, reason: "cell-empty" }); // ERR-01 显式标记
    }
  }
  return {
    modelId: get("model_id"),
    vendor: get("vendor"),
    displayName: get("display_name"),
    kind: get("drive_kind"),
    ratedForce: num(get("rated_force_n")),
    peakForce: get("peak_force_n") === "" ? null : num(get("peak_force_n")),
    maxLinearSpeed: num(get("max_speed_ms")),
    ratedPower: num(get("rated_power_w")),
    efficiency: get("efficiency") === "" ? null : num(get("efficiency")),
    stroke: get("stroke_m") === "" ? null : num(get("stroke_m")),
    mass: num(get("mass_kg")),
    mounting: { flangeKind: get("mounting_flange_kind"), shaftKind: get("mounting_shaft_kind") },
    curveRefs: get("curve_ref") === "" ? [] : get("curve_ref").split(";"),
    missing,
    status: missing.length === 0 ? "Valid" : "Partial",
  };
}).sort((a, b) => (a.modelId < b.modelId ? -1 : 1));

// 曲线组（§6.3：组内按 point_index 稳定排序；量纲/单位声明逐组一致）。
const curveCols = valid.curves.header;
const curveGroups = new Map();
for (const r of valid.curves.rows) {
  const id = r[col(curveCols, "curve_id")].trim();
  if (!curveGroups.has(id)) {
    curveGroups.set(id, {
      curveId: id,
      ownerKind: r[col(curveCols, "owner_kind")].trim(),
      ownerModelId: r[col(curveCols, "owner_model_id")].trim(),
      xQuantity: r[col(curveCols, "x_quantity")].trim(),
      xUnit: r[col(curveCols, "x_unit")].trim(),
      yQuantity: r[col(curveCols, "y_quantity")].trim(),
      yUnit: r[col(curveCols, "y_unit")].trim(),
      points: [],
    });
  }
  curveGroups.get(id).points.push({
    x: num(r[col(curveCols, "x_value")]),
    y: num(r[col(curveCols, "y_value")]),
    pointIndex: Number(r[col(curveCols, "point_index")].trim()),
    rowNo: 3 + valid.curves.rows.indexOf(r),
  });
}
const curves = [...curveGroups.values()].map((g) => ({
  curveId: g.curveId, ownerKind: g.ownerKind, ownerModelId: g.ownerModelId,
  xQuantity: g.xQuantity, xUnit: g.xUnit, yQuantity: g.yQuantity, yUnit: g.yUnit,
  points: g.points.sort((a, b) => a.pointIndex - b.pointIndex)
    .map((p) => ({ x: p.x, y: p.y })),
})).sort((a, b) => (a.curveId < b.curveId ? -1 : 1));

// 旋转面简表（v2 包既有四表照常装配——旋转链行为零变化的黄金证据）。
const motorCols = valid.motors.header;
const motors = valid.motors.rows.map((r) => ({
  modelId: r[col(motorCols, "model_id")].trim(),
  ratedTorque: num(r[col(motorCols, "rated_torque_nm")]),
  peakTorque: num(r[col(motorCols, "peak_torque_nm")]),
  mass: num(r[col(motorCols, "mass_kg")]),
}));
const gearboxCols = valid.gearboxes.header;
const gearboxes = valid.gearboxes.rows.map((r) => ({
  modelId: r[col(gearboxCols, "model_id")].trim(),
  ratio: num(r[col(gearboxCols, "ratio")]),
  efficiency: num(r[col(gearboxCols, "efficiency")]),
}));

// ---------- ② 误例校验报告期望（§5.3 八码族＋§6.3 四码族闭式推导） ----------

// 变异表组装：目标表被替换，其余四表取 valid 同款；逐规则推导 issue。
function issue(code, file, rowNo, column, modelId, extra) {
  return { code, file, rowNo, column, modelId, ...extra };
}

function linearRowsOf(rel) {
  const t = rel ? readCsv("../inputs/invalid/" + rel) : valid.linear;
  return t.rows.map((r) => {
    const o = {};
    for (const c of t.header) o[c] = r[col(t.header, c)].trim();
    return o;
  });
}

function validateLinearTable(rows, fileName) {
  const issues = [];
  // 校验报告的 file 定位＝表的标准文件名（ParsedFileTable.fileName——变异
  // 表只是测试组装方式，校验器视角的文件名恒为 kCatalogFileLinearDrives）。
  const file = "linear_drives.csv";
  const seen = new Map(); // modelId → 首行签名
  rows.forEach((o, idx) => {
    const rowNo = 3 + idx;
    const sig = JSON.stringify(o);
    if (o.model_id === "") {
      issues.push(issue("SEL-CATALOG-FIELD-MISSING", file, rowNo, "model_id", "", {}));
      return;
    }
    if (seen.has(o.model_id)) {
      const identical = seen.get(o.model_id) === sig;
      issues.push(issue(identical ? "SEL-CATALOG-DUPLICATE-MODEL" : "SEL-CATALOG-DUPLICATE-ID",
        file, rowNo, "model_id", o.model_id, {}));
      return;
    }
    seen.set(o.model_id, sig);
    // 数值范围（§5.3 行 5——逐列谓词按卡面语义闭式展开）。
    const range = (cl, v, pass, bound, unit) => {
      if (!pass) {
        issues.push(issue("SEL-CATALOG-RANGE-INVALID", file, rowNo, cl, o.model_id,
          { actualValue: v, expectedValue: bound, unit }));
      }
    };
    if (o.rated_force_n !== "") range("rated_force_n", num(o.rated_force_n), num(o.rated_force_n) > 0, 0, "N");
    if (o.peak_force_n !== "") range("peak_force_n", num(o.peak_force_n), num(o.peak_force_n) > 0, 0, "N");
    if (o.max_speed_ms !== "") range("max_speed_ms", num(o.max_speed_ms), num(o.max_speed_ms) > 0, 0, "m/s");
    if (o.rated_power_w !== "") range("rated_power_w", num(o.rated_power_w), num(o.rated_power_w) > 0, 0, "W");
    if (o.efficiency !== "") range("efficiency", num(o.efficiency), num(o.efficiency) > 0 && num(o.efficiency) <= 1, 1, "1");
    if (o.stroke_m !== "") range("stroke_m", num(o.stroke_m), num(o.stroke_m) > 0, 0, "m");
    if (o.mass_kg !== "") range("mass_kg", num(o.mass_kg), num(o.mass_kg) > 0, 0, "kg");
    // 交叉范围：峰值推力 ≥ 额定推力。
    if (o.rated_force_n !== "" && o.peak_force_n !== "") {
      range("peak_force_n", num(o.peak_force_n), num(o.peak_force_n) >= num(o.rated_force_n),
        num(o.rated_force_n), "N");
    }
    // 词表（§5.3 行 1 族——drive_kind 四类封闭词表）。
    if (!LIN_KINDS.includes(o.drive_kind)) {
      issues.push(issue("SEL-CATALOG-SCHEMA-MISMATCH", file, rowNo, "drive_kind", o.model_id,
        { actualText: o.drive_kind, expectedText: "ball-screw|rack-pinion|timing-belt|linear-motor" }));
    }
    // 必填缺失（可缺失列除外——ERR-01 显式清单不发码）。
    for (const f of valid.manifest.fieldDictionary.find((d) => d.targetFile === "linear_drives.csv").fields) {
      if (f.required && o[f.column] === "") {
        issues.push(issue("SEL-CATALOG-FIELD-MISSING", file, rowNo, f.column, o.model_id, {}));
      }
    }
  });
  return issues;
}

// 曲线表变异的规则推导（owner 存在性/单位量纲匹配/组内 x 升序）。
function validateCurveTable(rows) {
  const issues = [];
  const linearIds = new Set(linearRowsOf(null).map((o) => o.model_id));
  const motorIds = new Set(valid.motors.rows.map((r) => r[col(motorCols, "model_id")].trim()));
  const gearboxIds = new Set(valid.gearboxes.rows.map((r) => r[col(gearboxCols, "model_id")].trim()));
  rows.forEach((r, idx) => {
    const rowNo = 3 + idx;
    const get = (c) => r[col(curveCols, c)].trim();
    // ① owner 词表与存在性（§5.3 行 6 语义层——v2 词表含 linear-drive）。
    const okOwner = OWNER_KINDS.includes(get("owner_kind"));
    const ownerTable = get("owner_kind") === "motor" ? motorIds
      : get("owner_kind") === "gearbox" ? gearboxIds : linearIds;
    if (!okOwner || !ownerTable.has(get("owner_model_id"))) {
      issues.push(issue("SEL-CATALOG-REF-DANGLING", "capability_curves.csv", rowNo,
        "owner_model_id", get("owner_model_id"), { actualText: get("owner_model_id") }));
      return; // 悬空行不进组
    }
    // ② 单位与量纲匹配（v2 词表——linear-speed/load/force）。
    for (const [qCol, uCol] of [["x_quantity", "x_unit"], ["y_quantity", "y_unit"]]) {
      const expectedSi = QUANTITY_SI[get(qCol)];
      if (expectedSi === undefined) {
        issues.push(issue("SEL-CATALOG-SCHEMA-MISMATCH", "capability_curves.csv", rowNo, qCol,
          get("curve_id"), { actualText: get(qCol), expectedText: "speed|torque|power|linear-speed|load|force" }));
      } else if (get(uCol) !== expectedSi) {
        issues.push(issue("SEL-CATALOG-UNIT-INVALID", "capability_curves.csv", rowNo, uCol,
          get("curve_id"), { actualText: get(uCol), expectedText: expectedSi }));
      }
    }
  });
  // ③ 组内混装（首行声明基准——后继行 owner/量纲/单位不一致）。
  const groups = new Map();
  rows.forEach((r, idx) => {
    const get = (c) => r[col(curveCols, c)].trim();
    const id = get("curve_id");
    if (!groups.has(id)) groups.set(id, []);
    groups.get(id).push({
      rowNo: 3 + idx, ownerKind: get("owner_kind"), ownerModelId: get("owner_model_id"),
      xQuantity: get("x_quantity"), yQuantity: get("y_quantity"),
      xUnit: get("x_unit"), yUnit: get("y_unit"),
      x: num(get("x_value")),
    });
  });
  for (const [id, g] of groups) {
    const first = g[0];
    for (let i = 1; i < g.length; ++i) {
      const r = g[i];
      if (r.ownerKind !== first.ownerKind || r.ownerModelId !== first.ownerModelId
          || r.xQuantity !== first.xQuantity || r.yQuantity !== first.yQuantity
          || r.xUnit !== first.xUnit || r.yUnit !== first.yUnit) {
        issues.push(issue("SEL-CATALOG-SCHEMA-MISMATCH", "capability_curves.csv", r.rowNo,
          "curve_id", id, {}));
      }
    }
    // ④ x 升序（§6.3 行 1——拒绝不代排序；定位后一点物理行）。
    for (let i = 0; i + 1 < g.length; ++i) {
      if (g[i + 1].x < g[i].x) {
        issues.push(issue("SEL-CURVE-UNORDERED", "capability_curves.csv", g[i + 1].rowNo,
          "x_value", id, {}));
      } else if (g[i + 1].x === g[i].x) {
        issues.push(issue("SEL-CURVE-DUP-X", "capability_curves.csv", g[i + 1].rowNo,
          "x_value", id, {}));
      }
    }
  }
  return issues;
}

// 条目 curve_ref 引用语义（存在→owner 匹配；逐条目）。
function validateLinearCurveRefs(rows, groups) {
  const issues = [];
  rows.forEach((o, idx) => {
    if (o.curve_ref === "") return;
    for (const token of o.curve_ref.split(";")) {
      const g = groups.find((x) => x.curveId === token);
      if (g === undefined) {
        issues.push(issue("SEL-CATALOG-REF-DANGLING", "linear_drives.csv", 3 + idx,
          "curve_ref", o.model_id, { actualText: token }));
      } else if (g.ownerKind !== "linear-drive" || g.ownerModelId !== o.model_id) {
        issues.push(issue("SEL-CATALOG-REF-DANGLING", "linear_drives.csv", 3 + idx,
          "curve_ref", o.model_id,
          { actualText: token, expectedText: "linear-drive/" + o.model_id }));
      }
    }
  });
  return issues;
}

function groupDecls(rows) {
  const out = [];
  const seen = new Set();
  rows.forEach((r) => {
    const id = r[col(curveCols, "curve_id")].trim();
    if (seen.has(id)) return;
    seen.add(id);
    out.push({
      curveId: id,
      ownerKind: r[col(curveCols, "owner_kind")].trim(),
      ownerModelId: r[col(curveCols, "owner_model_id")].trim(),
    });
  });
  return out;
}

function makeCase(name, linearRel, curvesRel) {
  const linearRows = linearRowsOf(linearRel);
  const curveRows = curvesRel ? readCsv("../inputs/invalid/" + curvesRel).rows : valid.curves.rows;
  const decls = groupDecls(curveRows);
  const issues = [
    ...validateLinearTable(linearRows, linearRel ?? "linear_drives.csv"),
    ...(curvesRel ? validateCurveTable(curveRows) : []),
    ...(linearRel ? validateLinearCurveRefs(linearRows, decls) : []),
  ];
  // 报告稳定序（§5.3：file → rowNo → column → code）。
  issues.sort((a, b) => a.file !== b.file ? (a.file < b.file ? -1 : 1)
    : a.rowNo !== b.rowNo ? a.rowNo - b.rowNo
    : a.column !== b.column ? (a.column < b.column ? -1 : 1)
    : a.code < b.code ? -1 : a.code > b.code ? 1 : 0);
  return { name, issues };
}

const invalidCases = [
  makeCase("range-linear.csv", "range-linear.csv", null),
  makeCase("kind-linear.csv", "kind-linear.csv", null),
  makeCase("dup-linear.csv", "dup-linear.csv", null),
  makeCase("missing-linear.csv", "missing-linear.csv", null),
  makeCase("dangling-linear.csv", "dangling-linear.csv", null),
  makeCase("unit-curves-linear.csv", null, "unit-curves-linear.csv"),
  makeCase("unordered-curves-linear.csv", null, "unordered-curves-linear.csv"),
];

// ---------- ③ 筛选黄金（§17.2 直线维度集＋§10.4 稳定排序闭式推导） ----------

// 输入事实与筛选黄金面自 inputs/screening-inputs.json 装载（独立推导在下方）。
const screening = JSON.parse(readFileSync(new URL("../inputs/screening-inputs.json", import.meta.url), "utf8"));
const TOKEN_ORDER = [
  "torque-continuous-insufficient", "torque-peak-insufficient", "speed-insufficient",
  "power-insufficient", "overload-time-insufficient", "duty-mismatch", "voltage-mismatch",
  "thermal-derating-insufficient", "brake-insufficient", "holding-insufficient",
  "safety-factor-insufficient",
  "gearbox-rated-torque-insufficient", "gearbox-peak-torque-insufficient",
  "input-speed-exceeded", "ratio-mismatch", "efficiency-insufficient", "backlash-exceeded",
  "life-insufficient", "mounting-incompatible", "external-load-exceeded",
  "combo-incompatible", "axis-mapping-incomplete", "inertia-ratio-policy-unsettled",
  "identity-mismatch", "catalog-version-incompatible", "mapping-version-incompatible",
  "dynamics-missing", "drivetrain-missing", "case-coverage-gap", "input-invalid",
  "compute-failed", "axis-out-of-scope", "r2-capability-disabled", "user-preference-filtered",
  // T12 批（§17.2——WP-19-T12 表尾追加序）：
  "linear-force-continuous-insufficient", "linear-force-peak-insufficient",
  "linear-speed-insufficient", "linear-power-insufficient",
];
const TOL = { force: 1e-9, linearVelocity: 1e-9, power: 0 }; // 附录 D C7（未声明→0）

function interp(curve, x) {
  // §6.2：闭区间内分段线性；区间外拒绝（EXTRAPOLATION-DENIED——禁外推）。
  const pts = curve.points;
  if (x < pts[0].x || x > pts[pts.length - 1].x) return { rejected: true };
  for (let i = 0; i + 1 < pts.length; ++i) {
    if (x >= pts[i].x && x <= pts[i + 1].x) {
      const t = (x - pts[i].x) / (pts[i + 1].x - pts[i].x);
      return { value: pts[i].y + t * (pts[i + 1].y - pts[i].y) };
    }
  }
  return { rejected: true };
}

function exceeds(actual, limit, tol) {
  return actual > limit + tol;
}

const records = screening.cases.flatMap((c) => {
  const facts = c.factsByAxis[c.axis];
  const merged = {}; // 同轴多工况合并（记录粒度＝候选×轴）
  for (const drive of screening.drives) {
    const reasons = [];
    const gaps = [];
    for (const f of facts) {
      const sf = c.safetyFactor ?? 1;
      // 维度 1：连续推力。
      if (f.forceRms == null) {
        gaps.push({ dimension: "linear-force-rms", caseId: f.caseId });
      } else if (exceeds(f.forceRms * sf, drive.ratedForce, TOL.force)) {
        reasons.push({ token: "linear-force-continuous-insufficient", caseId: f.caseId,
          actual: f.forceRms * sf, required: drive.ratedForce, unit: "N",
          thresholdSource: "目录 rated_force_n（额定连续推力）" + (sf > 1 ? "——含安全系数复判" : "") });
      }
      // 维度 2：峰值推力（额定口径）。
      if (f.forcePeak == null) {
        gaps.push({ dimension: "linear-force-peak", caseId: f.caseId });
      } else {
        const demanded = f.forcePeak * sf;
        if (exceeds(demanded, drive.peakForce, TOL.force)) {
          reasons.push({ token: "linear-force-peak-insufficient", caseId: f.caseId,
            actual: demanded, required: drive.peakForce, unit: "N",
            thresholdSource: "目录 peak_force_n（峰值推力）" + (sf > 1 ? "——含安全系数复判" : "") });
        }
        // 维度 3：曲线口径（声明推力-速度曲线时——查询点＝峰值速度）。
        const curveId = drive.curveRefs.find((id) =>
          curves.find((g) => g.curveId === id && g.xQuantity === "linear-speed" && g.yQuantity === "force"));
        if (curveId !== undefined) {
          if (f.linearSpeedPeak == null) {
            gaps.push({ dimension: "linear-force-curve", caseId: f.caseId,
              detail: "曲线口径峰值推力维度缺查询点" });
          } else {
            const g = curves.find((x) => x.curveId === curveId);
            const r = interp(g, f.linearSpeedPeak);
            if (r.rejected) {
              gaps.push({ dimension: "linear-force-curve", caseId: f.caseId,
                diagCode: "SEL-CURVE-EXTRAPOLATION-DENIED" });
            } else if (exceeds(demanded, r.value, TOL.force)) {
              reasons.push({ token: "linear-force-peak-insufficient", caseId: f.caseId,
                actual: demanded, required: r.value, unit: "N",
                thresholdSource: "能力曲线 " + curveId + "（linear-speed→force，工作点速度查询）"
                  + (sf > 1 ? "——含安全系数复判" : "") });
            }
          }
        }
      }
      // 维度 4：直线速度。
      if (f.linearSpeedPeak == null) {
        gaps.push({ dimension: "linear-speed", caseId: f.caseId });
      } else if (exceeds(f.linearSpeedPeak * sf, drive.maxLinearSpeed, TOL.linearVelocity)) {
        reasons.push({ token: "linear-speed-insufficient", caseId: f.caseId,
          actual: f.linearSpeedPeak * sf, required: drive.maxLinearSpeed, unit: "m/s",
          thresholdSource: "目录 max_speed_ms（最高直线速度）" + (sf > 1 ? "——含安全系数复判" : "") });
      }
      // 维度 5：直线功率（峰值/RMS 两子项独立）。
      for (const [k, dim] of [["powerPeak", "linear-power-peak"], ["powerRms", "linear-power-rms"]]) {
        if (f[k] == null) {
          gaps.push({ dimension: dim, caseId: f.caseId });
        } else if (exceeds(f[k] * sf, drive.ratedPower, TOL.power)) {
          reasons.push({ token: "linear-power-insufficient", caseId: f.caseId,
            actual: f[k] * sf, required: drive.ratedPower, unit: "W",
            thresholdSource: "目录 rated_power_w（额定功率）" + (sf > 1 ? "——含安全系数复判" : "") });
        }
      }
    }
    // 原因稳定排序（§10.4：token 词表序 → 工况 ID → 时刻；stable 保留产生序）。
    reasons.sort((a, b) => TOKEN_ORDER.indexOf(a.token) - TOKEN_ORDER.indexOf(b.token)
      || (a.caseId < b.caseId ? -1 : a.caseId > b.caseId ? 1 : 0));
    const rec = {
      id: drive.modelId + "|" + screening.axes[c.axis],
      candidateModelId: drive.modelId,
      axis: c.axis,
      verdict: reasons.length > 0 ? "Rejected" : (gaps.length > 0 ? "DataInsufficient" : "Feasible"),
      reasons,
      gaps,
    };
    merged[drive.modelId] = rec;
  }
  // case 内输出序＝快照遍历序（modelId 升序——NFR-COR-02 确定序钉扎；
  // 记录集＝快照全量直线器件×该轴——screenLinearDrives 对快照全量候选
  // 遍历，cases.drives 仅是黄金对照的算例说明面）。
  return Object.keys(merged).sort().map((id) => merged[id]);
});

// ---------- 输出 ----------

const sha256 = (rel) => createHash("sha256").update(readFileSync(new URL(rel, import.meta.url))).digest("hex");

const catalogExpected = {
  schema: "sel-linear-catalog-expected/1",
  note: "直线传动目录模板黄金期望——由 generate/make_sel_linear_golden.mjs 按单元卡 §4.1/§5.1/§5.3/§6.3/§17.2 冻结语义封闭推导（行号＝firstDataRowNo(3)＋数据行序），与产品实现零共享。identity.contentIdentity 为产品装配入口计算面（§4.2）——输入侧全零占位。",
  valid: { linearDrives, curves, motors, gearboxes },
  invalidCases,
};

const screeningExpected = {
  schema: "sel-linear-screening-expected/1",
  note: "直线传动筛选黄金期望——§17.2 维度集（连续/峰值推力＋曲线口径＋速度＋功率＋SF 复判）与 §10.4 稳定排序的闭式推导；插值锚点：bs-force 0.65 m/s→10500 N（0.5→0.8 段）、lm-force 0.65 m/s→10550 N（0.5→1.0 段）、lm-force 0.9 m/s→9800 N；区间外（0.9 m/s 对 bs-force 上界 0.8）＝EXTRAPOLATION-DENIED 数据缺口——禁外推不放宽。",
  records,
};

writeFileSync(new URL("../expected/linear-catalog-expected.json", import.meta.url),
  JSON.stringify(catalogExpected, null, 2) + "\n");
writeFileSync(new URL("../expected/linear-screening-expected.json", import.meta.url),
  JSON.stringify(screeningExpected, null, 2) + "\n");

// integrity 表（数据集 manifest 由本脚本尾注提示手工重跑登记——sha256 现算打印）。
const files = [
  "inputs/valid/manifest.json", "inputs/valid/motors.csv", "inputs/valid/gearboxes.csv",
  "inputs/valid/capability_curves.csv", "inputs/valid/compatibility.csv",
  "inputs/valid/linear_drives.csv",
  "inputs/invalid/range-linear.csv", "inputs/invalid/kind-linear.csv",
  "inputs/invalid/dup-linear.csv", "inputs/invalid/missing-linear.csv",
  "inputs/invalid/dangling-linear.csv", "inputs/invalid/unit-curves-linear.csv",
  "inputs/invalid/unordered-curves-linear.csv",
  "inputs/screening-inputs.json",
  "expected/linear-catalog-expected.json", "expected/linear-screening-expected.json",
];
console.log("integrity:");
for (const f of files) {
  console.log(`  { "path": "${f}", "sha256": "${sha256("../" + f)}", "sizeBytes": ${readFileSync(new URL("../" + f, import.meta.url)).length} },`);
}
console.log("done: expected/linear-catalog-expected.json + expected/linear-screening-expected.json");
