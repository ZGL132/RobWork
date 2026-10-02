/**
 * @file   PanelConditionModel.cpp
 * @brief  工况面板呈现模型实现——工况表（是否必验 Tag 派生）/检查器行。
 *
 * 设计依据：units/requirements.md §9.8（面板表第 4 行）、§4.5/§6.2（字段
 * 与必验冻结 schema）、§4.8（派生档）；契约 WP-14-T08 acceptance 1/3。
 * 实现纪律：必验派生唯一经域函数（P-EV-9 单点）；四态缺失占位呈现
 * （缺失≠零）；确定性文本化（locale 无关）。
 */

#include "PanelConditionModel.hpp"

#include <charconv>
#include <utility>

namespace sdurws::ird::requirements {
namespace {

// 确定性数值文本化（同 PanelStationModel——6 位小数裁尾零，locale 无关）。
std::string formatDeterministic(double v)
{
    char buf[32];
    auto res = std::to_chars(buf, buf + sizeof(buf), v, std::chars_format::fixed, 6);
    std::string s(buf, res.ptr);
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') { s.pop_back(); }
        if (!s.empty() && s.back() == '.') { s.pop_back(); }
    }
    if (s == "-0") { s = "0"; }
    return s;
}

// 三维向量一行文本。
std::string formatVector(const rw::math::Vector3D<double>& v)
{
    return formatDeterministic(v[0]) + ", " + formatDeterministic(v[1]) + ", "
         + formatDeterministic(v[2]);
}

// SourcedValue 标量的呈现半区（四态——Provided→数值；其余→固定占位，
// 不伪造数值；单位由行字段负责）。
std::string formatSourcedScalar(const core::SourcedValue<double>& v)
{
    if (v.state() != core::FieldState::Provided) {
        return "未提供";  // NotProvided/NotApplicable/Invalid 的统一占位——四态明细归诊断
    }
    return formatDeterministic(v.value());
}

// 行拼装辅助（同其他面板——登记序确定性）。
void appendRow(std::vector<StationFieldRow>& rows, std::string key, std::string label,
               std::string valueText, std::string unitText,
               StationFieldEnablement enablement = StationFieldEnablement::Editable)
{
    StationFieldRow r;
    r.fieldKey = std::move(key);
    r.label = std::move(label);
    r.valueText = std::move(valueText);
    r.unitText = std::move(unitText);
    r.enablement = enablement;
    rows.push_back(std::move(r));
}

}  // namespace

std::vector<ConditionRow> conditionRows(const std::vector<OperatingCondition>& conditions)
{
    std::vector<ConditionRow> rows;
    rows.reserve(conditions.size());
    for (const OperatingCondition& c : conditions) {
        ConditionRow row;
        row.objectId = c.objectId;
        row.name = c.name;
        row.level = std::string(requirementLevelToken(c.level));
        row.enabled = c.enabled;
        // 目标节拍（s；可选——未设不伪造）。
        row.cycleText = c.targetCycleTimeS.has_value()
                          ? formatDeterministic(c.targetCycleTimeS.value()) + " s"
                          : "未设";
        // 适用范围摘要（D-REQ-5：工况侧声明——三值词表直投）。
        switch (c.appliesTo.scope) {
        case AppliesToScope::AllStations:
            row.appliesToText = "全部工位";
            break;
        case AppliesToScope::Stations:
            // 显式清单行数（悬空判定归就绪层 R4——本行只报计数事实）。
            row.appliesToText = std::to_string(c.appliesTo.stations.size()) + " 个工位";
            break;
        case AppliesToScope::None:
            row.appliesToText = "不适用";  // §6.1：显式"当前不适用"标记
            break;
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

std::vector<StationFieldRow> conditionFieldsFor(const OperatingCondition& condition,
                                                const IOperatingConditionService& service,
                                                bool writable)
{
    std::vector<StationFieldRow> rows;
    rows.reserve(16);

    // ---- 条目级行（必验行＝派生事实直投——域解析单条目查询经
    //      resolveRequiredCases 全集合解析取本条目投影；插件零复判 I-REQ-9）。
    appendRow(rows, "name", "名称", condition.name, "");
    appendRow(rows, "level", "等级", std::string(requirementLevelToken(condition.level)), "");
    appendRow(rows, "enabled", "启用", condition.enabled ? "是" : "否", "");
    {
        // 单条目的必验派生事实：以单元素集合调用域函数（P-EV-9 单点——
        // mandatory 字段即 level 事实的域面投影，呈现层不本地复判）。
        const std::vector<OperatingCondition> single{condition};
        const RequiredCaseResolution resolution = service.resolveRequiredCases(single);
        std::string mustText = "否";
        for (const RequiredCaseEntry& e : resolution.entries) {
            if (e.caseId == condition.objectId && e.enabled && e.mandatory) {
                mustText = "是";  // enabled∧Must——§6.2 冻结解析结果直投
                break;
            }
        }
        appendRow(rows, "mandatory", "必验", mustText, "",
                  StationFieldEnablement::ReadOnlyGrey);  // 派生事实——不可直接编辑
    }

    // ---- 负载逐条（§4.5 payloads——物性四态；质量 kg/质心 m/惯量 kg·m²）。
    for (std::size_t i = 0; i < condition.payloads.size(); ++i) {
        const ConditionPayload& p = condition.payloads[i];
        const std::string idx = std::to_string(i + 1);  // 1 起呈现序
        appendRow(rows, "payload-" + idx + "-tool", "负载 " + idx + "·工具",
                  std::string(requirementRefKindToken(p.toolRef.kind)), "");
        // B2 质量行：仅 Provided 态可编（未提供态的四态语义〔设初值〕归
        // 负载编辑后续批次——行收窄为只读呈现，不虚构可编）。
        appendRow(rows, "payload-" + idx + "-mass", "负载 " + idx + "·质量",
                  formatSourcedScalar(p.mass), "kg",
                  p.mass.state() == core::FieldState::Provided
                      ? StationFieldEnablement::Editable
                      : StationFieldEnablement::ReadOnlyGrey);
        appendRow(rows, "payload-" + idx + "-com", "负载 " + idx + "·质心",
                  p.com.state() == core::FieldState::Provided ? formatVector(p.com.value())
                                                              : "未提供",
                  "m");
        appendRow(rows, "payload-" + idx + "-inertia", "负载 " + idx + "·惯量",
                  formatSourcedScalar(p.inertia), "kg·m²");
    }
    if (condition.payloads.empty()) {
        appendRow(rows, "payload-none", "负载", "无", "",
                  StationFieldEnablement::ReadOnlyGrey);
    }

    // ---- 事件逐条（§4.5 events——类型＋绑定工位锚＋时长 s）。
    for (std::size_t i = 0; i < condition.events.size(); ++i) {
        const ConditionEvent& e = condition.events[i];
        const std::string idx = std::to_string(i + 1);
        appendRow(rows, "event-" + idx + "-type", "事件 " + idx + "·类型",
                  std::string(conditionEventTypeToken(e.type)), "");
        appendRow(rows, "event-" + idx + "-station", "事件 " + idx + "·工位",
                  e.stationRef.isValid() ? "已锚定" : "未设", "");
        appendRow(rows, "event-" + idx + "-duration", "事件 " + idx + "·时长",
                  e.durationS.has_value() ? formatDeterministic(e.durationS.value()) : "未设",
                  "s");
    }
    if (condition.events.empty()) {
        appendRow(rows, "event-none", "事件", "无", "",
                  StationFieldEnablement::ReadOnlyGrey);
    }

    // ---- 节拍/要求值/适用范围/引用面。
    appendRow(rows, "cycle-time", "目标节拍",
              condition.targetCycleTimeS.has_value()
                  ? formatDeterministic(condition.targetCycleTimeS.value())
                  : "未设",
              "s");
    appendRow(rows, "demand-collision", "无碰撞要求",
              condition.demands.collisionFreeRequired ? "要求" : "不要求", "");
    appendRow(rows, "demand-margin", "最小关节裕量",
              condition.demands.minimumJointMargin.has_value()
                  ? formatDeterministic(condition.demands.minimumJointMargin.value())
                  : "未设",
              "");
    switch (condition.appliesTo.scope) {
    case AppliesToScope::AllStations:
        appendRow(rows, "applies-to", "适用范围", "全部工位", "");
        break;
    case AppliesToScope::Stations:
        appendRow(rows, "applies-to", "适用范围",
                  std::to_string(condition.appliesTo.stations.size()) + " 个工位", "");
        break;
    case AppliesToScope::None:
        appendRow(rows, "applies-to", "适用范围", "不适用", "");
        break;
    }
    appendRow(rows, "env-refs", "环境引用",
              std::to_string(condition.environmentRefs.size()) + " 项", "");
    appendRow(rows, "note", "备注", condition.note, "");

    // ---- L-R12 只读门控（行半区——同前两面板的降级规则）。
    if (!writable) {
        for (StationFieldRow& r : rows) {
            if (r.enablement == StationFieldEnablement::Editable) {
                r.enablement = StationFieldEnablement::ReadOnlyGrey;
            }
        }
    }
    return rows;
}

// =====================================================================
// B2 字段编辑提交协议（UI-T31——specs 词表＋回填；工位/区域同构）
// =====================================================================

std::vector<ui::QuantityFieldSpec> conditionQuantitySpecs(
    const OperatingCondition& condition)
{
    auto unitOrThrow = [](const char* symbol) {
        auto u = core::UnitToken::find(symbol);
        if (!u.has_value()) {
            throw std::logic_error(std::string("工况面板：单位注册表缺少 ") + symbol
                                   + "（实现缺陷）");
        }
        return u.value();
    };
    const core::UnitToken s = unitOrThrow("s");
    const core::UnitToken kg = unitOrThrow("kg");

    // 正数下界（节拍/质量＞0 的呈现层预过滤——业务裁决归域链）。
    const ui::QuantityBounds positive{1.0e-12, 1.0e9};

    std::vector<ui::QuantityFieldSpec> specs;
    specs.reserve(1 + condition.payloads.size());
    specs.push_back(ui::makeQuantityFieldSpec(
        "cycle-time", "目标节拍", core::QuantityKind::Time, s, s, positive));
    for (std::size_t i = 0; i < condition.payloads.size(); ++i) {
        specs.push_back(ui::makeQuantityFieldSpec(
            "payload-" + std::to_string(i + 1) + "-mass",
            "负载 " + std::to_string(i + 1) + "·质量",
            core::QuantityKind::Mass, kg, kg, positive));
    }
    return specs;
}

OperatingCondition applyConditionEditSet(const OperatingCondition& base,
                                         const ui::ParamEditSet& edits,
                                         std::vector<std::string>& known)
{
    known.clear();
    known.reserve(edits.changes.size());
    OperatingCondition out = base;  // 值拷贝——非表单字段原样保留
    for (const ui::ParamChange& c : edits.changes) {
        if (c.key == "cycle-time") {
            // 节拍为 optional：任何一次提交即设值（未设→设值合法路径；
            // 清空走条目删除/域命令——行编辑不承载"清除"语义）。
            out.targetCycleTimeS = c.newSi;
        } else if (c.key.rfind("payload-", 0) == 0
                   && c.key.size() > 8U && c.key.substr(c.key.size() - 5U) == "-mass") {
            // 负载序解析（payload-<i>-mass——i 为 1 起呈现序）。
            const std::size_t idx = static_cast<std::size_t>(
                std::stoull(c.key.substr(8U, c.key.size() - 8U - 5U)));
            if (idx < 1U || idx > out.payloads.size()) {
                throw std::logic_error("工况面板：负载序越界 " + c.key);
            }
            out.payloads[idx - 1U].mass = core::SourcedValue<double>::provided(
                c.newSi, out.payloads[idx - 1U].mass.state()
                                 == core::FieldState::Provided
                             ? out.payloads[idx - 1U].mass.provenance()
                             : core::ValueProvenance::make(
                                   core::ProvenanceKind::UserProvided));
        } else {
            throw std::logic_error("工况面板：编辑行携带词表外键 " + c.key);
        }
        known.push_back(c.key);
    }
    return out;
}


OperatingCondition applyConditionEnumEdit(const OperatingCondition& base,
                                          const std::string& key,
                                          const std::string& valueText,
                                          std::vector<std::string>& known)
{
    known.clear();
    // 枚举词表回填（UI-T37 返工②——等级/启用；词表外 fail-fast）。
    OperatingCondition out = base;
    if (key == "level") {
        const auto level = tryRequirementLevel(valueText);
        if (!level.has_value()) {
            throw std::invalid_argument("工况面板：等级词表外文本 " + valueText);
        }
        out.level = level.value();
    } else if (key == "enabled") {
        out.enabled = (valueText == "是");
    } else {
        throw std::invalid_argument("工况面板：枚举回填携带词表外键 " + key
                                    + "（实现缺陷）");
    }
    known.push_back(key);
    return out;
}

}  // namespace sdurws::ird::requirements
