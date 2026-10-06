/**
 * @file   CatalogTestSupport.hpp
 * @brief  目录包测试辅助（selection 单元测试私有头——不跨单元暴露）：
 *         v1 合法基线包构造器＋表/行构造工具＋逐码 issue 查找工具。
 *
 * 设计依据：
 *   - units/selection.md §5.2（v1 文件清单 schema——基线包的列布局与之一
 *     致，与 src/CatalogValidation.cpp 的 v1 列契约表同步维护：字典列集
 *     与契约一致是"合法导入"用例的前提，契约变更加两侧）
 *   - 需求 SEL-01/02（目录包模板/导入校验——基线包即 SEL-01 模板的
 *     可执行形态）、AT-08（导入/错误字段/版本锁定）
 *   - 任务契约 tasks/foundation/WP-19-T03.json acceptance 1/2
 *
 * 线程安全：纯值构造（无共享状态）。
 */

#ifndef IRD_SELECTION_CATALOGTESTSUPPORT_HPP
#define IRD_SELECTION_CATALOGTESTSUPPORT_HPP

#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/selection/CatalogProvider.hpp>
#include <sdurws/ird/selection/CatalogTypes.hpp>

namespace sdurws::ird::selection::testsupport {

// =====================================================================
// 表构造工具（row-major 与 ParsedFileTable 契约对齐）
// =====================================================================

/// 由表头＋二维行构造解析表（firstDataRowNo＝首数据行物理行号——1 起，
/// 含方言标识行/表头行偏移，io 卡 §5.6 口径：本基线按"1 标识行＋1 表头
/// 行"计，数据自第 3 行起）。
inline ParsedFileTable makeTable(const std::string& name,
                                 std::vector<std::string> header,
                                 const std::vector<std::vector<std::string>>& rows,
                                 std::uint64_t firstDataRowNo = 3)
{
    ParsedFileTable t;
    t.fileName = name;
    t.header = std::move(header);
    t.columnCount = t.header.size();
    t.firstDataRowNo = firstDataRowNo;
    for (const auto& r : rows) {
        for (const auto& c : r) {
            t.cells.push_back(c);
        }
    }
    return t;
}

// =====================================================================
// v1 表头（与 src/CatalogValidation.cpp 列契约逐列同步——见文件头注）
// =====================================================================

/// motors.csv v1 表头（21 列）。
inline std::vector<std::string> motorHeader()
{
    return {"model_id", "vendor", "display_name", "rated_torque_nm", "peak_torque_nm",
            "rated_speed", "max_speed", "rated_power_w", "overload_torque_nm",
            "overload_duration_s", "duty_class", "rated_voltage_v", "thermal_ref_temp",
            "thermal_factor_per_ref", "brake_torque_nm", "holding_torque_nm",
            "rotor_inertia_kgm2", "mass_kg", "mounting_flange_kind", "mounting_shaft_kind",
            "curve_ref"};
}

/// gearboxes.csv v1 表头（19 列）。
inline std::vector<std::string> gearboxHeader()
{
    return {"model_id", "vendor", "display_name", "rated_output_torque_nm",
            "peak_output_torque_nm", "max_input_speed", "ratio", "efficiency", "backlash",
            "rated_life", "mounting_orientation", "external_load_radial_n",
            "external_load_axial_n", "external_load_dist_m", "mass_kg",
            "housing_inertia_kgm2", "mounting_flange_kind", "mounting_shaft_kind",
            "curve_ref"};
}

/// capability_curves.csv v1 表头（10 列）。
inline std::vector<std::string> curveHeader()
{
    return {"curve_id", "owner_kind", "owner_model_id", "x_quantity", "x_unit",
            "y_quantity", "y_unit", "x_value", "y_value", "point_index"};
}

/// compatibility.csv v1 表头（3 列）。
inline std::vector<std::string> compatHeader()
{
    return {"motor_model_id", "gearbox_model_id", "mount_kind"};
}

// =====================================================================
// v1 字段字典（与列契约逐列同步——列名/单位/必填性一致）
// =====================================================================

/// 单字段速记（语义列测试不敏感，统一占位）。
inline FieldSpec fs(const char* column, const char* unit, bool required)
{
    FieldSpec s;
    s.column = column;
    s.semantic = std::string(column) + " 语义（测试占位）";
    s.unit = unit;
    s.required = required;
    return s;
}

/// motors.csv 字段字典（21 列）。
inline FieldDictionary motorDictionary()
{
    return {kCatalogFileMotors,
            {fs("model_id", "", true), fs("vendor", "", true), fs("display_name", "", true),
             fs("rated_torque_nm", "N*m", true), fs("peak_torque_nm", "N*m", true),
             fs("rated_speed", "rad/s", true), fs("max_speed", "rad/s", true),
             fs("rated_power_w", "W", true), fs("overload_torque_nm", "N*m", false),
             fs("overload_duration_s", "s", false), fs("duty_class", "", true),
             fs("rated_voltage_v", "V", false), fs("thermal_ref_temp", "1", false),
             fs("thermal_factor_per_ref", "1", false), fs("brake_torque_nm", "N*m", false),
             fs("holding_torque_nm", "N*m", false), fs("rotor_inertia_kgm2", "kg*m^2", true),
             fs("mass_kg", "kg", true), fs("mounting_flange_kind", "", false),
             fs("mounting_shaft_kind", "", false), fs("curve_ref", "", false)}};
}

/// gearboxes.csv 字段字典（19 列）。
inline FieldDictionary gearboxDictionary()
{
    return {kCatalogFileGearboxes,
            {fs("model_id", "", true), fs("vendor", "", true), fs("display_name", "", true),
             fs("rated_output_torque_nm", "N*m", true),
             fs("peak_output_torque_nm", "N*m", true), fs("max_input_speed", "rad/s", true),
             fs("ratio", "1", true), fs("efficiency", "1", true),
             fs("backlash", "rad", false), fs("rated_life", "1", false),
             fs("mounting_orientation", "", true), fs("external_load_radial_n", "N", false),
             fs("external_load_axial_n", "N", false), fs("external_load_dist_m", "m", false),
             fs("mass_kg", "kg", true), fs("housing_inertia_kgm2", "kg*m^2", false),
             fs("mounting_flange_kind", "", false), fs("mounting_shaft_kind", "", false),
             fs("curve_ref", "", false)}};
}

/// capability_curves.csv 字段字典（10 列——x_value/y_value 单位由行级
/// 声明，列契约不携单位）。
inline FieldDictionary curveDictionary()
{
    return {kCatalogFileCurves,
            {fs("curve_id", "", true), fs("owner_kind", "", true),
             fs("owner_model_id", "", true), fs("x_quantity", "", true),
             fs("x_unit", "", true), fs("y_quantity", "", true), fs("y_unit", "", true),
             fs("x_value", "", true), fs("y_value", "", true),
             fs("point_index", "1", true)}};
}

/// compatibility.csv 字段字典（3 列）。
inline FieldDictionary compatDictionary()
{
    return {kCatalogFileCompatibility,
            {fs("motor_model_id", "", true), fs("gearbox_model_id", "", true),
             fs("mount_kind", "", true)}};
}

// =====================================================================
// v1 合法基线包（两电机＋两减速器＋两曲线＋两兼容行——正例数据源）
// =====================================================================

/// 基线 manifest（身份/来源/文件清单/四字典）。
inline CatalogManifest makeBaselineManifest(const std::string& catalogId = "cat-demo",
                                            const std::string& version = "v1")
{
    CatalogManifest m;
    m.formatVersion = kCatalogFormatVersion;
    m.identity.catalogId = catalogId;
    m.identity.version = version;
    m.identity.source = "demo 企业器件库（测试基线）";
    m.files = catalogPackageFileSchema();
    m.fieldDictionary = {motorDictionary(), gearboxDictionary(), curveDictionary(),
                         compatDictionary()};
    return m;
}

/// 基线电机行（可缺失列全部填值——M-200 形态；blank 可选列空＝M-100 形态）。
inline std::vector<std::string> motorRowFull(const std::string& id, const char* torque,
                                             const char* peak, const char* rotorInertia,
                                             const char* mass, const std::string& curveRefs)
{
    return {id, "Sinotech", "ST-" + id, torque, peak, "150", "300", "2000", "6.75", "10",
            "S1", "220", "25", "0.95", "18", "20", rotorInertia, mass, "flangeA", "shaftB",
            curveRefs};
}

/// 基线电机表（M-100 可选缺失多列→Partial；M-200 全填＋引用曲线）。
inline ParsedFileTable makeBaselineMotors()
{
    // M-100：rated_voltage/thermal/brake/holding 空（→missing 清单），
    // curve_ref 引用基线曲线 curve-tq。
    std::vector<std::string> m100 = {"M-100", "Sinotech", "ST-100", "4.5", "11", "150",
                                     "300", "2000", "", "", "S1", "", "", "", "", "",
                                     "0.012", "5.5", "flangeA", "shaftB", "curve-tq"};
    std::vector<std::string> m200 = motorRowFull("M-200", "9", "22", "0.03", "9.2", "");
    return makeTable(kCatalogFileMotors, motorHeader(), {m100, m200});
}

/// 基线减速器表（G-50 可选全空；G-120 全填＋引用曲线 curve-eff）。
inline ParsedFileTable makeBaselineGearboxes()
{
    std::vector<std::string> g50 = {"G-50", "Nabtesco", "RV-50", "50", "100", "300", "50",
                                    "0.95", "", "", "any", "", "", "", "3.2", "", "", "",
                                    ""};
    std::vector<std::string> g120 = {"G-120", "Nabtesco", "RV-120", "120", "240", "300",
                                     "100", "0.94", "0.8", "6000", "any", "1200", "600",
                                     "0.08", "6.5", "0.012", "flangeA", "shaftB",
                                     "curve-eff"};
    return makeTable(kCatalogFileGearboxes, gearboxHeader(), {g50, g120});
}

/// 基线曲线表：curve-tq＝电机 M-100 转矩-转速三点（插值黄金数据源）；
/// curve-eff＝减速器 G-120 功率-转速两点。
inline ParsedFileTable makeBaselineCurves()
{
    std::vector<std::vector<std::string>> rows = {
        {"curve-tq", "motor", "M-100", "speed", "rad/s", "torque", "N*m", "50", "5.0", "0"},
        {"curve-tq", "motor", "M-100", "speed", "rad/s", "torque", "N*m", "150", "4.5", "1"},
        {"curve-tq", "motor", "M-100", "speed", "rad/s", "torque", "N*m", "300", "3.5", "2"},
        {"curve-eff", "gearbox", "G-120", "speed", "rad/s", "power", "W", "100", "3000", "0"},
        {"curve-eff", "gearbox", "G-120", "speed", "rad/s", "power", "W", "300", "3600", "1"},
    };
    return makeTable(kCatalogFileCurves, curveHeader(), rows);
}

/// 基线兼容表（两对——与基线主表对应）。
inline ParsedFileTable makeBaselineCompat()
{
    std::vector<std::vector<std::string>> rows = {
        {"M-100", "G-50", "flange-mount"},
        {"M-200", "G-120", "flange-mount"},
    };
    return makeTable(kCatalogFileCompatibility, compatHeader(), rows);
}

/// 基线解析输入（四表齐全——合法包）。
inline ParsedCatalogInput makeBaselineInput()
{
    ParsedCatalogInput in;
    in.files = {makeBaselineMotors(), makeBaselineGearboxes(), makeBaselineCurves(),
                makeBaselineCompat()};
    return in;
}

// =====================================================================
// 行级编辑工具（错误注入——从合法基线做单点变异）
// =====================================================================

/// 数据行数（测试侧只读便利——与 ParsedFileTable row-major 契约对齐）。
inline std::size_t rowCount(const ParsedFileTable& t)
{
    return t.columnCount == 0 ? 0 : t.cells.size() / t.columnCount;
}

/// 取单元格（测试侧只读便利）。
inline std::string cell(const ParsedFileTable& t, std::size_t r, const std::string& column)
{
    for (std::size_t i = 0; i < t.header.size(); ++i) {
        if (t.header[i] == column) {
            return t.cells[r * t.columnCount + i];
        }
    }
    return {};
}

/// 取整行（按数据行序；测试内复制/追加行的统一入口）。
inline std::vector<std::string> cellRowOf(const ParsedFileTable& t, std::size_t r)
{
    std::vector<std::string> row;
    row.reserve(t.columnCount);
    for (std::size_t c = 0; c < t.columnCount; ++c) {
        row.push_back(t.cells[r * t.columnCount + c]);
    }
    return row;
}

/// 定位行（按首列＝model_id / curve_id / motor_model_id）并替换指定列值；
/// 找不到返回 false（测试内显性失败）。
inline bool editRow(ParsedFileTable& t, const std::string& keyColumn,
                    const std::string& keyValue, const std::string& column,
                    const std::string& newValue)
{
    std::size_t keyCol = t.header.size();
    std::size_t valCol = t.header.size();
    for (std::size_t i = 0; i < t.header.size(); ++i) {
        if (t.header[i] == keyColumn) { keyCol = i; }
        if (t.header[i] == column) { valCol = i; }
    }
    if (keyCol >= t.header.size() || valCol >= t.header.size()) { return false; }
    for (std::size_t r = 0; r < rowCount(t); ++r) {
        if (t.cells[r * t.columnCount + keyCol] == keyValue) {
            t.cells[r * t.columnCount + valCol] = newValue;
            return true;
        }
    }
    return false;
}

/// 追加一行（重复行/冲突行注入）。
inline void appendRow(ParsedFileTable& t, const std::vector<std::string>& row)
{
    for (const auto& c : row) {
        t.cells.push_back(c);
    }
}

/// 报告内按码查找 issue（找到返回首条——逐码断言的统一入口）。
inline std::optional<CatalogIssue> findByCode(const CatalogValidationReport& rep,
                                              const std::string& code)
{
    for (const CatalogIssue& iss : rep.issues) {
        if (iss.code == code) {
            return iss;
        }
    }
    return std::nullopt;
}

}  // namespace sdurws::ird::selection::testsupport

#endif  // IRD_SELECTION_CATALOGTESTSUPPORT_HPP
