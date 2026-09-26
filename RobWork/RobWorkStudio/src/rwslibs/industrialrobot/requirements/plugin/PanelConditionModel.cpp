/**
 * @file   PanelConditionModel.cpp
 * @brief  工况面板呈现模型实现——工况表/检查器行/必验清单预览。
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
        appendRow(rows, "payload-" + idx + "-mass", "负载 " + idx + "·质量",
                  formatSourcedScalar(p.mass), "kg");
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

std::vector<MustListEntryRow> mustListPreview(const std::vector<TaskPoint>& points,
                                              const std::vector<WorkRegion>& regions,
                                              const std::vector<OperatingCondition>& conditions,
                                              const IOperatingConditionService& service)
{
    // 派生档经域函数现算（§4.8——P-EV-9 单点；contentIdentity 不入呈现：
    // 派生档指纹是 evidence 组装面，面板只呈现清单与计数）。
    const RequirementProfile profile =
        deriveRequirementProfile(points, regions, conditions, service);

    std::vector<MustListEntryRow> rows;
    rows.reserve(profile.requiredCases.size());
    for (const RequiredCaseEntry& e : profile.requiredCases) {
        MustListEntryRow row;
        row.caseId = e.caseId;      // §6.2 caseId——定位跳转锚（L-R1）
        row.label = e.label;        // §6.2 label＝工况 name
        row.enabled = e.enabled;
        row.mandatory = e.mandatory;  // 派生必验标记——域产出直投
        rows.push_back(std::move(row));
    }
    return rows;
}

}  // namespace sdurws::ird::requirements
