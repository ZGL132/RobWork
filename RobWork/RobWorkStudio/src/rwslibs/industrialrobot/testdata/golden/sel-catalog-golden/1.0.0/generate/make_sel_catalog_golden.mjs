/**
 * make_sel_catalog_golden.mjs —— selection 目录包黄金数据集的独立参考实现（WP-19-T11）。
 *
 * 数据集：sel-catalog-golden（kind=contract-fixture——AT-08"目录导入/错误字段/版本
 * 锁定"的数据面）。本脚本从内置唯一数据源（黄金目录源 CATALOG）产出：
 *   1. inputs/valid/  —— v1 合法目录包五文件（manifest.json＋四 CSV；CSV 首行为
 *      方言标识行 "ird-catalog-v1"、次行为表头、数据自物理行 3 起——io 卡 §5.6
 *      行号口径，与 selection/test/CatalogTestSupport.hpp 的 makeTable 契约一致）；
 *   2. inputs/invalid/ —— 六个单点变异误例表（其余表由消费测试取正例同款表）；
 *   3. expected/catalog-expected.json —— 正例装配黄金面（条目值/missing 清单/
 *      装配序——按 units/selection.md §4.1/§5.1 冻结语义的封闭代数直算）与逐误例
 *      期望校验报告（逐 issue：码/文件/行/列/型号/比较字段——按 §5.3/§6.3 校验
 *      清单逐条推导，与产品实现 CatalogValidation.cpp 零共享代码）。
 *
 * 与产品实现（selection/src/CatalogValidation.cpp + CatalogTypes.cpp）的独立性：
 * 期望值由本脚本按单元卡 §5.3 八码族＋§6.3 四码族的文字规则直写推导（行号＝
 * firstDataRowNo＋数据行序；比较型字段＝源数据字面值），不调用、不复刻产品代码；
 * 两侧任一漂移即在消费用例显性失败。
 *
 * 用法：node make_sel_catalog_golden.mjs（在本目录执行——相对路径定位 inputs）。
 */

import { writeFileSync } from "node:fs";
import { createHash } from "node:crypto";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const here = dirname(fileURLToPath(import.meta.url));
const validDir = join(here, "..", "inputs", "valid");
const invalidDir = join(here, "..", "inputs", "invalid");
const expectedPath = join(here, "..", "expected", "catalog-expected.json");

// =====================================================================
// 唯一数据源：黄金目录（catalogId=cat-sel-golden @ 1.0.0）
// ——四个数据集（catalog/screening/combo/backfill）的器件数据同源单点；
//    修改任何数值须同步复跑四个数据集生成脚本。
// =====================================================================

/** v1 表头（与 CatalogTestSupport.hpp / CatalogValidation.cpp 列契约逐列同步）。 */
const HEADERS = {
  motors: ["model_id","vendor","display_name","rated_torque_nm","peak_torque_nm",
           "rated_speed","max_speed","rated_power_w","overload_torque_nm",
           "overload_duration_s","duty_class","rated_voltage_v","thermal_ref_temp",
           "thermal_factor_per_ref","brake_torque_nm","holding_torque_nm",
           "rotor_inertia_kgm2","mass_kg","mounting_flange_kind","mounting_shaft_kind",
           "curve_ref"],
  gearboxes: ["model_id","vendor","display_name","rated_output_torque_nm",
              "peak_output_torque_nm","max_input_speed","ratio","efficiency","backlash",
              "rated_life","mounting_orientation","external_load_radial_n",
              "external_load_axial_n","external_load_dist_m","mass_kg",
              "housing_inertia_kgm2","mounting_flange_kind","mounting_shaft_kind",
              "curve_ref"],
  curves: ["curve_id","owner_kind","owner_model_id","x_quantity","x_unit",
           "y_quantity","y_unit","x_value","y_value","point_index"],
  compat: ["motor_model_id","gearbox_model_id","mount_kind"],
};

/** 单字段规格（semantic 语义列＝目录包登记素材——人读定位；测试断言 column/unit/required）。 */
const fs = (column, unit, semantic) => ({ column, unit, semantic });

/** 电机可缺失列（必填性按列契约——与 CatalogTestSupport.motorDictionary 同步）。 */
const MOTOR_OPTIONAL = ["overload_torque_nm","overload_duration_s","rated_voltage_v",
  "thermal_ref_temp","thermal_factor_per_ref","brake_torque_nm",
  "holding_torque_nm","mounting_flange_kind","mounting_shaft_kind","curve_ref"];

// 电机 missing 收集清单（装配期 cell-empty 显式标记——ERR-01）。★ curve_ref
// 不入本清单：引用列空＝无曲线引用（装配段按空串直跳——非"字段缺失"语义，
// 产品装配行为同口径；字典 required 面仍按上方 MOTOR_OPTIONAL 承载）。
const MOTOR_MISSING_COLS = ["overload_torque_nm","overload_duration_s","rated_voltage_v",
  "brake_torque_nm","holding_torque_nm","thermal_ref_temp",
  "thermal_factor_per_ref","mounting_flange_kind","mounting_shaft_kind"];

const motorDict = HEADERS.motors.map((column) => {
  const units = { rated_torque_nm:"N*m", peak_torque_nm:"N*m", rated_speed:"rad/s",
                  max_speed:"rad/s", rated_power_w:"W", overload_torque_nm:"N*m",
                  overload_duration_s:"s", rated_voltage_v:"V", thermal_ref_temp:"1",
                  thermal_factor_per_ref:"1", brake_torque_nm:"N*m",
                  holding_torque_nm:"N*m", rotor_inertia_kgm2:"kg*m^2", mass_kg:"kg" };
  const semantics = { model_id:"稳定型号 ID（包内唯一）", vendor:"厂商", display_name:"显示名",
                      rated_torque_nm:"额定连续转矩", peak_torque_nm:"峰值转矩",
                      rated_speed:"额定转速", max_speed:"最高转速", rated_power_w:"额定功率",
                      overload_torque_nm:"过载转矩", overload_duration_s:"过载持续时间",
                      duty_class:"工作制", rated_voltage_v:"额定电压",
                      thermal_ref_temp:"温度降额参考档位", thermal_factor_per_ref:"每档能力系数",
                      brake_torque_nm:"制动能力", holding_torque_nm:"保持能力",
                      rotor_inertia_kgm2:"转子惯量", mass_kg:"质量",
                      mounting_flange_kind:"法兰接口", mounting_shaft_kind:"轴伸接口",
                      curve_ref:"曲线引用（分号分隔）" };
  return { column, unit: units[column] ?? "",
           required: !MOTOR_OPTIONAL.includes(column), semantic: semantics[column] };
});

const gearboxDict = [
  "model_id","vendor","display_name","rated_output_torque_nm","peak_output_torque_nm",
  "max_input_speed","ratio","efficiency","backlash","rated_life","mounting_orientation",
  "external_load_radial_n","external_load_axial_n","external_load_dist_m","mass_kg",
  "housing_inertia_kgm2","mounting_flange_kind","mounting_shaft_kind","curve_ref",
].map((column, i) => {
  const units = { rated_output_torque_nm:"N*m", peak_output_torque_nm:"N*m",
                  max_input_speed:"rad/s", ratio:"1", efficiency:"1", backlash:"rad",
                  rated_life:"1", external_load_radial_n:"N", external_load_axial_n:"N",
                  external_load_dist_m:"m", mass_kg:"kg", housing_inertia_kgm2:"kg*m^2" };
  const optional = ["backlash","rated_life","external_load_radial_n","external_load_axial_n",
                    "external_load_dist_m","housing_inertia_kgm2","mounting_flange_kind",
                    "mounting_shaft_kind","curve_ref"].includes(column);
  const semantics = { model_id:"稳定型号 ID（包内唯一）", vendor:"厂商", display_name:"显示名",
                      rated_output_torque_nm:"额定输出转矩（输出轴系）",
                      peak_output_torque_nm:"峰值输出转矩", max_input_speed:"允许输入转速",
                      ratio:"速比 n:1", efficiency:"效率", backlash:"回程间隙",
                      rated_life:"额定寿命（循环数）", mounting_orientation:"安装方向",
                      external_load_radial_n:"允许径向力", external_load_axial_n:"允许轴向力",
                      external_load_dist_m:"外载荷作用点距离", mass_kg:"质量",
                      housing_inertia_kgm2:"壳体惯量", mounting_flange_kind:"法兰接口",
                      mounting_shaft_kind:"轴伸接口", curve_ref:"曲线引用（分号分隔）" };
  return { column, unit: units[column] ?? "", required: !optional, semantic: semantics[column] };
});

const curveDict = ["curve_id","owner_kind","owner_model_id","x_quantity","x_unit",
  "y_quantity","y_unit","x_value","y_value","point_index"].map((column) => ({
    column, unit: column === "point_index" ? "1" : "", required: true,
    semantic: column + " 语义（点级声明；单位由行级 x_unit/y_unit 承载）",
  }));

const compatDict = ["motor_model_id","gearbox_model_id","mount_kind"].map((column) => ({
  column, unit: "", required: true, semantic: column + " 语义（兼容对）" }));

// ---- 黄金器件行（文本值即 CSV 原文——expected 数值由 parseFloat 直取）----

const motorRows = [
  // M-101 基准型：全字段填写＋功率曲线（thermal ref=60——heavy 算例 ambient=55 不折减）。
  ["M-101","Sinotech","SG-101","8.0","20.0","150","300","2200","20.0","12","S1","220","60","0.90","25","30","0.012","5.5","flangeA","shaftB","c101-power"],
  // M-102 小型：thermal 两列/curve_ref 空（→Partial missing 清单）。
  ["M-102","Sinotech","SG-102","3.0","8.0","150","250","600","8.0","6","S1","220","","","10","12","0.020","3.2","flangeA","shaftB",""],
  // M-103 大型：thermal/brake/holding/curve_ref 空＋duty=S2＋380 V＋flangeC。
  ["M-103","Sinotech","SG-103","10.0","25.0","200","400","3500","25.0","15","S2","380","","","","","0.030","7.0","flangeC","shaftB",""],
];

const gearboxRows = [
  ["G-201","Nabtesco","RV-201","120","240","300","50","0.92","0.0015","6000","any","1300","800","0.16","6.5","0.012","flangeA","shaftB","c201-torque"],
  ["G-202","Nabtesco","RV-202","60","120","250","100","0.80","0.003","4000","any","500","600","0.05","3.0","0.006","flangeA","shaftB",""],
  ["G-203","Nabtesco","RV-203","200","400","300","30","0.95","","8000","up","","","","9.0","0.020","flangeA","shaftB",""],
];

const curveRows = [
  ["c101-power","motor","M-101","speed","rad/s","power","W","100","800","0"],
  ["c101-power","motor","M-101","speed","rad/s","power","W","200","1600","1"],
  ["c101-power","motor","M-101","speed","rad/s","power","W","300","2400","2"],
  ["c201-torque","gearbox","G-201","speed","rad/s","torque","N*m","50","150","0"],
  ["c201-torque","gearbox","G-201","speed","rad/s","torque","N*m","300","90","1"],
];

const compatRows = [
  ["M-101","G-201","flange-mount"],
  ["M-101","G-202","flange-mount"],
  ["M-101","G-203","flange-mount"],
  ["M-102","G-201","flange-mount"],
  ["M-103","G-201","flange-mount"],
];

// =====================================================================
// CSV 序列化（方言标识行＋表头＋数据行；\n 结尾——确定性字节）
// =====================================================================

const BOM_MARK = "ird-catalog-v1";
function toCsv(header, rows) {
  return BOM_MARK + "\n" + header.join(",") + "\n"
       + rows.map((r) => r.join(",")).join("\n") + "\n";
}
function sha256(text) {
  return createHash("sha256").update(text, "utf8").digest("hex");
}

const validCsv = {
  "motors.csv": toCsv(HEADERS.motors, motorRows),
  "gearboxes.csv": toCsv(HEADERS.gearboxes, gearboxRows),
  "capability_curves.csv": toCsv(HEADERS.curves, curveRows),
  "compatibility.csv": toCsv(HEADERS.compat, compatRows),
};

// =====================================================================
// 误例生成（单点变异——其余表由消费测试取正例表）
// =====================================================================

/** 编辑某数据行指定列（保持列序）。 */
function editRow(header, row, column, value) {
  const clone = [...row];
  clone[header.indexOf(column)] = value;
  return clone;
}

const invalidCsv = {
  // 误例 1：数值范围违约（rated_torque_nm=-3.0 → RANGE-INVALID @motors row 4）。
  "range-motors.csv": toCsv(HEADERS.motors,
    motorRows.map((r) => r[0] === "M-102" ? editRow(HEADERS.motors, r, "rated_torque_nm", "-3.0") : r)),
  // 误例 2：重复型号行（M-101 行内容全同追加 → DUPLICATE-MODEL @row 6）。
  "duplicate-motors.csv": toCsv(HEADERS.motors, [...motorRows, [...motorRows[0]]]),
  // 误例 3：曲线 owner 悬空（c201-torque 两行 owner_model_id=M-999 → 逐行
  // REF-DANGLING；且 G-201 的 curve_ref 引用随组清空 → gearboxes REF-DANGLING）。
  "dangling-curves.csv": toCsv(HEADERS.curves,
    curveRows.map((r) => r[1] === "gearbox" ? editRow(HEADERS.curves, r, "owner_model_id", "M-999") : r)),
  // 误例 4：曲线横坐标未按升序提交（c101-power 第 2 点 x=300、第 3 点 x=200
  // → point_index 序上 x0=300→x1=200 下降 → SEL-CURVE-UNORDERED @第 3 点行）。
  "unordered-curves.csv": toCsv(HEADERS.curves,
    curveRows.map((r) => (r[0] === "c101-power" && r[9] === "1")
      ? editRow(HEADERS.curves, r, "x_value", "300")
      : (r[0] === "c101-power" && r[9] === "2")
        ? editRow(HEADERS.curves, r, "x_value", "200") : r)),
  // 误例 5：必填字段缺失（M-103 duty_class 空 → FIELD-MISSING @motors row 5）。
  "missing-motors.csv": toCsv(HEADERS.motors,
    motorRows.map((r) => r[0] === "M-103" ? editRow(HEADERS.motors, r, "duty_class", "") : r)),
  // 误例 6：单位 token 未注册（c201-torque 两行 x_unit=rpm → 逐行 UNIT-INVALID）。
  "unit-curves.csv": toCsv(HEADERS.curves,
    curveRows.map((r) => r[1] === "gearbox" ? editRow(HEADERS.curves, r, "x_unit", "rpm") : r)),
};

// =====================================================================
// 正例装配黄金面（units/selection.md §5.1 装配语义的封闭推导）
// =====================================================================

const num = (t) => parseFloat(t);
const empty = (t) => t === "";

// 减速器可缺失列（装配期 cell-empty → missing 清单——ERR-01 显式标记）。
// ★ curve_ref 不入本清单：引用列空＝无曲线引用（装配段按空串直跳——非
//   "字段缺失"语义；产品装配行为 CatalogValidation.cpp 曲线引用段同口径）。
const GEARBOX_MISSING_COLS = ["backlash","rated_life","housing_inertia_kgm2",
  "external_load_radial_n","external_load_axial_n","external_load_dist_m",
  "mounting_flange_kind","mounting_shaft_kind"];

function missingOf(row, header, optionalCols) {
  const missing = [];
  for (const col of optionalCols) {
    if (empty(row[header.indexOf(col)])) { missing.push({ column: col, reason: "cell-empty" }); }
  }
  return missing;
}

/** 电机条目黄金业务模型（§4.1 MotorCatalogEntry 关键字段——值全部由源行直取）。 */
function motorGolden(row) {
  const h = HEADERS.motors;
  return {
    modelId: row[0],
    ratedTorque: num(row[3]), peakTorque: num(row[4]),
    ratedSpeed: num(row[5]), maxSpeed: num(row[6]), ratedPower: num(row[7]),
    overload: empty(row[8]) ? null : { torque: num(row[8]), duration: num(row[9]) },
    dutyClass: row[10],
    ratedVoltage: empty(row[11]) ? null : num(row[11]),
    thermal: empty(row[12]) ? null : { refTemp: num(row[12]), factorPerRef: num(row[13]) },
    brakeTorque: empty(row[14]) ? null : num(row[14]),
    holdingTorque: empty(row[15]) ? null : num(row[15]),
    rotorInertia: num(row[16]), mass: num(row[17]),
    mounting: { flangeKind: row[18], shaftKind: row[19] },
    curveRefs: row[20] === "" ? [] : row[20].split(";"),
    missing: missingOf(row, h, MOTOR_MISSING_COLS),
    status: missingOf(row, h, MOTOR_MISSING_COLS).length === 0 ? "Valid" : "Partial",
  };
}

function gearboxGolden(row) {
  const h = HEADERS.gearboxes;
  const missing = missingOf(row, h, GEARBOX_MISSING_COLS);
  return {
    modelId: row[0],
    ratedOutputTorque: num(row[3]), peakOutputTorque: num(row[4]),
    maxInputSpeed: num(row[5]), ratio: num(row[6]), efficiency: num(row[7]),
    backlash: empty(row[8]) ? null : num(row[8]),
    ratedLife: empty(row[9]) ? null : num(row[9]),
    mountingOrientation: row[10],
    extLoad: empty(row[11]) ? null : { radial: num(row[11]), axial: num(row[12]), distance: num(row[13]) },
    mass: num(row[14]),
    housingInertia: empty(row[15]) ? null : num(row[15]),
    mounting: { flangeKind: row[16], shaftKind: row[17] },
    curveRefs: row[18] === "" ? [] : row[18].split(";"),
    missing,
    status: missing.length === 0 ? "Valid" : "Partial",
  };
}

const validExpected = {
  motors: motorRows.map(motorGolden),          // 装配序＝modelId 升序（源行序即升序）
  gearboxes: gearboxRows.map(gearboxGolden),
  curves: [
    { curveId: "c101-power", ownerKind: "motor", ownerModelId: "M-101",
      xQuantity: "speed", xUnit: "rad/s", yQuantity: "power", yUnit: "W",
      points: [{ x: 100, y: 800 }, { x: 200, y: 1600 }, { x: 300, y: 2400 }] },
    { curveId: "c201-torque", ownerKind: "gearbox", ownerModelId: "G-201",
      xQuantity: "speed", xUnit: "rad/s", yQuantity: "torque", yUnit: "N*m",
      points: [{ x: 50, y: 150 }, { x: 300, y: 90 }] },
  ],                                            // 装配序＝curveId 升序
  compatibility: compatRows.map((r) => ({ motorId: r[0], gearboxId: r[1], mountKind: r[2] })),
  identity: { catalogId: "cat-sel-golden", version: "1.0.0", source: "黄金器件目录（selection 契约测试同源数据——WP-19-T11）" },
};

// =====================================================================
// 误例期望报告（§5.3/§6.3 校验清单逐条推导；issue 排序＝(file,rowNo,column,code)）
// =====================================================================

const invalidExpected = [
  { id: "range-motors", file: "range-motors.csv", note: "rated_torque_nm=-3.0（数值范围违约）",
    issues: [ { code: "SEL-CATALOG-RANGE-INVALID", file: "motors.csv", rowNo: 4,
                column: "rated_torque_nm", modelId: "M-102",
                actualValue: -3.0, expectedValue: 0.0, unit: "N*m" } ] },
  { id: "duplicate-motors", file: "duplicate-motors.csv", note: "M-101 行内容全同追加（重复型号行）",
    issues: [ { code: "SEL-CATALOG-DUPLICATE-MODEL", file: "motors.csv", rowNo: 6,
                column: "model_id", modelId: "M-101" } ] },
  { id: "dangling-curves", file: "dangling-curves.csv", note: "c201-torque owner=M-999 悬空（逐行）＋G-201 curve_ref 随组清空",
    issues: [
      { code: "SEL-CATALOG-REF-DANGLING", file: "capability_curves.csv", rowNo: 6,
        column: "owner_model_id", modelId: "M-999", actualText: "M-999" },
      { code: "SEL-CATALOG-REF-DANGLING", file: "capability_curves.csv", rowNo: 7,
        column: "owner_model_id", modelId: "M-999", actualText: "M-999" },
      { code: "SEL-CATALOG-REF-DANGLING", file: "gearboxes.csv", rowNo: 3,
        column: "curve_ref", modelId: "G-201" },
    ] },
  { id: "unordered-curves", file: "unordered-curves.csv", note: "c101-power 点序 100→300→200（下降）",
    issues: [ { code: "SEL-CURVE-UNORDERED", file: "capability_curves.csv", rowNo: 5,
                column: "x_value", modelId: "c101-power" } ] },
  { id: "missing-motors", file: "missing-motors.csv", note: "M-103 duty_class 空（必填缺失）",
    issues: [ { code: "SEL-CATALOG-FIELD-MISSING", file: "motors.csv", rowNo: 5,
                column: "duty_class", modelId: "M-103" } ] },
  { id: "unit-curves", file: "unit-curves.csv", note: "c201-torque x_unit=rpm（未注册单位）",
    issues: [
      { code: "SEL-CATALOG-UNIT-INVALID", file: "capability_curves.csv", rowNo: 6,
        column: "x_unit", modelId: "c201-torque", actualText: "rpm" },
      { code: "SEL-CATALOG-UNIT-INVALID", file: "capability_curves.csv", rowNo: 7,
        column: "x_unit", modelId: "c201-torque", actualText: "rpm" },
    ] },
];

// =====================================================================
// 写盘：正例五文件＋误例六表＋expected
// =====================================================================

for (const [name, text] of Object.entries(validCsv)) {
  writeFileSync(join(validDir, name), text, "utf8");
}
for (const [name, text] of Object.entries(invalidCsv)) {
  writeFileSync(join(invalidDir, name), text, "utf8");
}

// 目录包 manifest.json（CatalogManifest 的 io JSON 通道投影——消费测试解析回
// 结构体）。identity.contentIdentity＝装配入口按快照规范序列化计算回填（§4.2）
// ——输入侧全零占位（不伪造；测试断言装配后＝产品计算的包内容身份）。
// files 条目 sha256Hex＝CSV 四文件真实字节哈希（脚本按写盘文本现算）；manifest
// 自身的哈希不可自含——全零占位（io 文件层核对职责，selection 侧透传不校验，
// CatalogTypes.hpp ManifestEntry 注口径）。
const packageManifest = {
  formatVersion: "1",
  identity: { catalogId: "cat-sel-golden", version: "1.0.0",
              contentIdentity: "0".repeat(64),
              source: "黄金器件目录（selection 契约测试同源数据——WP-19-T11）" },
  files: [
    { fileName: "manifest.json", role: "manifest", required: true, sha256Hex: "0".repeat(64) },
    { fileName: "motors.csv", role: "motors", required: true, sha256Hex: sha256(validCsv["motors.csv"]) },
    { fileName: "gearboxes.csv", role: "gearboxes", required: true, sha256Hex: sha256(validCsv["gearboxes.csv"]) },
    { fileName: "capability_curves.csv", role: "curves", required: true, sha256Hex: sha256(validCsv["capability_curves.csv"]) },
    { fileName: "compatibility.csv", role: "compatibility", required: true, sha256Hex: sha256(validCsv["compatibility.csv"]) },
  ],
  fieldDictionary: [
    { targetFile: "motors.csv", fields: motorDict },
    { targetFile: "gearboxes.csv", fields: gearboxDict },
    { targetFile: "capability_curves.csv", fields: curveDict },
    { targetFile: "compatibility.csv", fields: compatDict },
  ],
};
writeFileSync(join(validDir, "manifest.json"), JSON.stringify(packageManifest, null, 2) + "\n", "utf8");

const expected = {
  schema: "sel-catalog-expected/1",
  note: "目录包黄金期望——由 generate/make_sel_catalog_golden.mjs 按单元卡 §4.1/§5.1/§5.3/§6.3 冻结语义封闭推导（行号＝firstDataRowNo(3)＋数据行序；比较型字段＝源数据字面值），与产品 CatalogValidation/CatalogTypes 实现零共享。identity.contentIdentity 为产品装配入口计算面（§4.2）——输入侧全零占位，测试断言装配回填非零且确定性。",
  valid: validExpected,
  invalidCases: invalidExpected,
};

writeFileSync(expectedPath, JSON.stringify(expected, null, 2) + "\n", "utf8");
console.log("sel-catalog-golden: wrote 5 valid files + 6 invalid tables + expected");
console.log("  motors.csv sha256 =", sha256(validCsv["motors.csv"]).slice(0, 16), "…");
