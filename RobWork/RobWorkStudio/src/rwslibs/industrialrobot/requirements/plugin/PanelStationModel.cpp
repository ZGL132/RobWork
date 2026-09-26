/**
 * @file   PanelStationModel.cpp
 * @brief  工位面板呈现模型实现——检查器字段投影/五规则显隐表/编辑差值装配。
 *
 * 设计依据：units/requirements.md §9.8（面板表第 2 行）、§4.3/§5.1/§5.3
 * （字段权威）、ui FormEditCommon（WP-10-T08 公共件）；契约 WP-14-T08
 * acceptance 1/2/3。实现纪律同 modeling/PanelModel.cpp：零业务判定、
 * 确定性文本化（std::to_chars，locale 无关）、无静态可变状态。
 */

#include "PanelStationModel.hpp"

#include <charconv>
#include <stdexcept>
#include <utility>

#include <sdurws/ird/ui/UiText.hpp>  // ui::ensureNoInternalIdentity——UX-02 守卫（显示名复用唯一出口）

namespace sdurws::ird::requirements {
namespace {

// ---- 确定性数值文本化（modeling/PanelModel.cpp 同款——6 位小数裁尾零；
//      显示用途，不参与任何计算回读——回读一律走工作集权威值）--------
std::string formatDeterministic(double v)
{
    char buf[32];
    // std::to_chars：locale 无关（NFR-COR-02 确定性来源——同一 double 在
    // 任何线程/区域设置下产出同一字节串）。
    auto res = std::to_chars(buf, buf + sizeof(buf), v, std::chars_format::fixed, 6);
    std::string s(buf, res.ptr);
    // 裁尾零与孤立小数点（"1.500000"→"1.5"；"-0.000000"→"0"）。
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') { s.pop_back(); }
        if (!s.empty() && s.back() == '.') { s.pop_back(); }
    }
    if (s == "-0") { s = "0"; }
    return s;
}

// 三维向量的一行文本（"x, y, z"——单位由行字段负责）。
std::string formatVector(const rw::math::Vector3D<double>& v)
{
    return formatDeterministic(v[0]) + ", " + formatDeterministic(v[1]) + ", "
         + formatDeterministic(v[2]);
}

// SourcedValue 三维向量的呈现半区（Provided→数值；其余态→固定占位——
// "缺失≠零"，ERR-01 四态呈现纪律：不伪造数值）。
std::string formatSourcedVector(const core::SourcedValue<rw::math::Vector3D<double>>& v)
{
    if (v.state() != core::FieldState::Provided) {
        return "未提供";
    }
    return formatVector(v.value());
}

// 拼装一行并追加（行序即调用序——登记序确定性）。
void appendRow(std::vector<StationFieldRow>& rows, std::string key, std::string label,
               std::string valueText, std::string unitText,
               StationFieldEnablement enablement = StationFieldEnablement::Editable,
               std::optional<std::string> badge = std::nullopt)
{
    StationFieldRow r;
    r.fieldKey = std::move(key);
    r.label = std::move(label);
    r.valueText = std::move(valueText);
    r.unitText = std::move(unitText);
    r.enablement = enablement;
    r.sourceBadge = std::move(badge);
    rows.push_back(std::move(r));
}

// 等级/启用的事实文本（词表直投——零判定）。
std::string levelText(RequirementLevel level)
{
    return std::string(requirementLevelToken(level));  // "Must"/"Should"——§4.3 括注原文
}

std::string axisText(SegmentAxis axis)
{
    return std::string(segmentAxisToken(axis));  // "ToolZ"/"ReferenceZ"——§4.3 括注原文
}

}  // namespace

std::string sourceBadgeFor(const TaskPoint& point)
{
    // 判序＝来源互斥事实的投影序（文件头注①~⑥——首中即返，确定性）。
    if (point.importProvenance.has_value()) {
        return "导入";  // ①导入溯源在场（I-REQ-8 摘要＋行号事实）
    }
    if (point.generation.has_value()) {
        const std::string& gen = point.generation.value().generatorId;
        if (gen == "mirror") {
            return "镜像";  // ②镜像批次（generatorId 词表——TemplateArray.hpp 类注）
        }
        if (gen.rfind("template:", 0) == 0) {
            return "模板";  // ③模板批次
        }
        if (gen.rfind("array:", 0) == 0) {
            return "阵列";  // ④阵列批次（同族徽标延伸）
        }
    }
    if (point.source.methodTag.has_value()
        && point.source.methodTag.value() == "captured-tcp") {
        return "捕获";  // ⑤TCP 捕获自证标记（§5.1 来源标记行）
    }
    return "手工";  // ⑥缺省——手工输入
}

std::vector<StationFieldRow> stationFieldsFor(const TaskPoint& point, bool writable)
{
    std::vector<StationFieldRow> rows;
    rows.reserve(20);

    // ---- 条目级行（来源行带徽标——卡 §9.8"来源徽标"挂条目级）----
    appendRow(rows, "name", "名称", point.name, "", StationFieldEnablement::Editable);
    appendRow(rows, "level", "等级", levelText(point.level), "",
              StationFieldEnablement::Editable);
    appendRow(rows, "enabled", "启用", point.enabled ? "是" : "否", "",
              StationFieldEnablement::Editable);
    appendRow(rows, "source", "来源", sourceBadgeFor(point), "",
              StationFieldEnablement::ReadOnlyGrey,  // 来源是溯源事实——恒灰显（不因只读"复活"）
              sourceBadgeFor(point));
    appendRow(rows, "process-tag", "工艺", std::string(processTagToken(point.processTag)),
              "", StationFieldEnablement::Editable);

    // ---- 位姿分量约束（六分量受约束掩码逐行——"受约束/自由"事实直投）----
    const ConstrainedDof& dof = point.pose.constrainedDof;
    appendRow(rows, "dof-x", "约束·X", dof.x ? "受约束" : "自由", "");
    appendRow(rows, "dof-y", "约束·Y", dof.y ? "受约束" : "自由", "");
    appendRow(rows, "dof-z", "约束·Z", dof.z ? "受约束" : "自由", "");
    appendRow(rows, "dof-roll", "约束·Roll", dof.roll ? "受约束" : "自由", "");
    appendRow(rows, "dof-pitch", "约束·Pitch", dof.pitch ? "受约束" : "自由", "");
    appendRow(rows, "dof-yaw", "约束·Yaw", dof.yaw ? "受约束" : "自由", "");

    // ---- 受约束位置（四态——缺失"未提供"，不伪造 0）----
    appendRow(rows, "pose-position", "位置 (x, y, z)", formatSourcedVector(point.pose.position),
              "m");

    // ---- 容差两行（I-REQ-5 要求值——直投，SI 词面零换算）----
    appendRow(rows, "tolerance-position", "位置容差",
              formatDeterministic(point.tolerance.positionTolerance), "m");
    appendRow(rows, "tolerance-orientation", "姿态容差",
              formatDeterministic(point.tolerance.orientationTolerance), "rad");

    // ---- 三段（§5.1：接近/作业/撤离；作业段 enabled 恒 true——占位表达段序）----
    appendRow(rows, "segment-approach", "接近段",
              std::string(point.approach.enabled ? "启用 " : "停用 ")
                  + axisText(point.approach.axis),
              "· " + formatDeterministic(point.approach.distanceM) + " m");
    appendRow(rows, "segment-work", "作业段",
              std::string(point.work.enabled ? "启用" : "停用"), "");
    appendRow(rows, "segment-retract", "撤离段",
              std::string(point.retract.enabled ? "启用 " : "停用 ")
                  + axisText(point.retract.axis),
              "· " + formatDeterministic(point.retract.distanceM) + " m");

    // ---- 姿态规则（种类行＋按 kind 显隐的参数行——联动表单投影半区）----
    const OrientationRule& rule = point.pose.orientation;
    appendRow(rows, "orientation-kind", "姿态规则",
              std::string(orientationRuleKindToken(rule.kind)), "",
              StationFieldEnablement::Editable);
    const std::string visibleNote = "（按规则种类显隐）";
    for (const std::string& key : orientationRuleParamKeys(rule.kind)) {
        // 参数行逐键产出（显隐表驱动——键在表内即显示，不在即不产出：
        // "按 kind 显隐"的结构化实现，零 if 分支）。
        if (key == "fixed-rpy-r") {
            appendRow(rows, key, "固定 Roll" + visibleNote,
                      formatDeterministic(rule.fixedRpy[0]), "rad");
        } else if (key == "fixed-rpy-p") {
            appendRow(rows, key, "固定 Pitch", formatDeterministic(rule.fixedRpy[1]), "rad");
        } else if (key == "fixed-rpy-y") {
            appendRow(rows, key, "固定 Yaw", formatDeterministic(rule.fixedRpy[2]), "rad");
        } else if (key == "target-frame") {
            // 引用类参数（非数量字段——词表文本直投；目标对象名解析归
            // 名称端口/评估侧，浅引用只锚 ObjectId＋token，§8.1 边界）。
            std::string text = std::string(requirementRefKindToken(rule.targetFrame.kind));
            if (rule.targetFrame.objectId.has_value()) {
                text += "·已锚定";
            }
            appendRow(rows, key, "对齐目标系", text, "");
        } else if (key == "target-scene") {
            appendRow(rows, key, "场景对象",
                      rule.targetSceneObject.has_value() ? "已锚定" : "未设", "");
        } else if (key == "feature") {
            appendRow(rows, key, "几何特征",
                      rule.feature.has_value()
                          ? std::string(orientationFeatureToken(rule.feature.value()))
                          : "未设",
                      "");
        } else if (key == "invert-normal") {
            appendRow(rows, key, "法向取反", rule.invertNormal ? "是" : "否", "");
        } else if (key == "target-point-x") {
            appendRow(rows, key, "目标点 X", formatDeterministic(rule.targetPoint[0]), "m");
        } else if (key == "target-point-y") {
            appendRow(rows, key, "目标点 Y", formatDeterministic(rule.targetPoint[1]), "m");
        } else if (key == "target-point-z") {
            appendRow(rows, key, "目标点 Z", formatDeterministic(rule.targetPoint[2]), "m");
        } else if (key == "roll-min") {
            appendRow(rows, key, "滚转下限", formatDeterministic(rule.rollRange.min), "rad");
        } else if (key == "roll-max") {
            appendRow(rows, key, "滚转上限", formatDeterministic(rule.rollRange.max), "rad");
        }
    }

    // ---- 顺序键/备注（可选字段——未设＝"未设"占位）----
    appendRow(rows, "sequence-key", "顺序键",
              point.sequenceKey.has_value() ? point.sequenceKey.value() : "未设", "");
    appendRow(rows, "note", "备注", point.note, "");

    // ---- L-R12 只读门控（行半区）：writable=false → 可编辑行降级灰显；
    //      已灰显行不变（来源行等灰显不因可写性"复活"——两个独立灰显源
    //      正交，modeling L-7×§7.2 同源纪律）。
    if (!writable) {
        for (StationFieldRow& r : rows) {
            if (r.enablement == StationFieldEnablement::Editable) {
                r.enablement = StationFieldEnablement::ReadOnlyGrey;
            }
        }
    }
    return rows;
}

std::vector<std::string> orientationRuleParamKeys(OrientationRuleKind kind)
{
    // 显隐表即数据（卡 §9.8"按 kind 显隐参数"——表驱动，零分支逻辑）。
    switch (kind) {
    case OrientationRuleKind::Fixed:
        return {"fixed-rpy-r", "fixed-rpy-p", "fixed-rpy-y"};
    case OrientationRuleKind::AlignFrame:
        return {"target-frame"};
    case OrientationRuleKind::AlignGeometryNormal:
        return {"target-scene", "feature", "invert-normal"};
    case OrientationRuleKind::PointAtTarget:
        return {"target-point-x", "target-point-y", "target-point-z"};
    case OrientationRuleKind::ToolRollFree:
        return {"roll-min", "roll-max"};
    }
    return {};  // 不可达（全枚举 switch——防御性空表，编译器告警面已覆盖）
}

std::vector<ui::QuantityFieldSpec> stationQuantitySpecs()
{
    // SI 单位锚（core UnitToken::find 注册表 token——makeQuantityFieldSpec
    // 装配期核对量纲一致性，WP-10-T08 契约）。查表失败＝实现缺陷 fail-fast。
    auto unitOrThrow = [](const char* symbol) {
        auto u = core::UnitToken::find(symbol);
        if (!u.has_value()) {
            throw std::logic_error(std::string("工位面板：单位注册表缺少 ") + symbol
                                   + "（实现缺陷）");
        }
        return u.value();
    };
    const core::UnitToken m = unitOrThrow("m");
    const core::UnitToken rad = unitOrThrow("rad");

    // makeQuantityFieldSpec 装配辅助（键＝回填词表同词表——单一词表两处消费）。
    auto spec = [&](const char* key, const char* label, core::QuantityKind kind,
                    core::UnitToken si, std::optional<ui::QuantityBounds> bounds) {
        return ui::makeQuantityFieldSpec(key, label, kind, si, si, bounds);
    };
    // 正数下界（容差/距离——I-REQ-5/段距离 >0 的呈现层宿主约束；业务裁决
    // 仍归域校验链，此处只是表单输入约束——UX-05 就地错误的预过滤面）。
    ui::QuantityBounds positive{1.0e-12, 1.0e9};

    return {
        // 容差（I-REQ-5：>0 有限）。
        spec("tolerance-position", "位置容差", core::QuantityKind::Length, m, positive),
        spec("tolerance-orientation", "姿态容差", core::QuantityKind::Angle, rad, positive),
        // 三段距离（启用时 >0——表单约束同上）。
        spec("segment-approach-distance", "接近段距离", core::QuantityKind::Length, m,
             positive),
        spec("segment-retract-distance", "撤离段距离", core::QuantityKind::Length, m,
             positive),
        // Fixed 欧拉角（rad；不设界——角度合法性与语义归域校验链）。
        spec("fixed-rpy-r", "固定 Roll", core::QuantityKind::Angle, rad, std::nullopt),
        spec("fixed-rpy-p", "固定 Pitch", core::QuantityKind::Angle, rad, std::nullopt),
        spec("fixed-rpy-y", "固定 Yaw", core::QuantityKind::Angle, rad, std::nullopt),
        // PointAtTarget 目标点（m；零向量非法归域校验——表单不设界防"范围
        // 暗示"误导，呈现层契约）。
        spec("target-point-x", "目标点 X", core::QuantityKind::Length, m, std::nullopt),
        spec("target-point-y", "目标点 Y", core::QuantityKind::Length, m, std::nullopt),
        spec("target-point-z", "目标点 Z", core::QuantityKind::Length, m, std::nullopt),
        // ToolRollFree 区间（rad；有序性归域校验 rollRange.wellFormed）。
        spec("roll-min", "滚转下限", core::QuantityKind::Angle, rad, std::nullopt),
        spec("roll-max", "滚转上限", core::QuantityKind::Angle, rad, std::nullopt),
    };
}

TaskPoint applyStationEditSet(const TaskPoint& base, const ui::ParamEditSet& edits,
                              std::vector<std::string>& known)
{
    known.clear();
    known.reserve(edits.changes.size());
    TaskPoint out = base;  // 值拷贝——非表单字段（身份/名称/引用/来源）原样保留
    for (const ui::ParamChange& c : edits.changes) {
        // 词表外键＝表单模型与回填词表漂移（实现缺陷）——fail-fast 不静默
        // 丢弃（AGENTS 错误纪律：调用方/装配错误不吞）。
        if (c.key == "tolerance-position") {
            out.tolerance.positionTolerance = c.newSi;
        } else if (c.key == "tolerance-orientation") {
            out.tolerance.orientationTolerance = c.newSi;
        } else if (c.key == "segment-approach-distance") {
            out.approach.distanceM = c.newSi;
        } else if (c.key == "segment-retract-distance") {
            out.retract.distanceM = c.newSi;
        } else if (c.key == "fixed-rpy-r") {
            out.pose.orientation.fixedRpy[0] = c.newSi;
        } else if (c.key == "fixed-rpy-p") {
            out.pose.orientation.fixedRpy[1] = c.newSi;
        } else if (c.key == "fixed-rpy-y") {
            out.pose.orientation.fixedRpy[2] = c.newSi;
        } else if (c.key == "target-point-x") {
            out.pose.orientation.targetPoint[0] = c.newSi;
        } else if (c.key == "target-point-y") {
            out.pose.orientation.targetPoint[1] = c.newSi;
        } else if (c.key == "target-point-z") {
            out.pose.orientation.targetPoint[2] = c.newSi;
        } else if (c.key == "roll-min") {
            out.pose.orientation.rollRange.min = c.newSi;
        } else if (c.key == "roll-max") {
            out.pose.orientation.rollRange.max = c.newSi;
        } else {
            throw std::invalid_argument("工位面板：修改集携带词表外键 " + c.key
                                        + "（表单/回填词表漂移——实现缺陷）");
        }
        known.push_back(c.key);
    }
    return out;
}

}  // namespace sdurws::ird::requirements
