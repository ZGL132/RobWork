/**
 * @file   Import.cpp
 * @brief  需求导入器的实现——字段字典冻结表装配、表头自动识别、单位
 *         预览与 SI 归一、CSV/JSON 逐行映射（行级部分成功）、副本导出。
 *
 * 设计依据：units/requirements.md §7.3/§7.4/§7.5/§9.5/§9.6（T04 行四码）、
 * §10.2（V-08/V-09/V-10）；需求 REQ-05/REQ-12/NFR-SEC-03/AT-02/AT-24/
 * NFR-COR-01/02/03；任务契约 tasks/foundation/WP-14-T04.json acceptance
 * 1~6。实现决策（ExportTarget 等价调整/CSV 摘要口径/行号口径/ObjectId
 * 待分配/去重基准/JSON 未知键拒绝）登记于 Import.hpp 文件头注与本文件
 * 各实现点注释。
 *
 * 线程安全：全部函数无共享可变状态（栈上局部＋只读成员），可重入——
 * const 方法并发调用安全（§3.4 纯函数服务总约定）。
 * 确定性（NFR-COR-01/02）：数值解析/回写 std::from_chars/to_chars
 * （general 格式，不经 locale）；诊断收集按"字典字段序→行序"稳定排列；
 * 无时钟/随机/环境依赖——同输入同输出。
 */

#include <sdurws/ird/requirements/Import.hpp>

#include <sdurws/ird/core/Provenance.hpp>              // ValueProvenance::make/SourcedValue——来源与四态
#include <sdurws/ird/core/Units.hpp>                   // UnitToken/tryConvert——唯一换算入口（SA-12）
#include <sdurws/ird/io/Json.hpp>                      // io 受限 DOM/JsonProfile 注册制/canonical 化
#include <sdurws/ird/requirements/DiagCodes.hpp>       // REQ-IMPORT-* 码常量（唯一书写点）
#include <sdurws/ird/requirements/Errors.hpp>          // requirementErrorCodeToken——终检失败原因
#include <sdurws/ird/requirements/RequirementTypes.hpp>  // 词表 try 轨/validateTaskPoint——草稿终检

#include <algorithm>
#include <array>
#include <charconv>     // std::from_chars/to_chars——locale 无关数值（确定性来源）
#include <cmath>        // std::isfinite
#include <map>
#include <stdexcept>    // std::invalid_argument——调用方契约违约 fail-fast
#include <utility>

namespace sdurws::ird::requirements {

namespace {

// =====================================================================
// 基础助手（全部纯函数）
// =====================================================================

/// 冻结表内 ImportField 的序号（数组索引面——枚举底层值即卡面原文序）。
std::size_t fieldIndex(ImportField field) noexcept
{
    return static_cast<std::size_t>(field);
}

/// 数值→文本（std::to_chars general 最短往返——与 io canonical 数值同一
/// 格式族；回写文本可被 tryParseNumber 无损还原，roundtrip 位精确）。
std::string numberToText(double value)
{
    char buffer[64];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value,
                                      std::chars_format::general);
    return std::string{buffer, static_cast<std::size_t>(result.ptr - buffer)};
}

/**
 * @brief 数值解析（std::from_chars general 格式——不经 locale，确定性来源；
 *        NFR-COR-03：解析失败/非有限值返回 nullopt，不静默转 0）。
 *
 * @param text [in] 单元格原文（精确——不剥离空白：空白属数据，解析失败
 *             即行级错误原文回显，表格污染由报告暴露而非静默纠错）
 * @return 有限数值；空串/词法非法/非有限＝nullopt
 */
std::optional<double> tryParseNumber(std::string_view text) noexcept
{
    if (text.empty()) {
        return std::nullopt;  // 空串＝未提供（调用方区分"空单元格"与"非法数值"）
    }
    double value = 0.0;
    const auto* first = text.data();
    const auto* last = text.data() + text.size();
    // general 格式接受定点与科学计数（"1.5"/"1e-3"），不接受前导空白与
    // 十六进制；ptr!=last 说明有残留字符（"1.2m"）——不猜测截断。
    const auto result = std::from_chars(first, last, value, std::chars_format::general);
    if (result.ec != std::errc{} || result.ptr != last) {
        return std::nullopt;
    }
    if (!std::isfinite(value)) {
        return std::nullopt;  // 非有限值拒绝（NFR-COR-03——不静默转 0）
    }
    return value;
}

/// 数值列的 SI 目标单位 token（Length→"m"、Angle→"rad"——§7.3 落库口径）。
std::string_view siTargetToken(ImportColumnKind kind) noexcept
{
    return kind == ImportColumnKind::Length ? std::string_view{"m"} : std::string_view{"rad"};
}

/// 单位换算（core 唯一入口——SA-12/NFR-COR-03）：try 轨，任一失败＝nullopt。
std::optional<double> tryConvertSi(double value, const std::string& declaredUnit,
                                   ImportColumnKind kind)
{
    const auto from = core::UnitToken::find(declaredUnit);
    const auto to = core::UnitToken::find(siTargetToken(kind));
    if (!from.has_value() || !to.has_value()) {
        return std::nullopt;  // 词表外 token（resolveUnit 已拦，此处防御）
    }
    return core::tryConvert(value, *from, *to);
}

/// 列声明的运行时解析产物（声明补全＋合法性——mapCsv/preview 共用，语义单源）。
struct ResolvedUnit {
    ImportColumnKind kind;  ///< 列种类（换算 SI 目标的依据）
    std::string token;      ///< 实际生效单位 token（声明补全后——预览展示面）
    bool usable = false;    ///< 是否可换算（token 词表内且量纲与列种类一致）
};

/**
 * @brief 列单位声明解析（§7.3"长度列默认 m、角度列默认 rad，可按列声明"）。
 *
 * 校验链：①Text/Number 列声明非空＝种类不符（不可用）；②空声明按种类
 * 补缺省（Length→"m"、Angle→"rad"）；③token 必须在 core UnitToken 注册
 * 表内且量纲匹配（Length↔Length、Angle↔Angle——x 列声明 deg＝违约）。
 * 任一违约均不猜测修正（NFR-COR-03），由调用方产 REQ-IMPORT-UNIT-ILLEGAL。
 */
ResolvedUnit resolveUnit(ImportColumnKind kind, const std::string& declared)
{
    ResolvedUnit out;
    out.kind = kind;
    if (kind == ImportColumnKind::Text || kind == ImportColumnKind::Number) {
        out.token = declared;         // 无单位列：声明必须为空（声明即种类误用）
        out.usable = declared.empty();
        return out;
    }
    out.token = declared.empty() ? std::string{siTargetToken(kind)} : declared;
    const auto unit = core::UnitToken::find(out.token);
    if (!unit.has_value()) {
        return out;  // 词表外 token——不可用（unregistered-token）
    }
    const auto expected = kind == ImportColumnKind::Length ? core::QuantityKind::Length
                                                           : core::QuantityKind::Angle;
    out.usable = unit->kind() == expected;  // 量纲匹配才可用（dimension-mismatch）
    return out;
}

/// 全字段声明解析（两通道共用装配前置——JSON 无声明即全缺省）。
std::array<ResolvedUnit, kImportFieldCount>
resolveAllUnits(const FieldDictionary& dictionary, const ImportUnitOptions& units)
{
    std::array<ResolvedUnit, kImportFieldCount> out{};
    for (const auto& specField : dictionary.fields()) {
        out[fieldIndex(specField.field)]
            = resolveUnit(specField.kind, units.unitFor(specField.field));
    }
    return out;
}

/// 单位声明违约的机器可判原因（诊断 cause 文本面）。
std::string_view unitFailureReason(const ResolvedUnit& unit)
{
    if ((unit.kind == ImportColumnKind::Text || unit.kind == ImportColumnKind::Number)
        && !unit.token.empty()) {
        return "declaration-on-unitless-column";  // 无单位列被声明了单位
    }
    return core::UnitToken::find(unit.token).has_value() ? "dimension-mismatch"
                                                         : "unregistered-token";
}

/// 列种类的人读名（诊断 cause 文本面）。
std::string_view columnKindName(ImportColumnKind kind) noexcept
{
    switch (kind) {
    case ImportColumnKind::Length: return "长度";
    case ImportColumnKind::Angle: return "角度";
    case ImportColumnKind::Number: return "无单位数值";
    case ImportColumnKind::Text: return "文本";
    }
    return "?";  // 不可达（全枚举 switch）
}

/// core::DiagnosticRecord 构造（C-3 工厂——context/cause/action 非空由本
/// 处字面保证；码语法经 DiagData 工厂校验，违约即实现缺陷 fail-fast）。
core::DiagnosticRecord makeDiag(std::string_view code, std::string context,
                                std::string cause, std::string action)
{
    return core::DiagnosticRecord::make(std::string{code}, std::nullopt,
                                        std::nullopt, std::nullopt,
                                        std::move(context), std::move(cause),
                                        std::move(action));
}

/**
 * @brief 行级错误诊断（REQ-IMPORT-ROW-ERROR；AT-02 定位三要素）。
 *
 * context 承载三要素文本：row=<行号>; column=<列名>; raw=<原文>——原文
 * 为开发级内容（成为用户可见文案前须经 diagnostics 脱敏，IoError.hpp 同
 * 源约束）；subject 置空（导入期条目尚无正式 ObjectId——O-36 口径）；
 * rowNo==0 为文档级哨兵（JSON 整档失败——io 通道无部分成功，§5.9.4）。
 */
core::DiagnosticRecord makeRowError(std::uint64_t rowNo, std::string_view columnName,
                                    std::string_view raw, std::string cause)
{
    std::string context = "row=" + std::to_string(rowNo);
    context += "; column=";
    context += columnName.empty() ? std::string_view{"-"} : columnName;
    context += "; raw=";
    context += raw;
    return makeDiag(kReqImportRowError, std::move(context), std::move(cause),
                    "修正该行数据后重新导入");
}

/// 重复键行级错误（REQ-IMPORT-DUPLICATE-ID——id/name 两去重键共用一码，
/// §9.6 行语义"导入重复 id/name"；context 携首次出现行供去重修正）。
core::DiagnosticRecord makeDuplicateError(std::uint64_t rowNo, std::string_view columnName,
                                          std::string_view value, std::uint64_t firstRow)
{
    std::string context = "row=" + std::to_string(rowNo);
    context += "; column=";
    context += columnName;
    context += "; value=";
    context += value;
    context += "; first-row=";
    context += std::to_string(firstRow);
    return makeDiag(kReqImportDuplicateId, std::move(context),
                    "条目 " + std::string{columnName} + " 重复（首次出现于行 "
                        + std::to_string(firstRow) + "）——集合内唯一（I-REQ-3）",
                    "去重或改名后重新导入");
}

/// 悬空 Frame 引用警告（REQ-IMPORT-FRAME-UNKNOWN——warning，条目保留待
/// 解析；跨闭包半区核对归 T05 就绪层 R2，§8.1 浅校验边界）。
core::DiagnosticRecord makeFrameUnknownWarning(std::uint64_t rowNo, std::string_view raw)
{
    std::string context = "row=" + std::to_string(rowNo);
    context += "; column=ref_frame; raw=";
    context += raw;
    return makeDiag(kReqImportFrameUnknown, std::move(context),
                    "参考系引用悬空（导入方无修订闭包，无法核验目标存在性——"
                    "§8.1 浅校验边界）；条目已保留，待命令应用后就绪校验复核",
                    "核对该模型坐标系引用；或改用 World");
}

/// 导入条目的来源标记（§4.3 source 行 ImportMapped——Import.hpp 单点）。
core::ValueProvenance importSource()
{
    return core::ValueProvenance::make(core::ProvenanceKind::ImportMapped);
}

/// RawTable 规范投影摘要（CSV 通道 sourceDigest 单点——Import.hpp 头注
/// 实现决策 2）：表头原文与各行原文按 \x1F（单元分隔符）串接、行间 \n，
/// 经 core::ContentDigester（SHA-256 唯一摘要算法，SA-12/D-05）——内容
/// 相同的表必得同摘要（CON-05 内容寻址一致口径）。
core::Digest256 digestRawTable(const io::RawTable& table)
{
    core::ContentDigester digester;
    const auto absorb = [&digester](std::string_view text) {
        digester.update(text.data(), text.size());
    };
    for (const auto& name : table.report.header) {
        absorb(name);
        absorb("\n");
    }
    for (const auto& row : table.rows) {
        for (std::size_t i = 0; i < row.size(); ++i) {
            if (i != 0) {
                absorb("\x1F");
            }
            absorb(row[i]);
        }
        absorb("\n");
    }
    return digester.finalize();
}

// =====================================================================
// 行装配（mapCsv/mapJson 共用的语义单源——§7.4"语义校验/映射同 §7.3"）
// =====================================================================

/// "字段→原始文本"视图的槽位（present 与空串正交——"未出现"≠"空值"）。
using FieldSlot = std::pair<bool, std::string>;

/**
 * @brief 单行/单记录的字段值视图（通道无关）。
 *
 * CSV＝单元格原文直填；JSON＝标量的稳定文本形（数值经 numberToText 回
 * 写——可被 tryParseNumber 无损还原）。字段级校验全部经本视图，两通道
 * 共用同一 assemblePoint（NFR-MNT-04 语义单源）。
 */
struct FieldValues {
    std::array<FieldSlot, kImportFieldCount> slots{};  ///< 每字段槽位（缺省未出现）

    bool has(ImportField field) const noexcept { return slots[fieldIndex(field)].first; }
    const std::string& textOf(ImportField field) const { return slots[fieldIndex(field)].second; }
    void set(ImportField field, std::string text)
    {
        auto& slot = slots[fieldIndex(field)];
        slot.first = true;
        slot.second = std::move(text);
    }
};

/// 单条目的装配与校验产物。
struct ParsedRow {
    bool ok = false;            ///< true＝装配成功（已过 validateTaskPoint 终检）
    TaskPoint point{};          ///< 草稿条目（objectId 全零待命令分配——O-36）
    bool frameWarning = false;  ///< ref_frame 走了"裸 ObjectId→悬空警告"分支
    std::string frameRaw;       ///< 悬空引用原文（REQ-IMPORT-FRAME-UNKNOWN 回显）
};

/**
 * @brief 字段值 → TaskPoint 草稿（逐字段语义校验——两通道唯一实现点）。
 *
 * 处理序（确定性——行级错误按字典字段序收集，AT-02"错误行逐条"）：
 * 必备四项（id/name/x/y/z）→ 词表字段（level/enabled/process_tag/
 * approach_axis）→ 引用字段（ref_frame/tcp）→ 角度列 → 位置/容差/段距
 * 离/裕量 → note → validateTaskPoint 终检。任一行级错误即整行丢弃
 * （ok=false），但该行其余字段错误**继续收集**（逐条呈现，一次导入暴
 * 露全部问题——减少用户往返）。
 *
 * @param values        [in] 字段值视图（通道无关）
 * @param rowNo         [in] 行号（1 起；JSON＝记录序号——溯源 recordNumber）
 * @param resolvedUnits [in] 已解析单位表（resolveAllUnits 产物，按字段索引）
 * @param rowErrors     [in,out] 行级错误收集（追加）
 * @return 装配产物（ok=false 时 point 无效——调用方不得取用）
 */
ParsedRow assemblePoint(const FieldValues& values, std::uint64_t rowNo,
                        const std::array<ResolvedUnit, kImportFieldCount>& resolvedUnits,
                        std::vector<core::DiagnosticRecord>& rowErrors)
{
    ParsedRow out;
    bool hasError = false;
    // 行级错误收集器（列名＝字段 canonical 名——冻结表原文，定位一致）。
    const auto fail = [&](ImportField field, std::string_view raw, std::string cause) {
        hasError = true;
        rowErrors.push_back(makeRowError(rowNo, importFieldToken(field), raw,
                                         std::move(cause)));
    };
    // 数值＋单位归一共用入口（raw 数值可解析且列单位可用才产出 SI 值）。
    const auto parseSi = [&](ImportField field) -> std::optional<double> {
        const auto raw = tryParseNumber(values.textOf(field));
        const auto& unit = resolvedUnits[fieldIndex(field)];
        if (!raw.has_value() || !unit.usable) {
            return std::nullopt;
        }
        return tryConvertSi(*raw, unit.token, unit.kind);
    };

    // ---- 必备四项：id/name/x/y/z（整列缺失已在上游结构级拦截；此处拦
    // "列在而单元格空/非法"——行级错误，整行丢弃）----
    if (!values.has(ImportField::Id) || values.textOf(ImportField::Id).empty()) {
        fail(ImportField::Id, values.textOf(ImportField::Id),
             "必备字段 id 缺失或为空（id＝导入批次内去重键；正式 ObjectId 归命令分配）");
    }
    if (!values.has(ImportField::Name) || values.textOf(ImportField::Name).empty()) {
        fail(ImportField::Name, values.textOf(ImportField::Name),
             "必备字段 name 缺失或为空（I-REQ-3 非空唯一）");
    }
    std::array<std::optional<double>, 3> positionSi{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const auto field = static_cast<ImportField>(fieldIndex(ImportField::X) + axis);
        const auto& raw = values.textOf(field);
        positionSi[axis] = parseSi(field);
        if (!values.has(field) || raw.empty() || !positionSi[axis].has_value()) {
            const auto& unit = resolvedUnits[fieldIndex(field)];
            fail(field, raw,
                 "必备位置分量缺失或数值非法/单位无法换算（长度列，声明单位 "
                     + (unit.usable ? unit.token : "非法:" + unit.token) + "）");
        }
    }

    // ---- 词表字段（词表外值＝行级错误——§7.3"level 非法"分支同族；
    // 空单元格＝字典缺省〔与"可选列缺失→默认值"同语义——不判词表外〕）----
    TaskPoint probe;  // 缺省值即字典缺省（level=Must/enabled=true/processTag=Generic…）
    if (values.has(ImportField::Level) && !values.textOf(ImportField::Level).empty()) {
        if (const auto level = tryRequirementLevel(values.textOf(ImportField::Level));
            level.has_value()) {
            probe.level = *level;
        } else {
            fail(ImportField::Level, values.textOf(ImportField::Level),
                 "需求等级词表外（合法值 Must|Should，精确等值）");
        }
    }
    if (values.has(ImportField::Enabled) && !values.textOf(ImportField::Enabled).empty()) {
        const auto& raw = values.textOf(ImportField::Enabled);
        if (raw == "true") {
            probe.enabled = true;
        } else if (raw == "false") {
            probe.enabled = false;
        } else {
            fail(ImportField::Enabled, raw,
                 "启用值词表外（合法值 true|false，精确等值）");
        }
    }
    if (values.has(ImportField::ProcessTag)
        && !values.textOf(ImportField::ProcessTag).empty()) {
        if (const auto tag = tryProcessTag(values.textOf(ImportField::ProcessTag));
            tag.has_value()) {
            probe.processTag = *tag;
        } else {
            fail(ImportField::ProcessTag, values.textOf(ImportField::ProcessTag),
                 "工艺标签词表外（11 值词表——ObjectTypes.hpp ProcessTag）");
        }
    }
    if (values.has(ImportField::ApproachAxis)
        && !values.textOf(ImportField::ApproachAxis).empty()) {
        if (const auto axis = trySegmentAxis(values.textOf(ImportField::ApproachAxis));
            axis.has_value()) {
            probe.approach.axis = *axis;
            probe.retract.axis = *axis;  // 接近/撤离同轴（§5.1 同一进退轴语义）
        } else {
            fail(ImportField::ApproachAxis, values.textOf(ImportField::ApproachAxis),
                 "进退轴词表外（合法值 ToolZ|ReferenceZ）");
        }
    }

    // ---- ref_frame（引用文本语法——Import.hpp RefFrame 枚举注）----
    if (values.has(ImportField::RefFrame) && !values.textOf(ImportField::RefFrame).empty()) {
        const auto& raw = values.textOf(ImportField::RefFrame);
        if (raw == "World") {
            probe.refFrame = RequirementReference{};  // World 缺省种
        } else if (raw.rfind("model:", 0) == 0 || raw.rfind("scene:", 0) == 0) {
            const bool isModel = raw.rfind("model:", 0) == 0;
            if (auto oid = core::ObjectId::tryFromCanonical(raw.substr(6))) {
                probe.refFrame.kind = isModel ? RequirementRefKind::ModelFrame
                                              : RequirementRefKind::SceneObject;
                probe.refFrame.objectId = std::move(oid);
            } else {
                fail(ImportField::RefFrame, raw,
                     "引用前缀后的 ObjectId 规范文本非法（期望 obj-<32hex>）");
            }
        } else if (core::ObjectId::tryFromCanonical(raw).has_value()) {
            // 裸 ObjectId＝外部表最常见形态：结构可解析、目标存在性本单元
            // 不可核验（§8.1 浅校验边界——无闭包）→按 ModelFrame 保留＋
            // 悬空警告（warning，可保留待解析——§9.6 行语义）。
            probe.refFrame.kind = RequirementRefKind::ModelFrame;
            probe.refFrame.objectId = core::ObjectId::tryFromCanonical(raw);
            out.frameWarning = true;
            out.frameRaw = raw;
        } else {
            fail(ImportField::RefFrame, raw,
                 "参考系引用文本非法（World|model:<obj->|scene:<obj->|裸 <obj->）");
        }
    }

    // ---- tcp（引用文本语法——Import.hpp Tcp 枚举注）----
    if (values.has(ImportField::Tcp) && !values.textOf(ImportField::Tcp).empty()) {
        const auto& raw = values.textOf(ImportField::Tcp);
        if (raw == "DefaultTcp") {
            probe.tcpRef = RequirementReference{};
            probe.tcpRef->kind = RequirementRefKind::DefaultTcp;
        } else if (raw.rfind("tool:", 0) == 0) {
            // "tool:<oid>|<tcpKey>"——'|' 分隔（ObjectId 规范文本不含 '|'，
            // 分隔无歧义）；tcpKey 空＝wellFormed 违约（I-REQ-4）。
            const auto body = raw.substr(5);
            const auto sep = body.find('|');
            if (sep == std::string::npos) {
                fail(ImportField::Tcp, raw, "tool: 引用缺少 '|' 分隔的 tcpKey");
            } else if (auto oid = core::ObjectId::tryFromCanonical(body.substr(0, sep));
                       oid.has_value() && !body.substr(sep + 1).empty()) {
                probe.tcpRef = RequirementReference{};
                probe.tcpRef->kind = RequirementRefKind::Tool;
                probe.tcpRef->objectId = std::move(oid);
                probe.tcpRef->tcpKey = body.substr(sep + 1);
            } else {
                fail(ImportField::Tcp, raw, "tool: 引用的 ObjectId 或 tcpKey 非法");
            }
        } else {
            fail(ImportField::Tcp, raw,
                 "TCP 引用文本非法（DefaultTcp|tool:<obj->|<tcpKey>）");
        }
    }

    // ---- 角度列（roll/pitch/yaw：出现即受约束；未出现＝自由分量——
    // Fixed 规则参数保持缺省零，自由分量字面值不进入任何工程判定，不构
    // 成"缺失转零"改写——NFR-COR-03 边界声明）----
    for (const auto field :
         {ImportField::Roll, ImportField::Pitch, ImportField::Yaw}) {
        if (!values.has(field) || values.textOf(field).empty()) {
            continue;  // 未提供＝该分量自由（缺省值语义）
        }
        if (const auto si = parseSi(field); si.has_value()) {
            switch (field) {
            case ImportField::Roll:
                probe.pose.constrainedDof.roll = true;
                probe.pose.orientation.fixedRpy[0] = *si;  // rad（Z-Y-X 欧拉 roll 分量）
                break;
            case ImportField::Pitch:
                probe.pose.constrainedDof.pitch = true;
                probe.pose.orientation.fixedRpy[1] = *si;  // rad
                break;
            case ImportField::Yaw:
                probe.pose.constrainedDof.yaw = true;
                probe.pose.orientation.fixedRpy[2] = *si;  // rad
                break;
            default:
                break;  // 不可达（循环面仅三角度列）
            }
        } else {
            fail(field, values.textOf(field), "角度数值非法或单位无法换算（rad 落库）");
        }
    }

    // ---- 位置（必备三分量——提供即受约束，I-REQ-5 至少一真的满足面；
    // SI 值一次装配，缺值分支已由必备四项收集）----
    probe.pose.constrainedDof.x = probe.pose.constrainedDof.y
        = probe.pose.constrainedDof.z = true;
    if (positionSi[0].has_value() && positionSi[1].has_value() && positionSi[2].has_value()) {
        // refFrame 系下表达，单位 m（SI 归一——§7.3"落库前经 core 唯一换
        // 算归一 SI"）；来源＝ImportMapped（core Provenance P-1 校验通过）。
        probe.pose.position = core::SourcedValue<rw::math::Vector3D<double>>::provided(
            rw::math::Vector3D<double>(*positionSi[0], *positionSi[1], *positionSi[2]),
            importSource());
    }

    // ---- 容差（空＝字典缺省 1e-3 m / π/180 rad——RequirementTypes 设计
    // 默认；非空即解析＋归一，非法＝行级错误）----
    const auto parseTolerance = [&](ImportField field, double& target,
                                    std::string_view unitName) {
        if (!values.has(field) || values.textOf(field).empty()) {
            return;  // 缺省值已在 probe 缺省构造中就位
        }
        if (const auto si = parseSi(field); si.has_value()) {
            target = *si;
        } else {
            fail(field, values.textOf(field),
                 std::string{unitName} + "数值非法或单位无法换算（>0 有限——I-REQ-5）");
        }
    };
    parseTolerance(ImportField::PosTol, probe.tolerance.positionTolerance, "位置容差");
    parseTolerance(ImportField::OriTol, probe.tolerance.orientationTolerance, "姿态容差");

    // ---- 接近/撤离段（距离空＝段关闭；出现即启用＋单位归一——§5.1）----
    const auto assembleSegment = [&](ImportField distField, TaskSegment& segment) {
        if (!values.has(distField) || values.textOf(distField).empty()) {
            return;  // 缺省关闭（approach/retract.enabled=false——占位距离 0）
        }
        if (const auto si = parseSi(distField); si.has_value()) {
            segment.enabled = true;
            segment.distanceM = *si;  // m（SI 归一；>0 终检复核）
        } else {
            fail(distField, values.textOf(distField),
                 "段距离数值非法或单位无法换算（m 落库，>0——启用段校验）");
        }
    };
    assembleSegment(ImportField::ApproachDist, probe.approach);
    assembleSegment(ImportField::RetractDist, probe.retract);

    // ---- 关节裕量（Number 列——无单位换算，rad/m 依关节类型原样落库；
    // 空＝未提供 optional）----
    if (values.has(ImportField::MinJointMargin)
        && !values.textOf(ImportField::MinJointMargin).empty()) {
        if (const auto raw = tryParseNumber(values.textOf(ImportField::MinJointMargin));
            raw.has_value()) {
            probe.demands.minimumJointMargin = *raw;
        } else {
            fail(ImportField::MinJointMargin, values.textOf(ImportField::MinJointMargin),
                 "关节裕量数值非法");
        }
    }

    // ---- note（原文承载——不经任何转写）----
    if (values.has(ImportField::Note)) {
        probe.note = values.textOf(ImportField::Note);
    }

    if (hasError) {
        return out;  // 行级错误已收集——整行丢弃（部分成功语义，AT-02）
    }

    // ---- 装配收尾：名称/来源/占位 work 段＋终检（validateTaskPoint 与
    // 编辑器/编解码校验链语义单源，NFR-MNT-04——前置拦截后理论不可失败，
    // 失败即实现缺陷，如实产行级错误暴露）----
    probe.name = values.textOf(ImportField::Name);
    probe.source = importSource();
    // work 段即任务点本身（§4.3"enabled 恒 true，占位表达段序"）——占位
    // 距离 1.0 m 不进入任何工程判定（Services createPoint 同款装配）。
    probe.work = TaskSegment{/*enabled=*/true, /*axis=*/SegmentAxis::ToolZ,
                             /*distanceM=*/1.0};
    if (const auto e = validateTaskPoint(probe)) {
        fail(ImportField::Id, values.textOf(ImportField::Id),
             "条目终检未通过（" + std::string{requirementErrorCodeToken(e->code)}
                 + "）——实现缺陷或数据组合越界，请附带原始数据报告");
        return out;
    }
    out.ok = true;
    out.point = std::move(probe);
    return out;
}

/**
 * @brief 行集装配收尾（mapCsv/mapJson 共用）：去重判定＋溯源注入＋条目
 *        收集——两通道唯一实现点。
 *
 * 去重基准＝**装配成功的行**（错误行已丢弃、不占用去重键——否则错误行
 * 会"毒化"同键的正确行，与"正确行保留"（AT-02）相悖；name 去重同时是
 * I-REQ-3 集合内唯一的预保证）。同键多行：首行保留、后续行逐条产
 * REQ-IMPORT-DUPLICATE-ID（行序稳定，首现行号可溯）。
 *
 * @param rows          [in] 各行装配产物（行序）
 * @param idOf/nameOf   [in] 行的 id/name 原值（去重键与诊断回显）
 * @param sourceDigest  [in] 源内容摘要（注入 importProvenance——I-REQ-8）
 * @param rowErrors     [in,out] 行级错误收集（重复键错误追加）
 * @param frameWarnings [in,out] 悬空 Frame 引用（行号＋原文；调用方转 warning）
 * @return 草稿条目（文档序——确定性；objectId 全零待命令分配）
 */
std::vector<TaskPoint> collectEntries(const std::vector<ParsedRow>& rows,
                                      const std::vector<std::string>& idOf,
                                      const std::vector<std::string>& nameOf,
                                      const core::Digest256& sourceDigest,
                                      std::vector<core::DiagnosticRecord>& rowErrors,
                                      std::vector<std::pair<std::uint64_t, std::string>>& frameWarnings)
{
    std::vector<TaskPoint> entries;
    // 去重登记表：键值→首次出现行（1 起）。仅装配成功的行登记/被查——
    // 见函数头注"去重基准"。
    std::map<std::string, std::uint64_t> firstIdRow;
    std::map<std::string, std::uint64_t> firstNameRow;

    for (std::size_t i = 0; i < rows.size(); ++i) {
        const std::uint64_t rowNo = static_cast<std::uint64_t>(i) + 1;  // 行号 1 起
        if (!rows[i].ok) {
            continue;  // 错误行已产行级错误——不参与去重（见函数头注）
        }
        // 悬空 Frame 引用先登记（warning 与该行保留并存——§9.6 行语义）。
        if (rows[i].frameWarning) {
            frameWarnings.emplace_back(rowNo, rows[i].frameRaw);
        }
        // id/name 去重（同行双重复＝两条独立诊断——逐条呈现）。
        bool duplicate = false;
        if (const auto it = firstIdRow.find(idOf[i]); it != firstIdRow.end()) {
            rowErrors.push_back(makeDuplicateError(rowNo, importFieldToken(ImportField::Id),
                                                   idOf[i], it->second));
            duplicate = true;
        } else {
            firstIdRow.emplace(idOf[i], rowNo);
        }
        if (const auto it = firstNameRow.find(nameOf[i]); it != firstNameRow.end()) {
            rowErrors.push_back(makeDuplicateError(rowNo, importFieldToken(ImportField::Name),
                                                   nameOf[i], it->second));
            duplicate = true;
        } else {
            firstNameRow.emplace(nameOf[i], rowNo);
        }
        if (duplicate) {
            continue;  // 重复行丢弃（与既有正确行冲突——部分成功语义）
        }
        // 溯源注入（I-REQ-8：摘要＋记录号；无路径字段——路径不入身份）。
        TaskPoint entry = rows[i].point;  // 值拷贝后注入（入参只读）
        entry.importProvenance = ImportProvenance{sourceDigest, rowNo};
        entries.push_back(std::move(entry));
    }
    return entries;
}

// =====================================================================
// JSON 通道支撑（profile 注册制——§7.4"解析经 io JSON 通道"）
// =====================================================================

/// 本单元 JSON 导入 profile 标识（§5.9.2 注册制——requirements 是该格式
/// 的所有者，装配期注册进 io 注册表）。
constexpr std::string_view kImportProfileId = "ird-requirements-import/1";

/// 形态约束 JsonShape 的便捷构造（仅 type 约束——其余成员缺省）。
std::shared_ptr<const io::JsonShape> typeShape(io::JsonValueType type)
{
    auto shape = std::make_shared<io::JsonShape>();
    shape->type = type;
    return shape;
}

/// 构造导入 profile（版本判定先于 schema 校验——io §5.9.2）：根对象必含
/// schemaVersion（Integer，io 版本门判定 supportedVersions {1}）与 points
/// （Array）；draft（Boolean）可选——草稿副本标记观测面。顶层未知键按 io
/// 缺省 Reject 拒绝（NFR-DEP-04——不静默吞字段）；points 元素不加 io 级
/// 键约束（记录键集为 canonical＋别名开放集，由 assemblePoint 做字段级
/// 语义校验——未知键在记录层以行级错误暴露，NFR-DEP-04 同口径）。
io::JsonProfile makeImportProfile()
{
    io::JsonProfile profile;
    profile.profileId.assign(kImportProfileId);
    profile.supportedVersions = {1};  // 升序非空（注册表前置）——未来版本走
                                      // IO-FORMAT-JSON-VERSION-FUTURE 稳定拒绝
    io::JsonProperty schemaVersion;
    schemaVersion.key = "schemaVersion";
    schemaVersion.required = true;
    schemaVersion.shape = typeShape(io::JsonValueType::Integer);
    io::JsonProperty draft;
    draft.key = "draft";
    draft.required = false;
    draft.shape = typeShape(io::JsonValueType::Boolean);
    io::JsonProperty points;
    points.key = "points";
    points.required = true;
    points.shape = typeShape(io::JsonValueType::Array);
    profile.rootShape.type = io::JsonValueType::Object;
    profile.rootShape.properties = {schemaVersion, draft, points};
    return profile;
}

/// JSON 标量→稳定文本形（数值经 numberToText——tryParseNumber 可无损还
/// 原；String/Boolean 原文直取；其余形态由调用方先行类型判定）。
std::string scalarText(const io::JsonValue& value)
{
    switch (value.type) {
    case io::JsonValue::Type::String:
        return value.stringValue;
    case io::JsonValue::Type::Integer:
        return std::to_string(value.integerValue);
    case io::JsonValue::Type::Real:
        return numberToText(value.realValue);
    case io::JsonValue::Type::Boolean:
        return value.boolValue ? "true" : "false";
    default:
        return {};  // Null/容器不应到达（调用方已判定）——空文本触发行级错误
    }
}

}  // namespace

// =====================================================================
// 字段字典（§7.3 冻结表——WP-14-T01 交付物零改动承接）
// =====================================================================

std::string_view importFieldToken(ImportField field) noexcept
{
    switch (field) {
    case ImportField::Id: return "id";
    case ImportField::Name: return "name";
    case ImportField::ProcessTag: return "process_tag";
    case ImportField::Level: return "level";
    case ImportField::Enabled: return "enabled";
    case ImportField::RefFrame: return "ref_frame";
    case ImportField::Tcp: return "tcp";
    case ImportField::X: return "x";
    case ImportField::Y: return "y";
    case ImportField::Z: return "z";
    case ImportField::Roll: return "roll";
    case ImportField::Pitch: return "pitch";
    case ImportField::Yaw: return "yaw";
    case ImportField::PosTol: return "pos_tol";
    case ImportField::OriTol: return "ori_tol";
    case ImportField::ApproachAxis: return "approach_axis";
    case ImportField::ApproachDist: return "approach_dist";
    case ImportField::RetractDist: return "retract_dist";
    case ImportField::MinJointMargin: return "min_joint_margin";
    case ImportField::Note: return "note";
    }
    return "";  // 不可达（全枚举 switch——防御性空串）
}

FieldDictionary::FieldDictionary()
{
    // 冻结表装配（§7.3 原文序；别名＝中英文常见表头写法，精确等值匹配；
    // required＝卡面"必填列缺失→结构级拒绝"的 id/name/位置三分量）。
    const auto text = ImportColumnKind::Text;
    const auto len = ImportColumnKind::Length;
    const auto ang = ImportColumnKind::Angle;
    const auto num = ImportColumnKind::Number;
    fields_ = {
        {ImportField::Id, true, text, {"ID", "Id", "编号", "条目ID"}},
        {ImportField::Name, true, text, {"Name", "NAME", "名称", "点名"}},
        {ImportField::ProcessTag, false, text, {"process", "工艺", "工艺标签"}},
        {ImportField::Level, false, text, {"Level", "等级", "需求等级"}},
        {ImportField::Enabled, false, text, {"Enabled", "启用", "是否启用"}},
        {ImportField::RefFrame, false, text, {"frame", "参考系", "参考坐标系"}},
        {ImportField::Tcp, false, text, {"TCP", "工具", "工具TCP"}},
        {ImportField::X, true, len, {"X", "坐标X"}},
        {ImportField::Y, true, len, {"Y", "坐标Y"}},
        {ImportField::Z, true, len, {"Z", "坐标Z"}},
        {ImportField::Roll, false, ang, {"Roll", "滚转"}},
        {ImportField::Pitch, false, ang, {"Pitch", "俯仰"}},
        {ImportField::Yaw, false, ang, {"Yaw", "偏航"}},
        {ImportField::PosTol, false, len, {"位置容差", "位置公差"}},
        {ImportField::OriTol, false, ang, {"姿态容差", "姿态公差"}},
        {ImportField::ApproachAxis, false, text, {"接近轴", "进退轴"}},
        {ImportField::ApproachDist, false, len, {"接近距离", "进刀距离"}},
        {ImportField::RetractDist, false, len, {"撤回距离", "退刀距离"}},
        {ImportField::MinJointMargin, false, num, {"最小关节裕量", "关节裕量"}},
        {ImportField::Note, false, text, {"Note", "备注", "说明"}},
    };
}

const FieldSpec& FieldDictionary::spec(ImportField field) const noexcept
{
    return fields_[fieldIndex(field)];
}

std::optional<ImportField> FieldDictionary::tryRecognize(std::string_view header) const noexcept
{
    // canonical 名优先（冻结表原文直取）；别名次之（精确等值——不折叠
    // 大小写、不剥离空白，NFR-COR-03 不改写用户输入）。
    for (const auto& specField : fields_) {
        if (importFieldToken(specField.field) == header) {
            return specField.field;
        }
    }
    for (const auto& specField : fields_) {
        for (const auto& alias : specField.aliases) {
            if (alias == header) {
                return specField.field;
            }
        }
    }
    return std::nullopt;
}

// =====================================================================
// 表头自动识别（§7.3"字段字典自动识别"域侧实现）
// =====================================================================

AutoDetectResult autoDetectMapping(const io::RawTable& table,
                                   const FieldDictionary& dictionary)
{
    if (!table.report.hasHeader) {
        // 无表头的表无法自动识别（调用方应手动逐列 assign）——契约违约
        // fail-fast（AGENTS.md 错误语义：调用方错误）。
        throw std::invalid_argument(
            "requirements/import: autoDetectMapping 要求带表头的 RawTable"
            "（report.hasHeader==false）");
    }
    AutoDetectResult out;
    out.mapping = FieldMapping::none();
    // 逐列识别；同一字段命中多列＝首列胜出（先到先得——确定性），后到
    // 列计入未识别清单（向导可手动改映射消解）。
    std::array<bool, kImportFieldCount> claimed{};
    for (std::size_t col = 0; col < table.report.header.size(); ++col) {
        const auto field = dictionary.tryRecognize(table.report.header[col]);
        if (!field.has_value() || claimed[fieldIndex(*field)]) {
            out.unrecognizedColumns.push_back(col);
            continue;
        }
        claimed[fieldIndex(*field)] = true;
        out.mapping.assign(*field, col);
    }
    return out;
}

// =====================================================================
// 单位预览（§7.3"换算预览"——与 mapCsv 同一声明校验/换算入口，语义单源）
// =====================================================================

std::vector<UnitPreviewEntry> previewUnitConversion(const io::RawTable& table,
                                                    const FieldMapping& mapping,
                                                    const ImportUnitOptions& units,
                                                    const FieldDictionary& dictionary)
{
    // 表宽＝表头（若有）与最宽行的较大者（无表头直构表按行宽核对）。
    std::size_t width = table.report.header.size();
    for (const auto& row : table.rows) {
        width = std::max(width, row.size());
    }
    // 前置：列号越界即调用方契约违约（与 mapCsv 同款 fail-fast 口径）。
    if (width != 0) {
        for (const auto& specField : dictionary.fields()) {
            if (mapping.isMapped(specField.field)
                && mapping.columnFor(specField.field) >= width) {
                throw std::invalid_argument(
                    "requirements/import: FieldMapping 列号越界（字段 "
                        + std::string{importFieldToken(specField.field)} + "）");
            }
        }
    }
    std::vector<UnitPreviewEntry> out;
    // 仅长度/角度列参与预览（Text 无单位语义、Number 不换算——§7.3 单位
    // 语义只覆盖两类数量列）。
    for (const auto& specField : dictionary.fields()) {
        if (specField.kind != ImportColumnKind::Length
            && specField.kind != ImportColumnKind::Angle) {
            continue;
        }
        if (!mapping.isMapped(specField.field)) {
            continue;  // 未映射列无预览对象（缺列报告由 mapCsv 承担）
        }
        UnitPreviewEntry entry;
        entry.field = specField.field;
        const auto resolved = resolveUnit(specField.kind, units.unitFor(specField.field));
        entry.declaredUnit = resolved.token;
        entry.unitUsable = resolved.usable;
        // 首个非空样本（行序扫描——确定性；原文照录，预览不改写）。
        for (const auto& row : table.rows) {
            const auto col = mapping.columnFor(specField.field);
            if (col >= row.size() || row[col].empty()) {
                continue;  // 行宽不足/空单元格——找下一个非空样本
            }
            entry.rawSample = row[col];
            if (const auto value = tryParseNumber(row[col]);
                value.has_value() && resolved.usable) {
                entry.siSample = tryConvertSi(*value, resolved.token, specField.kind);
            }
            break;  // 只取首个非空样本（预览语义——全量校验归 mapCsv）
        }
        out.push_back(std::move(entry));
    }
    return out;
}

// =====================================================================
// mapCsv（§9.5——io RawTable 为唯一输入，纯函数）
// =====================================================================

ImportOutcome RequirementImporter::mapCsv(const io::RawTable& table,
                                          const FieldMapping& mapping,
                                          const ImportUnitOptions& units,
                                          std::vector<core::DiagnosticRecord>& diags) const
{
    // ---- 前置校验（调用方契约违约 fail-fast）----
    // RawTable 行集与计数一致性：rows 是 retainRows=true 的读取产物（或
    // 等价直构）——dataRows>0 而 rows 空＝流式丢弃行后误入，静默返回空
    // 导入比显式失败危险（fail-fast）。
    if (table.report.dataRows > 0 && table.rows.empty()) {
        throw std::invalid_argument(
            "requirements/import: RawTable.rows 为空而 report.dataRows>0"
            "——读取时须 retainRows=true（io CsvReadOptions）");
    }
    // 表宽＝表头（若有）与最宽行的较大者（无表头直构表按行宽核对）。
    std::size_t width = table.report.header.size();
    for (const auto& row : table.rows) {
        width = std::max(width, row.size());
    }
    if (width != 0) {
        for (const auto& specField : dictionary_.fields()) {
            if (mapping.isMapped(specField.field)
                && mapping.columnFor(specField.field) >= width) {
                throw std::invalid_argument(
                    "requirements/import: FieldMapping 列号越界（字段 "
                        + std::string{importFieldToken(specField.field)} + "，表宽 "
                        + std::to_string(width) + "）");
            }
        }
    }

    ImportOutcome out;
    out.sourceDigest = digestRawTable(table);  // 内容寻址摘要（实现决策 2）

    // ---- 结构级校验：必备列映射齐全（id/name/x/y/z——缺任一＝该文件不
    // 可导入，§7.3"必填列缺失→结构级拒绝"；码＝REQ-IMPORT-UNIT-ILLEGAL
    // 的"列缺失"分支，§9.6 行语义）----
    {
        std::string missing;
        for (const auto& specField : dictionary_.fields()) {
            if (specField.required && !mapping.isMapped(specField.field)) {
                if (!missing.empty()) {
                    missing += ", ";
                }
                missing += importFieldToken(specField.field);
            }
        }
        if (!missing.empty()) {
            diags.push_back(makeDiag(
                kReqImportUnitIllegal, "row=0; column=-; raw=-",
                "必备列缺失（" + missing + "）——结构级拒绝，该文件不可导入（§7.3）",
                "补齐必备列映射或修正表头后重新导入"));
            return out;  // status 维持 Rejected；entries 恒空
        }
    }

    // ---- 单位声明逐列解析（违约→REQ-IMPORT-UNIT-ILLEGAL 逐列诊断；该
    // 列各行按"单位无法换算"行级错误处置——行不静默丢弃，走可见错误面）----
    const auto resolvedUnits = resolveAllUnits(dictionary_, units);
    for (const auto& specField : dictionary_.fields()) {
        if (!mapping.isMapped(specField.field)) {
            continue;  // 未映射可选列走缺省（defaulted 报告）——无单位可校
        }
        const auto& unit = resolvedUnits[fieldIndex(specField.field)];
        if (!unit.usable) {
            diags.push_back(makeDiag(
                kReqImportUnitIllegal,
                "row=0; column=" + std::string{importFieldToken(specField.field)},
                "单位声明无法换算（声明 " + unit.token + "，原因 "
                    + std::string{unitFailureReason(unit)} + "；列种类 "
                    + std::string{columnKindName(specField.kind)} + "）",
                "修正该列单位声明（长度 m/mm、角度 rad/deg）后重新导入"));
        }
    }

    // ---- 未映射/多余列 → 忽略项清单（"不静默丢弃"——§7.3；列名取表头
    // 原文，无表头时以列号文本呈现）----
    {
        std::vector<bool> mappedCol(width, false);
        for (const auto& specField : dictionary_.fields()) {
            if (mapping.isMapped(specField.field)
                && mapping.columnFor(specField.field) < width) {
                mappedCol[mapping.columnFor(specField.field)] = true;
            }
        }
        for (std::size_t col = 0; col < width; ++col) {
            if (!mappedCol[col]) {
                out.ignoredColumns.push_back(
                    col < table.report.header.size()
                        ? table.report.header[col]
                        : "#" + std::to_string(col));
            }
        }
    }

    // ---- 文件级缺列的可选字段 → 缺省补全报告（defaultedFields；缺省值
    // 语义见 assemblePoint——行内空单元格同走缺省，报告按列粒度）----
    for (const auto& specField : dictionary_.fields()) {
        if (!specField.required && !mapping.isMapped(specField.field)) {
            out.defaultedFields.push_back(std::string{importFieldToken(specField.field)});
        }
    }

    // ---- 逐行装配（部分成功——错误行不阻断正确行，AT-02）----
    std::vector<ParsedRow> rows;
    rows.reserve(table.rows.size());
    std::vector<std::string> idOf;
    idOf.reserve(table.rows.size());
    std::vector<std::string> nameOf;
    nameOf.reserve(table.rows.size());
    for (std::size_t i = 0; i < table.rows.size(); ++i) {
        FieldValues values;
        for (const auto& specField : dictionary_.fields()) {
            if (!mapping.isMapped(specField.field)) {
                continue;  // 未映射可选列＝缺省（已报告）；必备列缺列已结构级拒绝
            }
            const auto col = mapping.columnFor(specField.field);
            const auto& cell = table.rows[i];
            // 行宽不足的尾列按空单元格处置（io PadTrailing 语义的防御性
            // 对齐——不越界读，空值走缺省/必备错误面）。
            values.set(specField.field, col < cell.size() ? cell[col] : std::string{});
        }
        idOf.push_back(values.has(ImportField::Id) ? values.textOf(ImportField::Id)
                                                   : std::string{});
        nameOf.push_back(values.has(ImportField::Name) ? values.textOf(ImportField::Name)
                                                       : std::string{});
        rows.push_back(assemblePoint(values, static_cast<std::uint64_t>(i) + 1,
                                     resolvedUnits, out.rowErrors));
    }

    // ---- 去重判定＋溯源注入＋条目收集（两通道共用收尾）----
    std::vector<std::pair<std::uint64_t, std::string>> frameWarnings;
    out.entries = collectEntries(rows, idOf, nameOf, out.sourceDigest, out.rowErrors,
                                 frameWarnings);
    for (const auto& [rowNo, raw] : frameWarnings) {
        diags.push_back(makeFrameUnknownWarning(rowNo, raw));  // warning 不阻断
    }

    // ---- 状态归并（ImportOutcome 注三分语义）----
    out.status = out.rowErrors.empty() ? ImportOutcome::Status::Completed
                                       : ImportOutcome::Status::Partial;
    return out;
}

// =====================================================================
// mapJson（§9.5——io JSON 通道为唯一解析面，profile 注册制）
// =====================================================================

ImportOutcome RequirementImporter::mapJson(const std::vector<std::uint8_t>& jsonBytes,
                                           std::vector<core::DiagnosticRecord>& diags) const
{
    if (jsonBytes.empty()) {
        // 空字节＝无文档（调用方未读文件）——调用方契约违约 fail-fast。
        throw std::invalid_argument("requirements/import: mapJson 输入字节为空");
    }

    ImportOutcome out;
    // 源摘要＝输入字节原文的 SHA-256（文件头注实现决策 2——mapJson 直接
    // 持有字节，与 CSV 的 RawTable 投影口径通道内自洽）。
    {
        core::ContentDigester digester;
        digester.update(jsonBytes.data(), jsonBytes.size());
        out.sourceDigest = digester.finalize();
    }

    // ---- io 通道解析（profile 注册制——本单元自持注册表：格式所有者装
    // 配期注册、io 执行校验，R-1 合规；reader 无状态可每次构造）----
    auto registry = std::make_shared<io::JsonProfileRegistry>();
    registry->registerProfile(makeImportProfile());
    const auto reader = io::makeStructuredDataReader(std::move(registry));
    const std::string profileId{kImportProfileId};
    io::JsonReadOptions options;
    options.profileId = &profileId;
    // 预算/取消：单文档内存解析（bytes 已在内存，文档级预算由 reader 以
    // 产品缺省规格自开内部 scope；本函数无长操作——不可取消）。
    const auto parsed = reader->parseBytes(
        std::string_view{reinterpret_cast<const char*>(jsonBytes.data()), jsonBytes.size()},
        options, /*budget=*/nullptr, /*cancel=*/nullptr);
    if (!parsed) {
        // 文档级失败（io 通道无部分成功，§5.9.4 IO-D10）——结构级拒绝；
        // row=0 哨兵＝非单行错误（makeRowError 注）。
        diags.push_back(makeRowError(0, "-", "-",
                                     "JSON 文档解析/版本/结构校验失败（io 通道）"));
        return out;  // status 维持 Rejected
    }
    const io::JsonDocument& doc = parsed.value;

    // ---- draft 标记观测（§7.4"草稿导出带 draft:true 标记防误当正式数
    // 据"——导入侧透传为观测面，不改变导入语义）----
    if (const auto* draft = doc.root.findMember("draft");
        draft != nullptr && draft->isBoolean()) {
        out.sourceMarkedDraft = draft->boolValue;
    }

    // ---- points 数组逐记录装配（同 §7.3 语义；points 形态由 profile 保
    // 证为 Array；单位＝全缺省——JSON 文档不携带列级单位声明，SI 之外数
    // 值按 SI 直读）----
    const auto resolvedUnits = resolveAllUnits(dictionary_, ImportUnitOptions::defaults());
    std::vector<ParsedRow> rows;
    std::vector<std::string> idOf;
    std::vector<std::string> nameOf;
    if (const io::JsonValue* points = doc.root.findMember("points"); points != nullptr) {
        rows.reserve(points->items.size());
        idOf.reserve(points->items.size());
        nameOf.reserve(points->items.size());
        for (std::size_t record = 0; record < points->items.size(); ++record) {
            const std::uint64_t rowNo = static_cast<std::uint64_t>(record) + 1;
            const auto& item = points->items[record];
            if (!item.isObject()) {
                // 记录非对象＝该记录行级错误（其余记录继续——行粒度语义
                // 在记录粒度上的对齐；数组层形态已由 profile 保证）。
                out.rowErrors.push_back(makeRowError(
                    rowNo, "-", "-", "points 记录必须为对象（field:value 映射）"));
                idOf.emplace_back();
                nameOf.emplace_back();
                rows.push_back(ParsedRow{});
                continue;
            }
            // 记录键 → 字段（canonical＋别名识别——同一字段字典 REQ-12）。
            FieldValues values;
            for (const auto& member : item.members) {
                const auto field = dictionary_.tryRecognize(member.key);
                if (!field.has_value()) {
                    // 未知记录键：JSON 为版本化结构文档（NFR-DEP-04 不静默
                    // 吞字段）——行级错误暴露而非 CSV 式忽略清单（顶层未知
                    // 键已被 io profile Reject 拒绝，此处为记录级同口径）。
                    out.rowErrors.push_back(makeRowError(
                        rowNo, member.key, scalarText(member.value),
                        "记录键词表外（字段字典 canonical/别名均未命中）——"
                        "JSON 通道不静默忽略未知字段（NFR-DEP-04）"));
                    continue;
                }
                if (values.has(*field)) {
                    // 同一字段的双键命中（canonical 与别名并存/两别名并存）
                    // ＝映射歧义——行级错误暴露（确定性：文档序首键保留，
                    // 后键报错）。
                    out.rowErrors.push_back(makeRowError(
                        rowNo, member.key, scalarText(member.value),
                        "记录键与先前键映射到同一字段（映射歧义）"));
                    continue;
                }
                // 数值/文本统一转稳定文本形（numberToText 可无损还原——
                // 与 CSV 通道共用 assemblePoint 的解析链）。
                values.set(*field, scalarText(member.value));
            }
            idOf.push_back(values.has(ImportField::Id) ? values.textOf(ImportField::Id)
                                                       : std::string{});
            nameOf.push_back(values.has(ImportField::Name) ? values.textOf(ImportField::Name)
                                                           : std::string{});
            rows.push_back(assemblePoint(values, rowNo, resolvedUnits, out.rowErrors));
        }
    }

    // ---- 去重判定＋溯源注入＋条目收集（与 CSV 同一收尾）----
    std::vector<std::pair<std::uint64_t, std::string>> frameWarnings;
    out.entries = collectEntries(rows, idOf, nameOf, out.sourceDigest, out.rowErrors,
                                 frameWarnings);
    for (const auto& [rowNo, raw] : frameWarnings) {
        diags.push_back(makeFrameUnknownWarning(rowNo, raw));
    }

    out.status = out.rowErrors.empty() ? ImportOutcome::Status::Completed
                                       : ImportOutcome::Status::Partial;
    return out;
}

// =====================================================================
// exportCopy（§7.4——副本导出不影响项目；io 原子写出）
// =====================================================================

ExportOutcome RequirementImporter::exportCopy(const RequirementWorkingSet& ws,
                                              ExportFormat format,
                                              const ExportTarget& target) const
{
    if (target.filePath.empty()) {
        throw std::invalid_argument("requirements/import: exportCopy 目标路径为空");
    }

    // ---- 草稿标记语义（ExportTarget 注）：CSV 无草稿标记通道——值面拒
    // 绝（防草稿数据被误当正式数据的 fail-visible 处置，不静默省略标记）。
    if (format == ExportFormat::Csv && target.draft) {
        return ExportOutcome{false, 0,
                             "CSV 导出不支持草稿标记（无标记通道）——草稿导出请"
                             "使用 JSON 格式（draft:true 标记，§7.4）"};
    }

    // ---- 导出共用：任务点集合（§7.3 字段字典的对称导出面——区域/工况/
    // 计划不在坐标表字典内，其副本走正式工件通道，不在本接口）。
    // id 列：正式 ObjectId 有值＝规范文本；导入草稿（全零）＝合成占位
    // "draft-<条目序>"（去重键非空语义保持——重导入可判重；合成规则确
    // 定性，登记于单元卡 §14.6）。
    const auto idTextOf = [&ws](const TaskPoint& point, std::size_t ordinal) -> std::string {
        if (point.objectId.isValid()) {
            return point.objectId.toCanonical();
        }
        return "draft-" + std::to_string(ordinal + 1);  // 条目序 1 起（确定性）
    };
    // ref_frame/tcp 文本语法（Import.hpp 枚举注的逆变换——往返一致）。
    const auto refFrameTextOf = [](const TaskPoint& point) -> std::string {
        switch (point.refFrame.kind) {
        case RequirementRefKind::ModelFrame:
            return "model:" + point.refFrame.objectId->toCanonical();
        case RequirementRefKind::SceneObject:
            return "scene:" + point.refFrame.objectId->toCanonical();
        default:
            return "World";  // World（DefaultTcp 不属于 refFrame 场景——防御）
        }
    };
    const auto tcpTextOf = [](const TaskPoint& point) -> std::string {
        if (!point.tcpRef.has_value()) {
            return {};  // 未引用＝空单元格（CSV）/缺键（JSON）
        }
        if (point.tcpRef->kind == RequirementRefKind::Tool) {
            return "tool:" + point.tcpRef->objectId->toCanonical() + "|"
                   + point.tcpRef->tcpKey;
        }
        return "DefaultTcp";
    };

    // ---- CSV 通道：io ICsvWriter（唯一转义点 SA-12；文件目标内部"暂存
    // ＋原子替换"——Csv.hpp §9.4 等价承载，失败/放弃时目标不变）----
    if (format == ExportFormat::Csv) {
        auto writer = io::makeCsvWriter();
        auto opened = writer->open(io::CsvOutputTarget::file(target.filePath),
                                   io::CsvWriteOptions{});
        if (!opened) {
            return ExportOutcome{false, 0, "CSV 写出器打开失败（io 通道——目录不存在/占用）"};
        }
        // 表头＝冻结表 canonical 名（字典序＝卡面原文序——与导入识别对称）。
        std::vector<io::IoString> header;
        header.reserve(kImportFieldCount);
        for (const auto& specField : dictionary_.fields()) {
            header.emplace_back(importFieldToken(specField.field));
        }
        if (const auto written = writer->writeHeader(header); !written) {
            return ExportOutcome{false, 0, "CSV 表头写出失败（io 通道）"};
        }
        // 数据行（集合已按 ObjectId 字典序——I-REQ-1，导出序确定性）。
        for (std::size_t ordinal = 0; ordinal < ws.points.entries.size(); ++ordinal) {
            const auto& point = ws.points.entries[ordinal];
            std::vector<io::CsvCell> cells;
            cells.reserve(kImportFieldCount);
            const auto pushText = [&cells](const std::string& text) {
                // 空文本＝Empty 单元格（语义同缺省/未引用——与导入对称）。
                if (text.empty()) {
                    cells.push_back(io::CsvCell::empty());
                } else {
                    cells.push_back(io::CsvCell::fromText(text));
                }
            };
            const auto pushNumber = [&cells](const double* value) {
                // 空＝Empty 单元格；数值经 writer 的 to_chars 最短表示
                //（canonical——同数据两次导出字节相同，NFR-COR-01）。
                if (value != nullptr) {
                    cells.push_back(io::CsvCell::fromReal(*value));
                } else {
                    cells.push_back(io::CsvCell::empty());
                }
            };
            pushText(idTextOf(point, ordinal));
            pushText(point.name);
            pushText(std::string{processTagToken(point.processTag)});
            pushText(std::string{requirementLevelToken(point.level)});
            pushText(point.enabled ? "true" : "false");
            pushText(refFrameTextOf(point));
            pushText(tcpTextOf(point));
            // 位姿三分量（Provided 才写；m SI——与导入缺省单位对称）。
            if (point.pose.position.state() == core::FieldState::Provided) {
                const auto& p = point.pose.position.value();
                pushNumber(&p[0]);
                pushNumber(&p[1]);
                pushNumber(&p[2]);
            } else {
                cells.push_back(io::CsvCell::empty());
                cells.push_back(io::CsvCell::empty());
                cells.push_back(io::CsvCell::empty());
            }
            // 角度三分量（受约束才写；rad SI）。Fixed 规则参数字面（§5.3
            // 等价姿态不归一——导出即参数回显；非 Fixed 规则条目的完整参
            // 数不经坐标表通道——登记于单元卡 §14.6 限制条款）。
            const auto& dof = point.pose.constrainedDof;
            const auto& rpy = point.pose.orientation.fixedRpy;
            pushNumber(dof.roll ? &rpy[0] : nullptr);
            pushNumber(dof.pitch ? &rpy[1] : nullptr);
            pushNumber(dof.yaw ? &rpy[2] : nullptr);
            pushNumber(&point.tolerance.positionTolerance);
            pushNumber(&point.tolerance.orientationTolerance);
            pushText(std::string{segmentAxisToken(point.approach.axis)});
            pushNumber(point.approach.enabled ? &point.approach.distanceM : nullptr);
            pushNumber(point.retract.enabled ? &point.retract.distanceM : nullptr);
            pushNumber(point.demands.minimumJointMargin.has_value()
                           ? &*point.demands.minimumJointMargin
                           : nullptr);
            pushText(point.note);
            if (const auto written = writer->writeRow(cells); !written) {
                return ExportOutcome{false, 0, "CSV 数据行写出失败（io 通道）"};
            }
        }
        if (const auto finished = writer->finish(); !finished) {
            return ExportOutcome{false, 0, "CSV 收尾/原子替换失败（io 通道）"};
        }
        // bytesWritten＝0：CSV 为流式写出（不驻留全量字节——不计逐字节
        // 长度；成功语义由 finish 的原子替换就位承载）。
        return ExportOutcome{true, 0, {}};
    }

    // ---- JSON 通道：io canonicalizeJson（canonical 字节）＋
    // IAtomicFileWriter（prepare→write→commit，OverwriteAtomic——commit
    // 前目标不变，失败时旧文件完好，§7.4）----
    io::JsonDocument doc;
    auto& root = doc.root;
    root.type = io::JsonValue::Type::Object;
    const auto member = [&root](std::string key, io::JsonValue value) {
        io::JsonMember m;
        m.key = std::move(key);
        m.value = std::move(value);
        root.members.push_back(std::move(m));
    };
    const auto numberValue = [](double v) {
        io::JsonValue v2;
        v2.type = io::JsonValue::Type::Real;
        v2.realValue = v;
        return v2;
    };
    const auto textValue = [](std::string s) {
        io::JsonValue v;
        v.type = io::JsonValue::Type::String;
        v.stringValue = std::move(s);
        return v;
    };
    io::JsonValue schemaVersion;
    schemaVersion.type = io::JsonValue::Type::Integer;
    schemaVersion.integerValue = 1;  // 与导入 profile supportedVersions{1} 对称
    member("schemaVersion", std::move(schemaVersion));
    if (target.draft) {
        io::JsonValue draft;
        draft.type = io::JsonValue::Type::Boolean;
        draft.boolValue = true;
        member("draft", std::move(draft));  // §7.4 草稿标记（防误当正式数据）
    }
    io::JsonValue pointsValue;
    pointsValue.type = io::JsonValue::Type::Array;
    for (std::size_t ordinal = 0; ordinal < ws.points.entries.size(); ++ordinal) {
        const auto& point = ws.points.entries[ordinal];
        io::JsonValue record;
        record.type = io::JsonValue::Type::Object;
        const auto fieldMember = [&record](std::string key, io::JsonValue value) {
            io::JsonMember m;
            m.key = std::move(key);
            m.value = std::move(value);
            record.members.push_back(std::move(m));
        };
        const auto fieldText = [&fieldMember, &textValue](std::string key,
                                                          const std::string& s) {
            if (!s.empty()) {
                fieldMember(std::move(key), textValue(s));
            }  // 空文本＝缺键（与 CSV Empty 单元格对称——缺键即缺省/未引用）
        };
        const auto fieldNumber = [&fieldMember, &numberValue](std::string key,
                                                              const double* v) {
            if (v != nullptr) {
                fieldMember(std::move(key), numberValue(*v));
            }  // 未提供＝缺键（JSON 无空单元格语义——缺键即缺省/自由）
        };
        // 键＝canonical 名（重导入经字典识别直达——canonical 优先）。
        fieldText("id", idTextOf(point, ordinal));
        fieldText("name", point.name);
        fieldText("process_tag", std::string{processTagToken(point.processTag)});
        fieldText("level", std::string{requirementLevelToken(point.level)});
        fieldText("enabled", point.enabled ? "true" : "false");
        fieldText("ref_frame", refFrameTextOf(point));
        fieldText("tcp", tcpTextOf(point));
        if (point.pose.position.state() == core::FieldState::Provided) {
            const auto& p = point.pose.position.value();
            fieldNumber("x", &p[0]);
            fieldNumber("y", &p[1]);
            fieldNumber("z", &p[2]);
        }
        const auto& dof = point.pose.constrainedDof;
        const auto& rpy = point.pose.orientation.fixedRpy;
        fieldNumber("roll", dof.roll ? &rpy[0] : nullptr);
        fieldNumber("pitch", dof.pitch ? &rpy[1] : nullptr);
        fieldNumber("yaw", dof.yaw ? &rpy[2] : nullptr);
        fieldNumber("pos_tol", &point.tolerance.positionTolerance);
        fieldNumber("ori_tol", &point.tolerance.orientationTolerance);
        fieldText("approach_axis", std::string{segmentAxisToken(point.approach.axis)});
        fieldNumber("approach_dist",
                    point.approach.enabled ? &point.approach.distanceM : nullptr);
        fieldNumber("retract_dist",
                    point.retract.enabled ? &point.retract.distanceM : nullptr);
        if (point.demands.minimumJointMargin.has_value()) {
            fieldNumber("min_joint_margin", &*point.demands.minimumJointMargin);
        }
        fieldText("note", point.note);
        pointsValue.items.push_back(std::move(record));
    }
    member("points", std::move(pointsValue));

    // canonical 字节（profile=nullptr＝键名字典序——同 DOM 同字节，NFR-COR-01）。
    const io::JsonWriteOptions canonicalOptions{};
    auto canonical = io::canonicalizeJson(doc, canonicalOptions);
    if (!canonical) {
        return ExportOutcome{false, 0, "JSON canonical 化失败（io 通道）"};
    }

    // 原子写出（生产＝makeAtomicFileWriter 真实实现；测试注入 fake——
    // io AtomicFile.hpp 故障注入口径）。OverwriteAtomic＝用户已选定目标
    // 路径的显式覆盖（§7.4"失败时旧文件完好"由 commit 前目标不变保证）。
    io::IAtomicFileWriterPtr writer = atomicWriter_;
    if (!writer) {
        writer = io::makeAtomicFileWriter();
    }
    auto prepared = writer->prepare(target.filePath, io::ReplacePolicy::OverwriteAtomic);
    if (!prepared) {
        return ExportOutcome{false, 0, "原子写出准备失败（io 通道——目标目录不存在/占用）"};
    }
    auto targetSession = std::move(prepared.value);
    const auto& bytes = canonical.value;
    const auto written = targetSession.write(
        std::string_view{reinterpret_cast<const char*>(bytes.data()), bytes.size()});
    if (!written) {
        (void)writer->abort(targetSession);  // 清理暂存——目标零接触（V23 语义）
        return ExportOutcome{false, 0, "原子写出写入失败（io 通道）"};
    }
    if (const auto committed = writer->commit(targetSession); !committed) {
        (void)writer->abort(targetSession);
        return ExportOutcome{false, 0, "原子提交失败（io 通道）"};
    }
    return ExportOutcome{true, bytes.size(), {}};
}

// =====================================================================
// 构造装配
// =====================================================================

RequirementImporter::RequirementImporter()
    : RequirementImporter(nullptr)  // 委托——写出器由消费点延迟构造（生产工厂）
{
}

RequirementImporter::RequirementImporter(io::IAtomicFileWriterPtr atomicWriter)
    : dictionary_()
    , atomicWriter_(std::move(atomicWriter))
{
}

}  // namespace sdurws::ird::requirements
