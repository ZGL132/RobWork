/**
 * @file   CatalogValidation.cpp
 * @brief  目录包导入业务校验与快照装配实现（selection 单元）——
 *         CatalogImporter（卡 §5.3 八码全表＋§6.3 曲线四码＋文件间引用
 *         语义/唯一性/范围/必填）＋tryMakePerformanceCurve 曲线构造入口
 *         ＋v1 列契约注册表。
 *
 * 设计依据：
 *   - units/selection.md §5.1（固定交接流程——selection 只消费 io 解析
 *     结果，不触文件系统）、§5.2（v1 文件清单 schema——P-IO-7 注册形态）、
 *     §5.3（业务校验清单八码）、§6.3（曲线校验四码）、§6.4（固定额定值
 *     口径与同 quantity 曲线歧义）、§14.2（validate 签名与"致命结构错误
 *     以异常 fail-fast（schema 级），行级错误入报告"）、§14.0（错误二分
 *     ——调用方错误 fail-fast vs 数据类返回诊断）
 *   - 需求 SEL-01（目录包模板/字段字典/版本/来源）、SEL-02（导入校验：
 *     文件清单/文件间引用/单位/必填/唯一性/范围）、ERR-01（比较型字段）、
 *     NFR-COR-02/03（确定性；非有限/非法单位/引用缺失不静默通过）、
 *     NFR-DEP-04（未知格式拒绝）
 *   - 任务契约 tasks/foundation/WP-19-T03.json acceptance 1/3
 *
 * 落位细化登记（卡 §19.3 同步登记）：
 *   1. DUPLICATE-MODEL / DUPLICATE-ID 两码分配：同 modelId 多行且行内容
 *      全同 → DUPLICATE-MODEL（重复行）；同 modelId 多行且内容不同 →
 *      DUPLICATE-ID（主键冲突）。显示名重复但 ID 不同＝合法（卡 §5.3）。
 *   2. 数值文本无法解析（非数字文本）：主表归 SEL-CATALOG-RANGE-INVALID
 *      （数值合法性码族，actualText 携带原文）；曲线表归 SEL-CURVE-
 *      NONFINITE（曲线族，NFR-COR-03 同路径）。不新增码值（码表随
 *      DiagCodes.hpp 表尾追加纪律）。
 *   3. 曲线表 owner_kind 词表违约（非 motor/gearbox）→ REF-DANGLING
 *      （owner 无法归属——引用语义族）；x_quantity/y_quantity 词表违约
 *      → SCHEMA-MISMATCH（量纲 token 词表归字段字典——卡 §6.1）。
 *   4. 同条目引用同 (xQuantity,yQuantity) 的多条曲线（用途歧义）→
 *      SCHEMA-MISMATCH（卡 §6.4"同 quantity 多曲线→导入拒绝（歧义）"
 *      未指名码——归 schema 族，不新增码值）。
 *   5. v1 曲线单位列（x_unit/y_unit）冻结 SI 本位 token（speed→rad/s、
 *      torque→N*m、power→W；siFactor==1）——"目录以其他单位提供经字段
 *      字典声明换算"的完整支持随 core Units 词表扩展（本域不自建换算
 *      表，卡 §5.3"换算唯一经 core"）。
 *   6. manifest 身份字段：catalogId/version 为空 → schema 级抛出（身份
 *      三元组前提破坏，卡 §4.3）；source 为空 → 行级 FIELD-MISSING
 *      （V1"来源缺失"注入项的可定位拒绝面）。
 *
 * 线程安全：CatalogImporter 无状态纯函数对象（可重入——卡 §14.10）；
 * 确定性：报告按 (file,rowNo,column,code) 稳定序（NFR-COR-02）。
 */

#include <sdurws/ird/selection/CatalogProvider.hpp>

#include <sdurws/ird/selection/DiagCodes.hpp>  // SEL-* 码值常量（唯一书写点）

#include <sdurws/ird/core/Units.hpp>  // QuantityKind/UnitToken——v1 列契约量纲与单位词表

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sdurws::ird::selection {

// =====================================================================
// v1 列契约（业务 schema 的 selection 侧冻结——P-IO-7/C-6 注册面的字典
// 权威；manifest 字段字典是携带者，校验期与契约逐列比对）
// =====================================================================

namespace {

/// 单列契约（v1）：text==true 为文本列——不进单位/范围校验，仅必填性/
/// 非空；数值列按 kind/unit 进单位与范围校验。
struct ColumnContract {
    const char* column;        ///< 列名（与 CSV 表头逐字符一致）
    bool text;                 ///< 文本列标记
    core::QuantityKind kind;   ///< 数值列期望量纲（core 词表——量纲检查期望侧）
    const char* unit;          ///< 数值列 v1 冻结单位 token（core 注册；文本列 ""）
    bool required;             ///< 必填性（false＝可缺失→missing 清单显式标记）
};

/// 电机主表 v1 列契约（序＝CSV 列序；过载拆转矩/时长两列——卡 §5.2 列清单展开）。
const std::vector<ColumnContract>& motorColumns()
{
    static const std::vector<ColumnContract> kCols = {
        {"model_id",               true,  core::QuantityKind::Dimensionless, "",       true },
        {"vendor",                 true,  core::QuantityKind::Dimensionless, "",       true },
        {"display_name",           true,  core::QuantityKind::Dimensionless, "",       true },
        {"rated_torque_nm",        false, core::QuantityKind::Torque,        "N*m",    true },
        {"peak_torque_nm",         false, core::QuantityKind::Torque,        "N*m",    true },
        {"rated_speed",            false, core::QuantityKind::AngularVelocity, "rad/s", true },
        {"max_speed",              false, core::QuantityKind::AngularVelocity, "rad/s", true },
        {"rated_power_w",          false, core::QuantityKind::Power,         "W",      true },
        {"overload_torque_nm",     false, core::QuantityKind::Torque,        "N*m",    false},
        {"overload_duration_s",    false, core::QuantityKind::Time,          "s",      false},
        {"duty_class",             true,  core::QuantityKind::Dimensionless, "",       true },
        {"rated_voltage_v",        false, core::QuantityKind::Voltage,       "V",      false},
        {"thermal_ref_temp",       false, core::QuantityKind::Dimensionless, "1",      false},
        {"thermal_factor_per_ref", false, core::QuantityKind::Dimensionless, "1",      false},
        {"brake_torque_nm",        false, core::QuantityKind::Torque,        "N*m",    false},
        {"holding_torque_nm",      false, core::QuantityKind::Torque,        "N*m",    false},
        {"rotor_inertia_kgm2",     false, core::QuantityKind::Inertia,       "kg*m^2", true },
        {"mass_kg",                false, core::QuantityKind::Mass,          "kg",     true },
        {"mounting_flange_kind",   true,  core::QuantityKind::Dimensionless, "",       false},
        {"mounting_shaft_kind",    true,  core::QuantityKind::Dimensionless, "",       false},
        {"curve_ref",              true,  core::QuantityKind::Dimensionless, "",       false},
    };
    return kCols;
}

/// 减速器主表 v1 列契约（backlash v1 冻结 rad；rated_life v1＝循环数——登记 5）。
const std::vector<ColumnContract>& gearboxColumns()
{
    static const std::vector<ColumnContract> kCols = {
        {"model_id",                true,  core::QuantityKind::Dimensionless, "",       true },
        {"vendor",                  true,  core::QuantityKind::Dimensionless, "",       true },
        {"display_name",            true,  core::QuantityKind::Dimensionless, "",       true },
        {"rated_output_torque_nm",  false, core::QuantityKind::Torque,        "N*m",    true },
        {"peak_output_torque_nm",   false, core::QuantityKind::Torque,        "N*m",    true },
        {"max_input_speed",         false, core::QuantityKind::AngularVelocity, "rad/s", true },
        {"ratio",                   false, core::QuantityKind::Dimensionless, "1",      true },
        {"efficiency",              false, core::QuantityKind::Dimensionless, "1",      true },
        {"backlash",                false, core::QuantityKind::Angle,         "rad",    false},
        {"rated_life",              false, core::QuantityKind::Dimensionless, "1",      false},
        {"mounting_orientation",    true,  core::QuantityKind::Dimensionless, "",       true },
        {"external_load_radial_n",  false, core::QuantityKind::Force,         "N",      false},
        {"external_load_axial_n",   false, core::QuantityKind::Force,         "N",      false},
        {"external_load_dist_m",    false, core::QuantityKind::Length,        "m",      false},
        {"mass_kg",                 false, core::QuantityKind::Mass,          "kg",     true },
        {"housing_inertia_kgm2",    false, core::QuantityKind::Inertia,       "kg*m^2", false},
        {"mounting_flange_kind",    true,  core::QuantityKind::Dimensionless, "",       false},
        {"mounting_shaft_kind",     true,  core::QuantityKind::Dimensionless, "",       false},
        {"curve_ref",               true,  core::QuantityKind::Dimensionless, "",       false},
    };
    return kCols;
}

/// 能力曲线表 v1 列契约（x_value/y_value 的单位由行级 x_unit/y_unit 声明
/// ——v1 冻结 SI 本位（登记 5），故列契约不携单位；行级校验由 quantity
/// 契约表承载）。
const std::vector<ColumnContract>& curveColumns()
{
    static const std::vector<ColumnContract> kCols = {
        {"curve_id",       true,  core::QuantityKind::Dimensionless, "",  true },
        {"owner_kind",     true,  core::QuantityKind::Dimensionless, "",  true },
        {"owner_model_id", true,  core::QuantityKind::Dimensionless, "",  true },
        {"x_quantity",     true,  core::QuantityKind::Dimensionless, "",  true },
        {"x_unit",         true,  core::QuantityKind::Dimensionless, "",  true },
        {"y_quantity",     true,  core::QuantityKind::Dimensionless, "",  true },
        {"y_unit",         true,  core::QuantityKind::Dimensionless, "",  true },
        {"x_value",        false, core::QuantityKind::Dimensionless, "",  true },
        {"y_value",        false, core::QuantityKind::Dimensionless, "",  true },
        {"point_index",    false, core::QuantityKind::Dimensionless, "1", true },
    };
    return kCols;
}

/// 兼容关系表 v1 列契约。
const std::vector<ColumnContract>& compatColumns()
{
    static const std::vector<ColumnContract> kCols = {
        {"motor_model_id",   true, core::QuantityKind::Dimensionless, "", true },
        {"gearbox_model_id", true, core::QuantityKind::Dimensionless, "", true },
        {"mount_kind",       true, core::QuantityKind::Dimensionless, "", true },
    };
    return kCols;
}

/// 按文件名取 v1 列契约；非 CSV 目录文件返回 nullptr（manifest 无列契约）。
const std::vector<ColumnContract>* contractFor(const std::string& fileName)
{
    if (fileName == kCatalogFileMotors)        { return &motorColumns(); }
    if (fileName == kCatalogFileGearboxes)     { return &gearboxColumns(); }
    if (fileName == kCatalogFileCurves)        { return &curveColumns(); }
    if (fileName == kCatalogFileCompatibility) { return &compatColumns(); }
    return nullptr;
}

/// 量纲 token → 期望 SI 单位 token（v1 quantity 词表——卡 §6.1；登记 5）。
const char* siTokenForQuantity(std::string_view quantity)
{
    if (quantity == kQuantitySpeed)  { return "rad/s"; }
    if (quantity == kQuantityTorque) { return "N*m"; }
    if (quantity == kQuantityPower)  { return "W"; }
    return nullptr;
}

// =====================================================================
// 基础工具（定位/解析/工厂——全部纯函数，确定性）
// =====================================================================

/// 去除首尾 ASCII 空白（CSV 单元格清理；io 层保留原文，业务裁剪归本层）。
std::string_view trimView(std::string_view s)
{
    const auto isSpace = [](char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    };
    while (!s.empty() && isSpace(s.front())) { s.remove_prefix(1); }
    while (!s.empty() && isSpace(s.back())) { s.remove_suffix(1); }
    return s;
}

/// 解析有限十进制浮点（trim 后 std::from_chars——与 C locale 无关；
/// 不接受前导 '+'/内部空白——v1 目录模板规范数字，违约即解析失败，
/// 归数值合法性码族，登记 2）。非有限（NaN/±Inf 文本）在此拦截
/// （NFR-COR-03）。
bool parseFiniteDouble(std::string_view text, double& out)
{
    const std::string_view t = trimView(text);
    if (t.empty()) { return false; }
    const char* begin = t.data();
    const char* end = t.data() + t.size();
    const auto res = std::from_chars(begin, end, out);
    if (res.ec != std::errc{} || res.ptr != end) { return false; }
    return std::isfinite(out);
}

/// 解析整数（point_index 用；同 from_chars 口径）。
bool parseInt64(std::string_view text, std::int64_t& out)
{
    const std::string_view t = trimView(text);
    if (t.empty()) { return false; }
    const char* begin = t.data();
    const char* end = t.data() + t.size();
    const auto res = std::from_chars(begin, end, out);
    return res.ec == std::errc{} && res.ptr == end;
}

/// 单元格安全取值（row-major 展开；越界给空串引用——防御不 UB；行等长
/// 由 io PadTrailing 策略与 columnCount 保证）。
const std::string& cellAt(const ParsedFileTable& t, std::size_t rowIdx, std::size_t colIdx)
{
    static const std::string kEmpty;
    if (colIdx >= t.columnCount) { return kEmpty; }
    const std::size_t idx = rowIdx * t.columnCount + colIdx;
    return idx < t.cells.size() ? t.cells[idx] : kEmpty;
}

/// 列名→列位映射（每表建一次；未命中的列名映射到 header.size()——越界，
/// cellAt 返回空，语义＝"该列不存在于本表"）。
std::map<std::string, std::size_t> buildColumnIndex(const ParsedFileTable& t)
{
    std::map<std::string, std::size_t> idx;
    for (std::size_t i = 0; i < t.header.size(); ++i) {
        idx.emplace(t.header[i], i);   // emplace：重复表头保留首位（schema 校验另行报多列）
    }
    return idx;
}

/// 数据行数（row-major 展开行数＝cells/columnCount；列数为 0 时 0 行——
/// 防御除零，空表合法）。
std::size_t rowCountOf(const ParsedFileTable& t)
{
    if (t.columnCount == 0) { return 0; }
    return t.cells.size() / t.columnCount;
}

/// 单数值列校验前置声明（定义在本匿名命名空间后段——validateMotorRows/
/// validateGearboxRows 两个调用方共用，语义见定义处）。
bool validateNumberCell(const ParsedFileTable& t, std::size_t rowIdx, std::uint64_t rowNo,
                        const ColumnContract& cc,
                        const std::map<std::string, std::size_t>& colIdx,
                        const std::string& modelId, std::vector<MissingField>* missingOut,
                        double& out, CatalogValidationReport& rep);

/// 定位 issue 工厂（卡 §5.3"逐项可定位到文件/行/列"的统一入口）。
CatalogIssue makeIssue(std::string code, const std::string& file, std::uint64_t rowNo,
                       std::string column, std::string message)
{
    CatalogIssue iss;
    iss.code = std::move(code);
    iss.file = file;
    iss.rowNo = rowNo;
    iss.column = std::move(column);
    iss.message = std::move(message);
    return iss;
}

/// schema 级致命错误的统一抛出（卡 §14.2 note"致命结构错误以异常
/// fail-fast（schema 级）"——与行级报告分轨）。
[[noreturn]] void failSchema(const std::string& what)
{
    throw std::invalid_argument("SEL-CATALOG(schema): " + what);
}

/// 行内容签名（同 ID 重复行的"内容全同"判据——全单元格连接；'\x1f'
/// 单元分隔符为控制字符，不出现在规范 CSV 文本中，无碰撞歧义）。
std::string rowSignature(const ParsedFileTable& t, std::size_t rowIdx)
{
    std::string sig;
    for (std::size_t c = 0; c < t.columnCount; ++c) {
        sig += cellAt(t, rowIdx, c);
        sig += '\x1f';
    }
    return sig;
}

// =====================================================================
// schema 级前置（fail-fast 轨）
// =====================================================================

/// 必备解析表存在性（io 文件层前置——ParsedCatalogInput 契约，见头注）。
void requireParsedTables(const ParsedCatalogInput& parsed)
{
    for (const std::string& f : {std::string{kCatalogFileMotors},
                                 std::string{kCatalogFileGearboxes},
                                 std::string{kCatalogFileCurves},
                                 std::string{kCatalogFileCompatibility}}) {
        if (parsed.find(f) == nullptr) {
            // io §7.8 清单核对应已拦下必备文件缺失——收到不完整输入＝
            // 调用方装配违约（卡 §5.1 分工：文件层归 io；selection 不重复
            // 执行文件层核对，仅对输入完整性做前置断言）。
            failSchema("必备解析表缺失：" + f
                       + "（io 文件层校验前置未满足——调用方装配违约，卡 §5.1）");
        }
    }
}

/// manifest 身份与字段字典校验；返回清单级 issue（source 缺失等非致命
/// 项），致命项直接抛出（登记 6）。
CatalogValidationReport checkManifest(const CatalogManifest& manifest)
{
    CatalogValidationReport rep;

    // ① 格式版本：未知版本拒绝＋升级指引（卡 §5.2——不自动升级，PM-06
    // 精神）。版本未知则字典语义/列布局全部失去前提——schema 级抛出。
    if (manifest.formatVersion != kCatalogFormatVersion) {
        failSchema("未知 formatVersion \"" + manifest.formatVersion
                   + "\"（本实现支持 v" + kCatalogFormatVersion
                   + "；拒绝导入并给出升级指引——不自动升级，卡 §5.2/PM-06）");
    }

    // ② 目录身份：catalogId/version 是身份三元组前两段（卡 §4.3）——
    // 缺失则身份不可建立、版本锁定与引用完整性无从谈起，schema 级抛出。
    if (trimView(manifest.identity.catalogId).empty()
        || trimView(manifest.identity.version).empty()) {
        failSchema("manifest 身份不完整：catalogId/version 均为必填"
                   "（版本/来源缺失——SEL-01/AT-08；身份三元组前提破坏，卡 §4.3）");
    }

    // ③ 来源信息：SEL-01 必备语义，但缺失不破坏身份结构——入清单级
    // 报告（可定位拒绝）。
    if (trimView(manifest.identity.source).empty()) {
        rep.issues.push_back(makeIssue(std::string{kSelCatalogFieldMissing},
                                       kCatalogFileManifest, 0, "source",
                                       "来源信息缺失（SEL-01：目录包必须携带企业来源描述）"));
    }

    // ④ 字段字典覆盖四 CSV 且与 v1 契约逐列一致（列名/单位/必填性）。
    // 字典权威＝selection v1 列契约（业务 schema 归 selection——卡 §5.1
    // 分工；C-6 注册面）。字典未覆盖必备文件＝装配层违约，抛出；字典
    // 列值与契约不符＝schema 不匹配，入报告（定位 manifest.json）。
    for (const std::string& f : {std::string{kCatalogFileMotors},
                                 std::string{kCatalogFileGearboxes},
                                 std::string{kCatalogFileCurves},
                                 std::string{kCatalogFileCompatibility}}) {
        const FieldDictionary* dict = nullptr;
        for (const FieldDictionary& d : manifest.fieldDictionary) {
            if (d.targetFile == f) { dict = &d; break; }
        }
        if (dict == nullptr) {
            failSchema("字段字典未覆盖必备文件 " + f
                       + "（装配层违约——SEL-01 字段字典必备面）");
        }
        const std::vector<ColumnContract>* contract = contractFor(f);
        if (dict->fields.size() != contract->size()) {
            // 列数不符＝schema 不匹配（定位到文件级；逐位噪音无定位价值）。
            rep.issues.push_back(makeIssue(
                std::string{kSelCatalogSchemaMismatch}, kCatalogFileManifest, 0, f,
                "字段字典列数 " + std::to_string(dict->fields.size())
                    + " 与 v1 契约 " + std::to_string(contract->size()) + " 不符"));
            continue;
        }
        for (std::size_t i = 0; i < contract->size(); ++i) {
            const FieldSpec& spec = dict->fields[i];
            const ColumnContract& cc = (*contract)[i];
            // 单位一致性：声明须与契约相同；契约携单位的数值列还须是 core
            // 注册 token（防字典写出未注册单位）。契约 unit 为空串的数值列
            // （曲线表 x_value/y_value——单位由行级 x_unit/y_unit 声明，
            // 登记面见 CurveGroup/validateCurveRows）不要求注册性。
            const bool unitOk = spec.unit == cc.unit
                && (cc.text || cc.unit[0] == '\0'
                    || core::UnitToken::find(spec.unit).has_value());
            const bool reqOk = spec.required == cc.required;
            if (spec.column != cc.column || !unitOk || !reqOk) {
                rep.issues.push_back(makeIssue(
                    std::string{kSelCatalogSchemaMismatch}, kCatalogFileManifest, 0,
                    spec.column.empty() ? f : spec.column,
                    "字段字典第 " + std::to_string(i + 1) + " 列与 v1 契约不符（期望列 "
                        + cc.column + "；单位 \"" + cc.unit + "\"；必填 "
                        + (cc.required ? "是" : "否") + "）"));
                break;   // 首个差异定位即停
            }
        }
    }
    return rep;
}

/// 单 CSV 表的 schema 列比对（卡 §5.3 行 1：manifest 声明列与 CSV 表头
/// 一致——多列/缺列逐列定位）。返回 false＝存在 mismatch（调用方跳过
/// 该表行级校验——列位不可靠，避免级联噪音）。
bool checkTableSchema(const ParsedFileTable& t, const FieldDictionary& dict,
                      CatalogValidationReport& rep)
{
    bool ok = true;
    const std::size_t n = std::max(dict.fields.size(), t.header.size());
    for (std::size_t i = 0; i < n; ++i) {
        const bool hasDict = i < dict.fields.size();
        const bool hasHead = i < t.header.size();
        if (hasDict && hasHead) {
            if (dict.fields[i].column != t.header[i]) {
                // 同位不同列名——定位双端期望。
                rep.issues.push_back(makeIssue(
                    std::string{kSelCatalogSchemaMismatch}, t.fileName, 0, t.header[i],
                    "表头第 " + std::to_string(i + 1) + " 列 \"" + t.header[i]
                        + "\" 与字段字典声明 \"" + dict.fields[i].column + "\" 不符"));
                ok = false;
            }
        } else if (hasDict && !hasHead) {
            rep.issues.push_back(makeIssue(
                std::string{kSelCatalogSchemaMismatch}, t.fileName, 0, dict.fields[i].column,
                "缺列：字段字典声明 \"" + dict.fields[i].column + "\" 未在表头出现"));
            ok = false;
        } else if (!hasDict && hasHead) {
            rep.issues.push_back(makeIssue(
                std::string{kSelCatalogSchemaMismatch}, t.fileName, 0, t.header[i],
                "多列：表头列 \"" + t.header[i] + "\" 未在字段字典声明"));
            ok = false;
        }
    }
    return ok;
}

// =====================================================================
// 行级校验——电机/减速器主表
// =====================================================================

/// 数值列单一范围谓词检查（比较型三要素齐备——ERR-01）。
void checkRange(const ParsedFileTable& t, std::uint64_t rowNo, const std::string& column,
                const std::string& modelId, double actual, bool pass,
                double expectedBound, const char* unit, const char* expectation,
                CatalogValidationReport& rep)
{
    if (pass) { return; }
    CatalogIssue iss = makeIssue(std::string{kSelCatalogRangeInvalid}, t.fileName, rowNo,
                                 column, std::string("数值范围违约：要求 ") + expectation);
    iss.modelId = modelId;
    iss.actualValue = actual;
    iss.expectedValue = expectedBound;
    iss.unit = unit;
    rep.issues.push_back(std::move(iss));
}

/// 电机主表行级校验（schema 已通过；返回 (modelId → 行内容签名) 供引用
/// 校验使用；行签名供兼容表 ID 集核对——统一从本函数的产出取）。
void validateMotorRows(const ParsedFileTable& t,
                       std::map<std::string, std::string>& modelIdsOut,
                       CatalogValidationReport& rep)
{
    const auto colIdx = buildColumnIndex(t);
    std::map<std::string, std::uint64_t> seenRow;   // modelId → 首现行号（查重）
    std::map<std::string, std::string> seenSig;     // modelId → 首现行签名（重复行判别）

    for (std::size_t r = 0; r < rowCountOf(t); ++r) {
        const std::uint64_t rowNo = t.firstDataRowNo + r;
        // 主键列：缺失即无法归属（级联校验无定位价值）——报缺失后跳过该行。
        const std::string_view idV = trimView(cellAt(t, r, colIdx.at("model_id")));
        if (idV.empty()) {
            rep.issues.push_back(makeIssue(std::string{kSelCatalogFieldMissing}, t.fileName,
                                           rowNo, "model_id", "必填字段缺失（空白单元格）"));
            continue;
        }
        const std::string modelId(idV);

        // 唯一性（卡 §5.3 行 4——登记 1 两码分配）。
        const std::string sig = rowSignature(t, r);
        const auto seen = seenRow.find(modelId);
        if (seen != seenRow.end()) {
            const bool identical = seenSig[modelId] == sig;
            rep.issues.push_back(makeIssue(
                identical ? std::string{kSelCatalogDuplicateModel}
                          : std::string{kSelCatalogDuplicateId},
                t.fileName, rowNo, "model_id",
                identical ? "重复型号行（同 modelId 行内容全同）"
                          : "稳定 ID 重复（同 modelId 行内容不同——主键唯一性破坏）"));
            rep.issues.back().modelId = modelId;
            continue;   // 重复行不再逐列校验（避免同因多报）
        }
        seenRow[modelId] = rowNo;
        seenSig[modelId] = sig;
        modelIdsOut[modelId] = sig;

        // 可缺失字段清单（显式标记——ERR-01）随行收集。
        std::vector<MissingField> missing;

        // 数值列逐列校验（契约序；值暂存交叉校验所需者）。
        double ratedTorque = 0, peakTorque = 0, ratedSpeed = 0, maxSpeed = 0;
        double thermalFactor = 0;
        bool hasRatedTorque = false, hasPeakTorque = false;
        bool hasRatedSpeed = false, hasMaxSpeed = false, hasThermalFactor = false;

        for (const ColumnContract& cc : motorColumns()) {
            if (cc.text) {
                // 文本列：必填非空 / 可选缺失入清单。
                const std::string_view v = trimView(cellAt(t, r, colIdx.at(cc.column)));
                if (v.empty()) {
                    if (cc.required) {
                        rep.issues.push_back(makeIssue(
                            std::string{kSelCatalogFieldMissing}, t.fileName, rowNo,
                            cc.column, "必填字段缺失（空白单元格）"));
                        rep.issues.back().modelId = modelId;
                    } else {
                        missing.push_back({cc.column, "cell-empty"});
                    }
                }
                continue;
            }
            // 数值列：解析＋范围谓词（逐列展开——范围语义是列契约的一部分，
            // 集中在下方显式写出，不做成谓词表，便于逐列对照单元卡）。
            double val = 0.0;
            const bool has = validateNumberCell(t, r, rowNo, cc, colIdx, modelId,
                                                &missing, val, rep);
            if (!has) { continue; }
            if (cc.column == std::string("rated_torque_nm")) {
                ratedTorque = val; hasRatedTorque = true;
                checkRange(t, rowNo, cc.column, modelId, val, val > 0.0, 0.0, "N*m",
                           "rated_torque_nm > 0（N·m）", rep);
            } else if (cc.column == std::string("peak_torque_nm")) {
                peakTorque = val; hasPeakTorque = true;
                checkRange(t, rowNo, cc.column, modelId, val, val > 0.0, 0.0, "N*m",
                           "peak_torque_nm > 0（N·m）", rep);
            } else if (cc.column == std::string("rated_speed")) {
                ratedSpeed = val; hasRatedSpeed = true;
                checkRange(t, rowNo, cc.column, modelId, val, val > 0.0, 0.0, "rad/s",
                           "rated_speed > 0（rad/s）", rep);
            } else if (cc.column == std::string("max_speed")) {
                maxSpeed = val; hasMaxSpeed = true;
                checkRange(t, rowNo, cc.column, modelId, val, val > 0.0, 0.0, "rad/s",
                           "max_speed > 0（rad/s）", rep);
            } else if (cc.column == std::string("rated_power_w")) {
                checkRange(t, rowNo, cc.column, modelId, val, val > 0.0, 0.0, "W",
                           "rated_power_w > 0（W）", rep);
            } else if (cc.column == std::string("overload_torque_nm")) {
                checkRange(t, rowNo, cc.column, modelId, val, val > 0.0, 0.0, "N*m",
                           "overload_torque_nm > 0（N·m）", rep);
            } else if (cc.column == std::string("overload_duration_s")) {
                checkRange(t, rowNo, cc.column, modelId, val, val > 0.0, 0.0, "s",
                           "overload_duration_s > 0（s）", rep);
            } else if (cc.column == std::string("rated_voltage_v")) {
                checkRange(t, rowNo, cc.column, modelId, val, val > 0.0, 0.0, "V",
                           "rated_voltage_v > 0（V）", rep);
            } else if (cc.column == std::string("thermal_ref_temp")) {
                // 档位值：有限即可（v1 无量纲档位——登记 5；°C 语义）。
            } else if (cc.column == std::string("thermal_factor_per_ref")) {
                thermalFactor = val; hasThermalFactor = true;
                checkRange(t, rowNo, cc.column, modelId, val,
                           val > 0.0 && val <= 1.0, 1.0, "1",
                           "thermal_factor_per_ref ∈ (0,1]（无量纲）", rep);
            } else if (cc.column == std::string("brake_torque_nm")) {
                checkRange(t, rowNo, cc.column, modelId, val, val > 0.0, 0.0, "N*m",
                           "brake_torque_nm > 0（N·m）", rep);
            } else if (cc.column == std::string("holding_torque_nm")) {
                checkRange(t, rowNo, cc.column, modelId, val, val > 0.0, 0.0, "N*m",
                           "holding_torque_nm > 0（N·m）", rep);
            } else if (cc.column == std::string("rotor_inertia_kgm2")) {
                checkRange(t, rowNo, cc.column, modelId, val, val > 0.0, 0.0, "kg*m^2",
                           "rotor_inertia_kgm2 > 0（kg·m²）", rep);
            } else if (cc.column == std::string("mass_kg")) {
                checkRange(t, rowNo, cc.column, modelId, val, val > 0.0, 0.0, "kg",
                           "mass_kg > 0（kg）", rep);
            }
        }

        // 交叉范围（列间约束——峰值≥额定、最高转速≥额定转速）。
        if (hasRatedTorque && hasPeakTorque) {
            checkRange(t, rowNo, "peak_torque_nm", modelId, peakTorque,
                       peakTorque >= ratedTorque, ratedTorque, "N*m",
                       "peak_torque_nm >= rated_torque_nm（N·m）", rep);
        }
        if (hasRatedSpeed && hasMaxSpeed) {
            checkRange(t, rowNo, "max_speed", modelId, maxSpeed,
                       maxSpeed >= ratedSpeed, ratedSpeed, "rad/s",
                       "max_speed >= rated_speed（rad/s）", rep);
        }
        (void)thermalFactor;
        (void)hasThermalFactor;
    }
}

/// 单数值列校验（必填/可选统一入口；列位由 colIdx 查询）：
///   空白 → 必填报 FIELD-MISSING / 可选登记 missing 清单（ERR-01）；
///   解析失败/非有限 → RANGE-INVALID（原文入 actualText——登记 2）。
/// 返回是否取得有效数值（可选项缺失返回 false，不算失败）。
bool validateNumberCell(const ParsedFileTable& t, std::size_t rowIdx, std::uint64_t rowNo,
                        const ColumnContract& cc,
                        const std::map<std::string, std::size_t>& colIdx,
                        const std::string& modelId, std::vector<MissingField>* missingOut,
                        double& out, CatalogValidationReport& rep)
{
    const std::string_view v = trimView(cellAt(t, rowIdx, colIdx.at(cc.column)));
    if (v.empty()) {
        if (cc.required) {
            rep.issues.push_back(makeIssue(std::string{kSelCatalogFieldMissing}, t.fileName,
                                           rowNo, cc.column, "必填字段缺失（空白单元格）"));
            rep.issues.back().modelId = modelId;
        } else if (missingOut != nullptr) {
            missingOut->push_back({cc.column, "cell-empty"});
        }
        return false;
    }
    double val = 0.0;
    if (!parseFiniteDouble(v, val)) {
        CatalogIssue iss = makeIssue(std::string{kSelCatalogRangeInvalid}, t.fileName, rowNo,
                                     cc.column, "无法解析为有限数值（NFR-COR-03 数值合法性）");
        iss.modelId = modelId;
        iss.actualText = std::string(v);
        iss.expectedText = "有限数值";
        rep.issues.push_back(std::move(iss));
        return false;
    }
    out = val;
    return true;
}

// =====================================================================
// 行级校验——减速器主表（结构与电机表同构；范围谓词按减速器语义展开）
// =====================================================================

void validateGearboxRows(const ParsedFileTable& t,
                         std::map<std::string, std::string>& modelIdsOut,
                         CatalogValidationReport& rep)
{
    const auto colIdx = buildColumnIndex(t);
    std::map<std::string, std::uint64_t> seenRow;
    std::map<std::string, std::string> seenSig;

    for (std::size_t r = 0; r < rowCountOf(t); ++r) {
        const std::uint64_t rowNo = t.firstDataRowNo + r;
        const std::string_view idV = trimView(cellAt(t, r, colIdx.at("model_id")));
        if (idV.empty()) {
            rep.issues.push_back(makeIssue(std::string{kSelCatalogFieldMissing}, t.fileName,
                                           rowNo, "model_id", "必填字段缺失（空白单元格）"));
            continue;
        }
        const std::string modelId(idV);

        // 唯一性（同电机表——登记 1 两码分配）。
        const std::string sig = rowSignature(t, r);
        const auto seen = seenRow.find(modelId);
        if (seen != seenRow.end()) {
            const bool identical = seenSig[modelId] == sig;
            rep.issues.push_back(makeIssue(
                identical ? std::string{kSelCatalogDuplicateModel}
                          : std::string{kSelCatalogDuplicateId},
                t.fileName, rowNo, "model_id",
                identical ? "重复型号行（同 modelId 行内容全同）"
                          : "稳定 ID 重复（同 modelId 行内容不同——主键唯一性破坏）"));
            rep.issues.back().modelId = modelId;
            continue;
        }
        seenRow[modelId] = rowNo;
        seenSig[modelId] = sig;
        modelIdsOut[modelId] = sig;

        std::vector<MissingField> missing;

        // 数值列（值暂存交叉校验所需者）。
        double ratedOutTorque = 0, peakOutTorque = 0;
        bool hasRatedOut = false, hasPeakOut = false;

        for (const ColumnContract& cc : gearboxColumns()) {
            if (cc.text) {
                const std::string_view v = trimView(cellAt(t, r, colIdx.at(cc.column)));
                if (v.empty()) {
                    if (cc.required) {
                        rep.issues.push_back(makeIssue(
                            std::string{kSelCatalogFieldMissing}, t.fileName, rowNo,
                            cc.column, "必填字段缺失（空白单元格）"));
                        rep.issues.back().modelId = modelId;
                    } else {
                        missing.push_back({cc.column, "cell-empty"});
                    }
                }
                continue;
            }
            double val = 0.0;
            const bool has = validateNumberCell(t, r, rowNo, cc, colIdx, modelId,
                                                &missing, val, rep);
            if (!has) { continue; }
            if (cc.column == std::string("rated_output_torque_nm")) {
                ratedOutTorque = val; hasRatedOut = true;
                checkRange(t, rowNo, cc.column, modelId, val, val > 0.0, 0.0, "N*m",
                           "rated_output_torque_nm > 0（N·m，输出轴系）", rep);
            } else if (cc.column == std::string("peak_output_torque_nm")) {
                peakOutTorque = val; hasPeakOut = true;
                checkRange(t, rowNo, cc.column, modelId, val, val > 0.0, 0.0, "N*m",
                           "peak_output_torque_nm > 0（N·m，输出轴系）", rep);
            } else if (cc.column == std::string("max_input_speed")) {
                checkRange(t, rowNo, cc.column, modelId, val, val > 0.0, 0.0, "rad/s",
                           "max_input_speed > 0（rad/s）", rep);
            } else if (cc.column == std::string("ratio")) {
                // 速比正值（卡 §4.1 ratio 注——方向语义换算归 drivetrain 口径）。
                checkRange(t, rowNo, cc.column, modelId, val, val > 0.0, 0.0, "1",
                           "ratio > 0（无量纲）", rep);
            } else if (cc.column == std::string("efficiency")) {
                // 效率∈(0,1]（卡 §4.1 efficiency 注；卡 §4.4 单位表）。
                checkRange(t, rowNo, cc.column, modelId, val,
                           val > 0.0 && val <= 1.0, 1.0, "1",
                           "efficiency ∈ (0,1]（无量纲）", rep);
            } else if (cc.column == std::string("backlash")) {
                // 回程间隙非负（v1 冻结 rad——登记 5）。
                checkRange(t, rowNo, cc.column, modelId, val, val >= 0.0, 0.0, "rad",
                           "backlash >= 0（rad）", rep);
            } else if (cc.column == std::string("rated_life")) {
                // 寿命正值（v1＝循环数，无量纲——登记 5）。
                checkRange(t, rowNo, cc.column, modelId, val, val > 0.0, 0.0, "1",
                           "rated_life > 0（循环数）", rep);
            } else if (cc.column == std::string("external_load_radial_n")
                       || cc.column == std::string("external_load_axial_n")
                       || cc.column == std::string("external_load_dist_m")) {
                checkRange(t, rowNo, cc.column, modelId, val, val >= 0.0, 0.0,
                           cc.column == std::string("external_load_dist_m") ? "m" : "N",
                           "允许外载荷/作用点距离 >= 0（N/m）", rep);
            } else if (cc.column == std::string("housing_inertia_kgm2")) {
                checkRange(t, rowNo, cc.column, modelId, val, val > 0.0, 0.0, "kg*m^2",
                           "housing_inertia_kgm2 > 0（kg·m²）", rep);
            } else if (cc.column == std::string("mass_kg")) {
                checkRange(t, rowNo, cc.column, modelId, val, val > 0.0, 0.0, "kg",
                           "mass_kg > 0（kg）", rep);
            }
        }

        // 交叉范围：峰值输出转矩≥额定输出转矩。
        if (hasRatedOut && hasPeakOut) {
            checkRange(t, rowNo, "peak_output_torque_nm", modelId, peakOutTorque,
                       peakOutTorque >= ratedOutTorque, ratedOutTorque, "N*m",
                       "peak_output_torque_nm >= rated_output_torque_nm（N·m）", rep);
        }
    }
}

// =====================================================================
// 行级校验——能力曲线表（§6.3 四码＋词表/单位/引用存在性；组内点序）
// =====================================================================

/// 曲线表原始行（校验中间态——数值已解析；SI 埼值因 v1 冻结 SI 本位而
/// 与原文一致，登记 5）。
struct CurveRowRaw {
    std::string curveId;      ///< curve_id（组键）
    std::string ownerKind;    ///< owner_kind（motor|gearbox）
    std::string ownerModelId; ///< owner_model_id
    std::string xQuantity;    ///< x_quantity（v1 词表）
    std::string yQuantity;    ///< y_quantity
    std::string xUnit;        ///< x_unit（SI 本位 token——登记 5）
    std::string yUnit;        ///< y_unit
    double xVal = 0.0;        ///< x_value（SI；单位 xUnit）
    double yVal = 0.0;        ///< y_value（SI；单位 yUnit）
    std::int64_t pointIdx = 0; ///< point_index（组内点序声明）
    std::uint64_t rowNo = 0;  ///< 物理行号（错误定位）
};

/// 曲线组（同 curve_id 的点集＋owner/量纲声明——引用校验与装配共用）。
struct CurveGroup {
    std::string ownerKind;    ///< owner 类别（motor|gearbox——首行声明）
    std::string ownerModelId; ///< owner 型号 ID
    std::string xQuantity;    ///< 横坐标量纲 token
    std::string yQuantity;    ///< 纵坐标量纲 token
    std::string xUnit;        ///< 横坐标 SI 单位 token
    std::string yUnit;        ///< 纵坐标 SI 单位 token
    /// 点集：((x, y), rowNo)——组内按 (point_index, rowNo) 稳定排序后填充。
    std::vector<std::pair<std::pair<double, double>, std::uint64_t>> points;
};

/// 曲线表行级校验：词表/单位/点值/点序/组形态全表；返回 curveId→组映射
/// （引用校验与装配共用；空 map＝表无数据行——合法，卡 §5.2）。
std::map<std::string, CurveGroup> validateCurveRows(
    const ParsedFileTable& t,
    const std::map<std::string, std::string>& motorIds,
    const std::map<std::string, std::string>& gearboxIds,
    CatalogValidationReport& rep)
{
    const auto colIdx = buildColumnIndex(t);
    std::vector<CurveRowRaw> rows;

    for (std::size_t r = 0; r < rowCountOf(t); ++r) {
        const std::uint64_t rowNo = t.firstDataRowNo + r;
        CurveRowRaw row;
        row.rowNo = rowNo;

        // 必填文本列逐列（缺失即无法归属/校验该行语义——报缺失并跳过行）。
        bool rowUsable = true;
        for (const char* col : {"curve_id", "owner_kind", "owner_model_id",
                                "x_quantity", "x_unit", "y_quantity", "y_unit"}) {
            const std::string_view v = trimView(cellAt(t, r, colIdx.at(col)));
            if (v.empty()) {
                rep.issues.push_back(makeIssue(std::string{kSelCatalogFieldMissing},
                                               t.fileName, rowNo, col,
                                               "必填字段缺失（空白单元格）"));
                rowUsable = false;
            }
        }
        // 数值列（x_value/y_value/point_index）——空/解析失败按登记 2/3 归族。
        double xv = 0.0, yv = 0.0;
        std::int64_t pi = 0;
        for (const char* col : {"x_value", "y_value"}) {
            const std::string_view v = trimView(cellAt(t, r, colIdx.at(col)));
            if (v.empty()) {
                rep.issues.push_back(makeIssue(std::string{kSelCatalogFieldMissing},
                                               t.fileName, rowNo, col,
                                               "必填字段缺失（空白单元格）"));
                rowUsable = false;
            } else {
                double d = 0.0;
                if (!parseFiniteDouble(v, d)) {
                    // 曲线表数值文本解析失败归曲线族（登记 2/3——NFR-COR-03
                    // 同路径：无法进入数值域即"非有限点"拒绝面）。
                    CatalogIssue iss = makeIssue(std::string{kSelCurveNonfinite}, t.fileName,
                                                 rowNo, col,
                                                 "无法解析为有限数值（NFR-COR-03）");
                    iss.actualText = std::string(v);
                    iss.expectedText = "有限数值";
                    rep.issues.push_back(std::move(iss));
                    rowUsable = false;
                } else if (col == std::string("x_value")) {
                    xv = d;
                } else {
                    yv = d;
                }
            }
        }
        {
            const std::string_view v = trimView(cellAt(t, r, colIdx.at("point_index")));
            if (v.empty()) {
                rep.issues.push_back(makeIssue(std::string{kSelCatalogFieldMissing},
                                               t.fileName, rowNo, "point_index",
                                               "必填字段缺失（空白单元格）"));
                rowUsable = false;
            } else if (!parseInt64(v, pi) || pi < 0) {
                CatalogIssue iss = makeIssue(std::string{kSelCatalogRangeInvalid}, t.fileName,
                                             rowNo, "point_index",
                                             "无法解析为非负整数（点序声明）");
                iss.actualText = std::string(v);
                rep.issues.push_back(std::move(iss));
                rowUsable = false;
            }
        }
        if (!rowUsable) { continue; }

        // 文本列取值（上方已验非空）。
        row.curveId = std::string(trimView(cellAt(t, r, colIdx.at("curve_id"))));
        row.ownerKind = std::string(trimView(cellAt(t, r, colIdx.at("owner_kind"))));
        row.ownerModelId = std::string(trimView(cellAt(t, r, colIdx.at("owner_model_id"))));
        row.xQuantity = std::string(trimView(cellAt(t, r, colIdx.at("x_quantity"))));
        row.yQuantity = std::string(trimView(cellAt(t, r, colIdx.at("y_quantity"))));
        row.xUnit = std::string(trimView(cellAt(t, r, colIdx.at("x_unit"))));
        row.yUnit = std::string(trimView(cellAt(t, r, colIdx.at("y_unit"))));
        row.xVal = xv;
        row.yVal = yv;
        row.pointIdx = pi;

        // owner_kind 词表（motor|gearbox——卡 §5.2 列清单；违约＝owner 无法
        // 归属，归引用语义族，登记 3）。
        if (row.ownerKind != kCurveOwnerMotor && row.ownerKind != kCurveOwnerGearbox) {
            CatalogIssue iss = makeIssue(std::string{kSelCatalogRefDangling}, t.fileName,
                                         rowNo, "owner_kind",
                                         "owner_kind 词表违约（期望 motor|gearbox——无法归属）");
            iss.actualText = row.ownerKind;
            iss.expectedText = "motor|gearbox";
            iss.modelId = row.curveId;
            rep.issues.push_back(std::move(iss));
            continue;
        }

        // 量纲 token 词表（§6.1"字段字典词表：speed/torque/power/…"——
        // 词表违约归 schema 族，登记 3）。
        for (const auto& [token, colName] :
             {std::pair<const std::string*, const char*>{&row.xQuantity, "x_quantity"},
              std::pair<const std::string*, const char*>{&row.yQuantity, "y_quantity"}}) {
            if (siTokenForQuantity(*token) == nullptr) {
                CatalogIssue iss = makeIssue(std::string{kSelCatalogSchemaMismatch},
                                             t.fileName, rowNo, colName,
                                             "量纲 token 词表违约（v1 词表：speed|torque|power）");
                iss.actualText = *token;
                iss.expectedText = "speed|torque|power";
                iss.modelId = row.curveId;
                rep.issues.push_back(std::move(iss));
            }
        }

        // 单位 token：注册性＋与量纲匹配＋SI 本位（登记 5——v1 冻结 SI；
        // 未知单位拒绝不猜测，卡 §5.3 行 2；量纲不匹配同码）。
        for (const auto& q : {std::pair<const std::string*, const std::string*>
                                 {&row.xQuantity, &row.xUnit},
                              std::pair<const std::string*, const std::string*>
                                 {&row.yQuantity, &row.yUnit}}) {
            const std::string& token = *q.second;
            const std::string& quantity = *q.first;
            const char* colName = q.second == &row.xUnit ? "x_unit" : "y_unit";
            const auto ut = core::UnitToken::find(token);
            if (!ut.has_value()) {
                CatalogIssue iss = makeIssue(std::string{kSelCatalogUnitInvalid}, t.fileName,
                                             rowNo, colName,
                                             "单位 token 未注册（core Units 词表外——拒绝不猜测）");
                iss.modelId = row.curveId;
                iss.actualText = token;
                rep.issues.push_back(std::move(iss));
                continue;
            }
            const char* expectedSi = siTokenForQuantity(quantity);
            if (expectedSi != nullptr && token != expectedSi) {
                // 期望＝量纲对应 SI 本位（登记 5）；v1 拒绝非 SI 本位声明。
                CatalogIssue iss = makeIssue(std::string{kSelCatalogUnitInvalid}, t.fileName,
                                             rowNo, colName,
                                             "单位与量纲不符或非 SI 本位（v1 冻结 SI 口径）");
                iss.modelId = row.curveId;
                iss.actualText = token;
                iss.expectedText = expectedSi;
                rep.issues.push_back(std::move(iss));
            }
        }

        // owner_model_id 存在性（引用语义——对应主表必须已有该型号行；
        // 卡 §5.3 行 6 语义层）。
        const std::map<std::string, std::string>& ownerTable =
            row.ownerKind == kCurveOwnerMotor ? motorIds : gearboxIds;
        if (ownerTable.find(row.ownerModelId) == ownerTable.end()) {
            CatalogIssue iss = makeIssue(std::string{kSelCatalogRefDangling}, t.fileName,
                                         rowNo, "owner_model_id",
                                         "owner 型号在对应主表中不存在（引用悬空）");
            iss.modelId = row.ownerModelId;
            iss.actualText = row.ownerModelId;
            rep.issues.push_back(std::move(iss));
            continue;   // 悬空行不进组（组只收可归属的点）
        }

        rows.push_back(std::move(row));
    }

    // 分组：curve_id → 组；组内按 (point_index, rowNo) 稳定排序（点序声明
    // 优先，同声明按物理行序——确定性，NFR-COR-02）。
    std::map<std::string, CurveGroup> groups;
    std::map<std::string, std::vector<CurveRowRaw>> byCurve;
    for (CurveRowRaw& row : rows) {
        byCurve[row.curveId].push_back(std::move(row));
    }
    for (auto& [curveId, grpRows] : byCurve) {
        std::stable_sort(grpRows.begin(), grpRows.end(),
                         [](const CurveRowRaw& a, const CurveRowRaw& b) {
                             return a.pointIdx < b.pointIdx;
                         });
        CurveGroup g;
        g.ownerKind = grpRows.front().ownerKind;
        g.ownerModelId = grpRows.front().ownerModelId;
        g.xQuantity = grpRows.front().xQuantity;
        g.yQuantity = grpRows.front().yQuantity;
        g.xUnit = grpRows.front().xUnit;
        g.yUnit = grpRows.front().yUnit;
        // 组声明一致性：同组各行 owner/量纲声明不一致＝曲线行集混装（定位
        // 后继行——首行为组声明基准）。
        for (std::size_t i = 1; i < grpRows.size(); ++i) {
            const CurveRowRaw& row = grpRows[i];
            if (row.ownerKind != g.ownerKind || row.ownerModelId != g.ownerModelId
                || row.xQuantity != g.xQuantity || row.yQuantity != g.yQuantity
                || row.xUnit != g.xUnit || row.yUnit != g.yUnit) {
                CatalogIssue iss = makeIssue(
                    std::string{kSelCatalogSchemaMismatch}, t.fileName, row.rowNo,
                    "curve_id", "同 curve_id 各行的 owner/量纲/单位声明不一致（混装拒绝）");
                iss.modelId = curveId;
                rep.issues.push_back(std::move(iss));
            }
        }
        for (const CurveRowRaw& row : grpRows) {
            g.points.push_back({{row.xVal, row.yVal}, row.rowNo});
        }
        // 组形态与点序校验（§6.3——在提交序〔point_index 升序〕上判定）。
        if (g.points.size() < 2) {
            // 单点曲线声明为曲线（卡 §6.3 行 4/§6.4——固定额定值口径）。
            CatalogIssue iss = makeIssue(std::string{kSelCurveIntervalInvalid}, t.fileName,
                                         grpRows.front().rowNo, "curve_id",
                                         "曲线点数 " + std::to_string(g.points.size())
                                         + " < 2（单点能力值应走固定额定值口径）");
            iss.modelId = curveId;
            rep.issues.push_back(std::move(iss));
        }
        for (std::size_t i = 0; i + 1 < g.points.size(); ++i) {
            const double x0 = g.points[i].first.first;
            const double x1 = g.points[i + 1].first.first;
            const std::uint64_t rowNo = g.points[i + 1].second;
            if (x1 == x0) {
                CatalogIssue iss = makeIssue(std::string{kSelCurveDupX}, t.fileName, rowNo,
                                             "x_value", "重复横坐标 x=" + std::to_string(x0)
                                             + "（插值语义歧义）");
                iss.modelId = curveId;
                rep.issues.push_back(std::move(iss));
            } else if (x1 < x0) {
                CatalogIssue iss = makeIssue(std::string{kSelCurveUnordered}, t.fileName,
                                             rowNo, "x_value",
                                             "横坐标未按升序提交（" + std::to_string(x0)
                                             + " → " + std::to_string(x1)
                                             + "）——要求目录修正，不代排序");
                iss.modelId = curveId;
                rep.issues.push_back(std::move(iss));
            }
        }
        groups[curveId] = std::move(g);
    }
    return groups;
}

// =====================================================================
// 行级校验——兼容关系表（必填/引用存在性/同对 mount_kind 一致性）
// =====================================================================

void validateCompatRows(const ParsedFileTable& t,
                        const std::map<std::string, std::string>& motorIds,
                        const std::map<std::string, std::string>& gearboxIds,
                        CatalogValidationReport& rep)
{
    const auto colIdx = buildColumnIndex(t);
    // 同对声明集：(motorId, gearboxId) → 首行 mountKind（矛盾检测基准）。
    std::map<std::pair<std::string, std::string>, std::string> seenPairs;

    for (std::size_t r = 0; r < rowCountOf(t); ++r) {
        const std::uint64_t rowNo = t.firstDataRowNo + r;
        std::string mid, gid, kind;
        bool rowUsable = true;
        for (const auto& [col, out] : {std::pair<const char*, std::string*>
                                          {"motor_model_id", &mid},
                                       std::pair<const char*, std::string*>
                                          {"gearbox_model_id", &gid},
                                       std::pair<const char*, std::string*>
                                          {"mount_kind", &kind}}) {
            const std::string_view v = trimView(cellAt(t, r, colIdx.at(col)));
            if (v.empty()) {
                rep.issues.push_back(makeIssue(std::string{kSelCatalogFieldMissing},
                                               t.fileName, rowNo, col,
                                               "必填字段缺失（空白单元格）"));
                rowUsable = false;
            } else {
                *out = std::string(v);
            }
        }
        if (!rowUsable) { continue; }

        // 引用存在性（双方——卡 §5.3 行 6 语义层；文件层存在性归 io）。
        if (motorIds.find(mid) == motorIds.end()
            || gearboxIds.find(gid) == gearboxIds.end()) {
            CatalogIssue iss = makeIssue(std::string{kSelCatalogRefDangling}, t.fileName,
                                         rowNo, "motor_model_id",
                                         "兼容关系引用的型号在主表中不存在（引用悬空）");
            iss.modelId = mid + ">" + gid;
            iss.actualText = mid + ">" + gid;
            rep.issues.push_back(std::move(iss));
            continue;
        }

        // 兼容冲突（卡 §5.3 行 7：同型号对多行且 mount_kind 矛盾；同对
        // 同 kind 的冗余重复声明按卡字面容忍——不发码）。
        const auto key = std::make_pair(mid, gid);
        const auto seen = seenPairs.find(key);
        if (seen == seenPairs.end()) {
            seenPairs[key] = kind;
        } else if (seen->second != kind) {
            CatalogIssue iss = makeIssue(std::string{kSelCatalogCompatConflict}, t.fileName,
                                         rowNo, "mount_kind",
                                         "同型号对 mount_kind 矛盾（首声明 \""
                                             + seen->second + "\"）");
            iss.modelId = mid + ">" + gid;
            iss.actualText = kind;
            iss.expectedText = seen->second;
            rep.issues.push_back(std::move(iss));
        }
    }
}

// =====================================================================
// 条目能力曲线引用校验（§5.3 行 6 语义层＋§6.4 同 quantity 歧义）
// =====================================================================

/// 主表条目 curve_ref 列引用校验（ownerKindExpected＝该表对应的 owner
/// 词表值）；同条目引用同 (xQuantity,yQuantity) 多曲线报歧义（登记 4）。
void validateEntryCurveRefs(const ParsedFileTable& t,
                            const std::map<std::string, CurveGroup>& curves,
                            std::string_view ownerKindExpected,
                            CatalogValidationReport& rep)
{
    const auto colIdx = buildColumnIndex(t);
    const std::size_t refCol = colIdx.at("curve_ref");

    for (std::size_t r = 0; r < rowCountOf(t); ++r) {
        const std::uint64_t rowNo = t.firstDataRowNo + r;
        const std::string modelId(trimView(cellAt(t, r, colIdx.at("model_id"))));
        if (modelId.empty()) { continue; }   // 主键缺失已由主表校验报出

        // curve_ref 文本：分号分隔多 curve_id（v1 冻结分隔符——卡 §5.2
        // "curve_ref…"列形态的落位细化，登记于单元卡）。
        const std::string refText(trimView(cellAt(t, r, refCol)));
        if (refText.empty()) { continue; }   // 无曲线引用＝固定额定值口径（合法）

        std::vector<std::pair<std::string, std::string>> usedQuantities; // (xQ,yQ)
        std::size_t begin = 0;
        while (begin <= refText.size()) {
            const std::size_t sep = refText.find(';', begin);
            const std::string token = std::string(trimView(std::string_view(
                refText).substr(begin, sep == std::string::npos
                                          ? std::string::npos : sep - begin)));
            if (sep == std::string::npos && token.empty()) { break; }
            if (!token.empty()) {
                // 引用三重语义：存在性→owner 类别匹配→owner 型号匹配。
                const auto it = curves.find(token);
                if (it == curves.end()) {
                    CatalogIssue iss = makeIssue(std::string{kSelCatalogRefDangling},
                                                 t.fileName, rowNo, "curve_ref",
                                                 "curve_ref 引用的曲线不存在（引用悬空）");
                    iss.modelId = modelId;
                    iss.actualText = token;
                    rep.issues.push_back(std::move(iss));
                } else if (it->second.ownerKind != ownerKindExpected
                           || it->second.ownerModelId != modelId) {
                    CatalogIssue iss = makeIssue(std::string{kSelCatalogRefDangling},
                                                 t.fileName, rowNo, "curve_ref",
                                                 "curve_ref 引用的曲线 owner 不匹配"
                                                 "（期望 " + std::string(ownerKindExpected)
                                                 + "/" + modelId + "）");
                    iss.modelId = modelId;
                    iss.actualText = token;
                    iss.expectedText = std::string(ownerKindExpected) + "/" + modelId;
                    rep.issues.push_back(std::move(iss));
                } else {
                    // 同 quantity 歧义检测（§6.4——同用途多曲线＝导入拒绝）。
                    for (const auto& used : usedQuantities) {
                        if (used.first == it->second.xQuantity
                            && used.second == it->second.yQuantity) {
                            CatalogIssue iss = makeIssue(
                                std::string{kSelCatalogSchemaMismatch}, t.fileName, rowNo,
                                "curve_ref", "同条目引用同量纲用途（"
                                + it->second.xQuantity + "/" + it->second.yQuantity
                                + "）的多条曲线——用途歧义（卡 §6.4）");
                            iss.modelId = modelId;
                            rep.issues.push_back(std::move(iss));
                            break;   // 同对一次报出
                        }
                    }
                    usedQuantities.push_back({it->second.xQuantity, it->second.yQuantity});
                }
            }
            if (sep == std::string::npos) { break; }
            begin = sep + 1;
        }
    }
}

}  // namespace

// =====================================================================
// tryMakePerformanceCurve（曲线统一构造入口——卡 §6.1/§6.3 全表）
// =====================================================================

std::optional<PerformanceCurve> tryMakePerformanceCurve(
    const CurveId& curveId, std::string xQuantity, std::string yQuantity,
    std::string xUnit, std::string yUnit, std::vector<CapabilityPoint> points,
    const CatalogIdentity& catalog, CatalogIssue& reject)
{
    // ① 单位 token 注册性（core 词表——未知单位拒绝不猜测，卡 §5.3 行 2）。
    for (const auto& [token, which] :
         {std::pair<const std::string*, const char*>{&xUnit, "x_unit"},
          std::pair<const std::string*, const char*>{&yUnit, "y_unit"}}) {
        if (!core::UnitToken::find(*token).has_value()) {
            reject = makeIssue(std::string{kSelCatalogUnitInvalid}, kCatalogFileCurves, 0,
                               which, "曲线单位 token 未注册（core Units 词表外）");
            reject.modelId = curveId;
            reject.actualText = *token;
            return std::nullopt;
        }
    }

    // ② 点数下限：< 2（含空/单点）＝区间不合法——单点能力值应走"固定
    // 额定值"口径声明，不得以曲线形态存在（卡 §6.3 行 4/§6.4）。
    if (points.size() < 2) {
        reject = makeIssue(std::string{kSelCurveIntervalInvalid}, kCatalogFileCurves, 0,
                           "curve_id", "曲线点数 " + std::to_string(points.size())
                           + " < 2（单点能力值应走固定额定值口径——卡 §6.4）");
        reject.modelId = curveId;
        return std::nullopt;
    }

    // ③ 逐点有限性（NFR-COR-03——非有限即拒绝，不静默剔除/置零）。
    for (std::size_t i = 0; i < points.size(); ++i) {
        if (!std::isfinite(points[i].x) || !std::isfinite(points[i].y)) {
            reject = makeIssue(std::string{kSelCurveNonfinite}, kCatalogFileCurves, 0,
                               "point_index",
                               "第 " + std::to_string(i + 1) + " 点含非有限坐标（NaN/±Inf）");
            reject.modelId = curveId;
            return std::nullopt;
        }
    }

    // ④ 点序校验（提交序上逐相邻对——不代排序：排序会掩盖目录错误，
    // 卡 §6.3 行 1 注）。先相等（DUP-X）后下降（UNORDERED），报首个。
    for (std::size_t i = 0; i + 1 < points.size(); ++i) {
        const double x0 = points[i].x;
        const double x1 = points[i + 1].x;
        if (x1 == x0) {
            reject = makeIssue(std::string{kSelCurveDupX}, kCatalogFileCurves, 0, "x_value",
                               "第 " + std::to_string(i + 1) + "/"
                               + std::to_string(i + 2) + " 点重复横坐标 x="
                               + std::to_string(x0));
            reject.modelId = curveId;
            return std::nullopt;
        }
        if (x1 < x0) {
            reject = makeIssue(std::string{kSelCurveUnordered}, kCatalogFileCurves, 0,
                               "x_value", "第 " + std::to_string(i + 1) + "/"
                               + std::to_string(i + 2) + " 点横坐标未升序（"
                               + std::to_string(x0) + " → " + std::to_string(x1)
                               + "）——要求目录修正，构造入口不代排序");
            reject.modelId = curveId;
            return std::nullopt;
        }
    }

    // ⑤ 全部通过——组装曲线并计算点集内容身份（卡 §6.1）。
    PerformanceCurve curve;
    curve.curveId = curveId;
    curve.xQuantity = std::move(xQuantity);
    curve.yQuantity = std::move(yQuantity);
    curve.xUnit = std::move(xUnit);
    curve.yUnit = std::move(yUnit);
    curve.points = std::move(points);
    curve.catalog = catalog;
    curve.contentIdentity = computeCurveContentIdentity(curve);
    return curve;
}

// =====================================================================
// CatalogImporter::validate（§5.3 全表编排——schema 级抛出＋行级报告）
// =====================================================================

CatalogValidationReport CatalogImporter::validate(const ParsedCatalogInput& parsed,
                                                  const CatalogManifest& manifest) const
{
    // 第一段：schema 级前置（致命项抛出；清单级 issue 收入报告）。
    CatalogValidationReport rep = checkManifest(manifest);
    requireParsedTables(parsed);

    // 取四表与字典（requireParsedTables/checkManifest 已保证存在）。
    const ParsedFileTable* motors = parsed.find(kCatalogFileMotors);
    const ParsedFileTable* gearboxes = parsed.find(kCatalogFileGearboxes);
    const ParsedFileTable* curves = parsed.find(kCatalogFileCurves);
    const ParsedFileTable* compat = parsed.find(kCatalogFileCompatibility);
    const auto dictFor = [&manifest](const std::string& f) -> const FieldDictionary& {
        for (const FieldDictionary& d : manifest.fieldDictionary) {
            if (d.targetFile == f) { return d; }
        }
        // 不可达（checkManifest 已抛）；防御返回静态空字典。
        static const FieldDictionary kEmpty;
        return kEmpty;
    };

    // 第二段：逐表 schema 列比对（mismatch 表跳过行级——列位不可靠，
    // 避免级联噪音；报告保留定位）。
    std::map<std::string, std::string> motorIds;    // modelId → 行签名
    std::map<std::string, std::string> gearboxIds;
    const bool motorsOk = checkTableSchema(*motors, dictFor(kCatalogFileMotors), rep);
    const bool gearboxesOk = checkTableSchema(*gearboxes, dictFor(kCatalogFileGearboxes), rep);
    const bool curvesOk = checkTableSchema(*curves, dictFor(kCatalogFileCurves), rep);
    const bool compatOk = checkTableSchema(*compat, dictFor(kCatalogFileCompatibility), rep);

    // 第三段：行级校验（ID 集先行——曲线/兼容表的引用校验依赖主表 ID 集；
    // 主表各自独立，顺序＝文件清单序，确定性）。
    if (motorsOk)    { validateMotorRows(*motors, motorIds, rep); }
    if (gearboxesOk) { validateGearboxRows(*gearboxes, gearboxIds, rep); }
    std::map<std::string, CurveGroup> curveGroups;
    if (curvesOk) {
        curveGroups = validateCurveRows(*curves, motorIds, gearboxIds, rep);
    }
    if (compatOk) {
        validateCompatRows(*compat, motorIds, gearboxIds, rep);
    }

    // 第四段：文件间引用语义——条目 curve_ref → 曲线组（存在/owner 匹配/
    // 同 quantity 歧义）。仅在两表 schema 均通过时执行（任一失配则行位
    // 不可靠）。
    if (motorsOk && curvesOk) {
        validateEntryCurveRefs(*motors, curveGroups, kCurveOwnerMotor, rep);
    }
    if (gearboxesOk && curvesOk) {
        validateEntryCurveRefs(*gearboxes, curveGroups, kCurveOwnerGearbox, rep);
    }

    // 第五段：报告稳定序（file → rowNo → column → code——NFR-COR-02；
    // 卡 §5.3 逐项定位的读取纪律）。
    std::sort(rep.issues.begin(), rep.issues.end(),
              [](const CatalogIssue& a, const CatalogIssue& b) {
                  if (a.file != b.file) { return a.file < b.file; }
                  if (a.rowNo != b.rowNo) { return a.rowNo < b.rowNo; }
                  if (a.column != b.column) { return a.column < b.column; }
                  return a.code < b.code;
              });
    return rep;
}

// =====================================================================
// CatalogImporter::assemble（校验通过后的快照装配——卡 §5.1 流程段）
// =====================================================================

namespace {

/// 主表数值列取值（装配期；validate 已保证可解析——失败即内部一致性
/// 破坏，防御抛出，NFR-COR-03 不静默）。
double assembleNumber(const ParsedFileTable& t, std::size_t rowIdx,
                      const std::map<std::string, std::size_t>& colIdx,
                      const std::string& column)
{
    double v = 0.0;
    const std::string_view raw = trimView(cellAt(t, rowIdx, colIdx.at(column)));
    if (!parseFiniteDouble(raw, v)) {
        throw std::invalid_argument("SEL-CATALOG(assemble): 列 " + column
                                    + " 在校验通过后无法解析（内部一致性破坏——防御拒绝）");
    }
    return v;
}

}  // namespace

CatalogPackageSnapshot CatalogImporter::assemble(const ParsedCatalogInput& parsed,
                                                 const CatalogManifest& manifest) const
{
    // 第零步：装配前置＝校验通过（把"先 validate 后 assemble"的调用方
    // 约定变为机器可查——未校验/未通过即调用方契约违约，fail-fast）。
    const CatalogValidationReport rep = validate(parsed, manifest);
    if (!rep.ok()) {
        CatalogIssue first;
        if (!rep.issues.empty()) { first = rep.issues.front(); }
        throw std::invalid_argument(
            "SEL-CATALOG(assemble): 校验未通过（" + std::to_string(rep.issues.size())
            + " 项；首项 " + first.code + " @ " + first.file + ":"
            + std::to_string(first.rowNo) + "）——先 validate 后 assemble");
    }

    CatalogPackageSnapshot snap;
    snap.manifest = manifest;

    const ParsedFileTable* motors = parsed.find(kCatalogFileMotors);
    const ParsedFileTable* gearboxes = parsed.find(kCatalogFileGearboxes);
    const ParsedFileTable* curves = parsed.find(kCatalogFileCurves);
    const ParsedFileTable* compat = parsed.find(kCatalogFileCompatibility);

    const auto dictFor = [&manifest](const std::string& f) -> const FieldDictionary& {
        for (const FieldDictionary& d : manifest.fieldDictionary) {
            if (d.targetFile == f) { return d; }
        }
        static const FieldDictionary kEmpty;
        return kEmpty;
    };

    // ---- 曲线组重建（装配需要点集/owner——validate 内部结构不出接口，
    // 此处按已验证数据重建；失败面为防御分支）。
    const auto motorCols = buildColumnIndex(*motors);
    const auto gearboxCols = buildColumnIndex(*gearboxes);
    const auto curveCols = buildColumnIndex(*curves);

    // 分组收集曲线行（validate 已验证词表/单位/点序——此处直接装组）。
    struct RawCurveRow {
        double x, y;
        std::int64_t pointIdx;
    };
    std::map<std::string, std::vector<RawCurveRow>> byCurve;
    std::map<std::string, std::array<std::string, 6>> curveMeta;  // owner/oid/xQ/yQ/xU/yU
    for (std::size_t r = 0; r < rowCountOf(*curves); ++r) {
        const std::string cid(trimView(cellAt(*curves, r, curveCols.at("curve_id"))));
        byCurve[cid].push_back({assembleNumber(*curves, r, curveCols, "x_value"),
                                assembleNumber(*curves, r, curveCols, "y_value"),
                                [&] {
                                    std::int64_t p = 0;
                                    const std::string_view v = trimView(cellAt(
                                        *curves, r, curveCols.at("point_index")));
                                    (void)parseInt64(v, p);   // validate 已保证成功
                                    return p;
                                }()});
        curveMeta[cid] = {std::string(trimView(cellAt(*curves, r, curveCols.at("owner_kind")))),
                          std::string(trimView(cellAt(*curves, r, curveCols.at("owner_model_id")))),
                          std::string(trimView(cellAt(*curves, r, curveCols.at("x_quantity")))),
                          std::string(trimView(cellAt(*curves, r, curveCols.at("y_quantity")))),
                          std::string(trimView(cellAt(*curves, r, curveCols.at("x_unit")))),
                          std::string(trimView(cellAt(*curves, r, curveCols.at("y_unit"))))};
    }
    for (auto& [cid, pts] : byCurve) {
        std::stable_sort(pts.begin(), pts.end(),
                         [](const RawCurveRow& a, const RawCurveRow& b) {
                             return a.pointIdx < b.pointIdx;
                         });
        const auto& meta = curveMeta[cid];
        std::vector<CapabilityPoint> points;
        points.reserve(pts.size());
        for (const RawCurveRow& p : pts) {
            points.push_back({p.x, p.y});
        }
        CatalogIssue reject;
        std::optional<PerformanceCurve> curve = tryMakePerformanceCurve(
            cid, meta[2], meta[3], meta[4], meta[5], std::move(points),
            manifest.identity, reject);
        if (!curve.has_value()) {
            // 校验通过后构造仍失败＝内部一致性破坏（防御——NFR-COR-03）。
            throw std::invalid_argument("SEL-CATALOG(assemble): 曲线 " + cid
                                        + " 构造失败（" + reject.code
                                        + "）——内部一致性破坏，防御拒绝");
        }
        snap.curves.push_back(std::move(*curve));
    }

    // ---- 电机条目映射（列→业务字段；缺失列入 missing 清单）。
    for (std::size_t r = 0; r < rowCountOf(*motors); ++r) {
        MotorCatalogEntry m;
        m.modelId = std::string(trimView(cellAt(*motors, r, motorCols.at("model_id"))));
        m.vendor = std::string(trimView(cellAt(*motors, r, motorCols.at("vendor"))));
        m.displayName = std::string(trimView(cellAt(*motors, r, motorCols.at("display_name"))));
        m.ratedTorque = assembleNumber(*motors, r, motorCols, "rated_torque_nm");
        m.peakTorque = assembleNumber(*motors, r, motorCols, "peak_torque_nm");
        m.ratedSpeed = assembleNumber(*motors, r, motorCols, "rated_speed");
        m.maxSpeed = assembleNumber(*motors, r, motorCols, "max_speed");
        m.ratedPower = assembleNumber(*motors, r, motorCols, "rated_power_w");
        m.dutyClass = std::string(trimView(cellAt(*motors, r, motorCols.at("duty_class"))));
        m.rotorInertia = assembleNumber(*motors, r, motorCols, "rotor_inertia_kgm2");
        m.mass = assembleNumber(*motors, r, motorCols, "mass_kg");

        // 可缺失字段（validate 已放行缺失——逐一显式标记）。
        const auto optionalNum = [&](const char* col) -> std::optional<double> {
            const std::string_view v = trimView(cellAt(*motors, r, motorCols.at(col)));
            if (v.empty()) {
                m.missing.push_back({col, "cell-empty"});
                return std::nullopt;
            }
            return assembleNumber(*motors, r, motorCols, col);
        };
        if (const auto t = optionalNum("overload_torque_nm")) {
            m.overload = OverloadSpec{};
            m.overload->torque = *t;
            if (const auto d = optionalNum("overload_duration_s")) {
                m.overload->duration = *d;
            }
        } else {
            // 转矩缺失则时长不构成完整过载规格——时长列若非空仍记 missing
            // 由 optionalNum 的空判定处理；两列独立缺失语义一致（显式标记）。
            (void)optionalNum("overload_duration_s");
        }
        m.ratedVoltage = optionalNum("rated_voltage_v");
        m.brakeTorque = optionalNum("brake_torque_nm");
        m.holdingTorque = optionalNum("holding_torque_nm");
        if (const auto rt = optionalNum("thermal_ref_temp")) {
            ThermalDerating th;
            th.refTemp = *rt;
            if (const auto f = optionalNum("thermal_factor_per_ref")) {
                th.factorPerRef = *f;
                m.thermal = th;
            }
        } else {
            (void)optionalNum("thermal_factor_per_ref");
        }
        {
            const std::string_view fk = trimView(cellAt(*motors, r, motorCols.at("mounting_flange_kind")));
            const std::string_view sk = trimView(cellAt(*motors, r, motorCols.at("mounting_shaft_kind")));
            if (fk.empty()) { m.missing.push_back({"mounting_flange_kind", "cell-empty"}); }
            if (sk.empty()) { m.missing.push_back({"mounting_shaft_kind", "cell-empty"}); }
            m.mounting.flangeKind = std::string(fk);
            m.mounting.shaftKind = std::string(sk);
        }

        // 曲线引用（分号分隔；validate 已验存在/owner/歧义——此处直接映射）。
        const std::string refText(trimView(cellAt(*motors, r, motorCols.at("curve_ref"))));
        if (!refText.empty()) {
            std::size_t begin = 0;
            while (begin <= refText.size()) {
                const std::size_t sep = refText.find(';', begin);
                const std::string token = std::string(trimView(std::string_view(refText)
                    .substr(begin, sep == std::string::npos ? std::string::npos
                                                            : sep - begin)));
                if (sep == std::string::npos && token.empty()) { break; }
                if (!token.empty()) {
                    const auto& meta = curveMeta.at(token);   // validate 已保证存在
                    m.curves.push_back({token, meta[2], meta[3]});
                }
                if (sep == std::string::npos) { break; }
                begin = sep + 1;
            }
        }

        m.catalog = manifest.identity;
        m.status = m.missing.empty() ? ValidationStatus::Valid : ValidationStatus::Partial;
        snap.motors.push_back(std::move(m));
    }

    // ---- 减速器条目映射（同电机结构）。
    for (std::size_t r = 0; r < rowCountOf(*gearboxes); ++r) {
        GearboxCatalogEntry g;
        g.modelId = std::string(trimView(cellAt(*gearboxes, r, gearboxCols.at("model_id"))));
        g.vendor = std::string(trimView(cellAt(*gearboxes, r, gearboxCols.at("vendor"))));
        g.displayName =
            std::string(trimView(cellAt(*gearboxes, r, gearboxCols.at("display_name"))));
        g.ratedOutputTorque =
            assembleNumber(*gearboxes, r, gearboxCols, "rated_output_torque_nm");
        g.peakOutputTorque =
            assembleNumber(*gearboxes, r, gearboxCols, "peak_output_torque_nm");
        g.maxInputSpeed = assembleNumber(*gearboxes, r, gearboxCols, "max_input_speed");
        g.ratio = assembleNumber(*gearboxes, r, gearboxCols, "ratio");
        g.efficiency = assembleNumber(*gearboxes, r, gearboxCols, "efficiency");
        g.mountingOrientation =
            std::string(trimView(cellAt(*gearboxes, r, gearboxCols.at("mounting_orientation"))));
        g.mass = assembleNumber(*gearboxes, r, gearboxCols, "mass_kg");

        const auto optionalNum = [&](const char* col) -> std::optional<double> {
            const std::string_view v = trimView(cellAt(*gearboxes, r, gearboxCols.at(col)));
            if (v.empty()) {
                g.missing.push_back({col, "cell-empty"});
                return std::nullopt;
            }
            return assembleNumber(*gearboxes, r, gearboxCols, col);
        };
        g.backlash = optionalNum("backlash");
        g.ratedLife = optionalNum("rated_life");
        g.housingInertia = optionalNum("housing_inertia_kgm2");
        {
            const auto rad = optionalNum("external_load_radial_n");
            const auto ax = optionalNum("external_load_axial_n");
            const auto dist = optionalNum("external_load_dist_m");
            if (rad.has_value() && ax.has_value() && dist.has_value()) {
                g.extLoad = ExternalLoadSpec{*rad, *ax, *dist};
            }
        }
        {
            const std::string_view fk =
                trimView(cellAt(*gearboxes, r, gearboxCols.at("mounting_flange_kind")));
            const std::string_view sk =
                trimView(cellAt(*gearboxes, r, gearboxCols.at("mounting_shaft_kind")));
            if (fk.empty()) { g.missing.push_back({"mounting_flange_kind", "cell-empty"}); }
            if (sk.empty()) { g.missing.push_back({"mounting_shaft_kind", "cell-empty"}); }
            g.mounting.flangeKind = std::string(fk);
            g.mounting.shaftKind = std::string(sk);
        }

        const std::string refText(trimView(cellAt(*gearboxes, r, gearboxCols.at("curve_ref"))));
        if (!refText.empty()) {
            std::size_t begin = 0;
            while (begin <= refText.size()) {
                const std::size_t sep = refText.find(';', begin);
                const std::string token = std::string(trimView(std::string_view(refText)
                    .substr(begin, sep == std::string::npos ? std::string::npos
                                                            : sep - begin)));
                if (sep == std::string::npos && token.empty()) { break; }
                if (!token.empty()) {
                    const auto& meta = curveMeta.at(token);   // validate 已保证存在
                    g.curves.push_back({token, meta[2], meta[3]});
                }
                if (sep == std::string::npos) { break; }
                begin = sep + 1;
            }
        }

        g.catalog = manifest.identity;
        g.status = g.missing.empty() ? ValidationStatus::Valid : ValidationStatus::Partial;
        snap.gearboxes.push_back(std::move(g));
    }

    // ---- 兼容关系映射（逐行直映；validate 已验引用/矛盾）。
    const auto compatCols = buildColumnIndex(*compat);
    for (std::size_t r = 0; r < rowCountOf(*compat); ++r) {
        CompatibilityRecord rec;
        rec.motorId =
            std::string(trimView(cellAt(*compat, r, compatCols.at("motor_model_id"))));
        rec.gearboxId =
            std::string(trimView(cellAt(*compat, r, compatCols.at("gearbox_model_id"))));
        rec.mountKind = std::string(trimView(cellAt(*compat, r, compatCols.at("mount_kind"))));
        snap.compatibility.push_back(std::move(rec));
    }

    // ---- 确定性排序（快照内存序＝canonical 序——NFR-COR-02；身份关系
    // 表的稳定 ID 序，卡 §4.3）。
    std::sort(snap.motors.begin(), snap.motors.end(),
              [](const MotorCatalogEntry& a, const MotorCatalogEntry& b) {
                  return a.modelId < b.modelId;
              });
    std::sort(snap.gearboxes.begin(), snap.gearboxes.end(),
              [](const GearboxCatalogEntry& a, const GearboxCatalogEntry& b) {
                  return a.modelId < b.modelId;
              });
    std::sort(snap.curves.begin(), snap.curves.end(),
              [](const PerformanceCurve& a, const PerformanceCurve& b) {
                  return a.curveId < b.curveId;
              });
    std::sort(snap.compatibility.begin(), snap.compatibility.end(),
              [](const CompatibilityRecord& a, const CompatibilityRecord& b) {
                  if (a.motorId != b.motorId) { return a.motorId < b.motorId; }
                  if (a.gearboxId != b.gearboxId) { return a.gearboxId < b.gearboxId; }
                  return a.mountKind < b.mountKind;
              });

    // ---- 内容身份回填（包身份＝业务数据 canonical 摘要——canonical 文本
    // 不含身份字段本身，回填不改变摘要输入；卡 §4.2）。
    snap.contentIdentity = computePackageContentIdentity(snap);
    snap.manifest.identity.contentIdentity = snap.contentIdentity;
    return snap;
}

}  // namespace sdurws::ird::selection
