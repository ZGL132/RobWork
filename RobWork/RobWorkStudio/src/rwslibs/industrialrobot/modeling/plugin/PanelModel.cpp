/**
 * @file   PanelModel.cpp
 * @brief  建模面板呈现模型实现——五区信息架构的纯函数投影（卡 §9.7.1）。
 *
 * 设计依据：units/modeling.md §9.7.1/§9.7.4/§7.2/§4.4/§4.5/§4.7、
 * ui.md §6.6（UX-02）、契约 WP-13-T15 acceptance 2/5。
 * 实现纪律（本文件通则）：
 *   - 零计算逻辑：一切业务判定（合法性/权威互斥/就绪分级）唯一消费
 *     计算库既有产出（工作集值/就绪报告）；本文件只做"值→视图行"的
 *     形状搬运与确定性文本化（插件零计算逻辑——DTB 禁止项）。
 *   - 无缓存：所有函数为自由函数、无静态可变状态（ACC5"面板不缓存
 *     模型权威数据"的结构性实现）。
 *   - 确定性文本化：数值转串统一经本文件尾部的 formatDeterministic
 *     （std::to_chars，locale 无关，固定 6 位小数裁尾零——与 core
 *     displayValueIn 的换算面分工：这里只做"已有 SI 数值→显示串"，
 *     单位换算仍唯一归 core，SA-12）。
 */

#include "PanelModel.hpp"

#include <charconv>
#include <type_traits>
#include <utility>

#include <sdurws/ird/modeling/Parts.hpp>  // ToolDefinition/SceneObject/DrivetrainDesign 值模型（部件投影）
#include <sdurws/ird/ui/UiText.hpp>       // ui::ensureNoInternalIdentity——UX-02 哈希形态守卫（唯一出口复用，不私写第二实现）

namespace sdurws::ird::modeling {
namespace {

// ---- 确定性数值文本化 -------------------------------------------------
// 6 位小数裁尾零：面板呈现精度约定（显示用途；不参与任何计算回读——
// 回读一律走工作集权威值，防止"显示值被再编辑"造成精度丢失）。
std::string formatDeterministic(double v)
{
    char buf[32];
    // std::to_chars：locale 无关（NFR-COR-02 确定性来源——同一 double
    // 在任何线程/区域设置下产出同一字节串）。
    auto res = std::to_chars(buf, buf + sizeof(buf), v, std::chars_format::fixed, 6);
    std::string s(buf, res.ptr);
    // 裁尾零与孤立小数点（"1.500000"→"1.5"；"-0.000000"→"0"——负零呈现归一）。
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') { s.pop_back(); }
        if (!s.empty() && s.back() == '.') { s.pop_back(); }
    }
    if (s == "-0") { s = "0"; }
    return s;
}

// 三维向量的一行文本（"x, y, z"——m 或无量纲，单位由行字段负责）。
std::string formatVector(const rw::math::Vector3D<double>& v)
{
    return formatDeterministic(v[0]) + ", " + formatDeterministic(v[1]) + ", "
         + formatDeterministic(v[2]);
}

// ---- SourcedValue 的呈现半区 -----------------------------------------
// Provided→数值文本；其余态→固定占位（"未提供"/"不适用"——不伪造数值，
// ERR-01 四态呈现纪律）；Provided 态同时直投来源徽标（core ProvenanceKind
// 五值——UX-02 文案归 widget 层）。显式函数模板（不用 auto lambda——
// 返回类型推定经 pair 显式书写，MSVC 结构化绑定兼容）。
template <typename T>
std::pair<std::string, std::optional<core::ProvenanceKind>> sourcedRow(
    const core::SourcedValue<T>& sv)
{
    std::string text;
    if (sv.state() == core::FieldState::Provided) {
        if constexpr (std::is_same_v<T, double>) {
            text = formatDeterministic(sv.value());
        } else {
            // 复合值类型（向量/张量/位姿/限位对）的占位——精确数值由
            // 调用方按字段展开（见 propertyFieldsFor 各行）。
            text = "[值已提供]";
        }
    } else if (sv.state() == core::FieldState::NotApplicable) {
        text = "不适用";
    } else {
        text = "未提供";
    }
    std::optional<core::ProvenanceKind> prov;
    if (sv.state() == core::FieldState::Provided) {
        prov = sv.provenance().kind;
    }
    return {text, prov};
}

// 组节点工厂（分组无对象身份——objectId 为空，锚语义只属于对象叶子）。
StructureNode makeGroup(StructureNodeKind kind, std::string label)
{
    StructureNode n;
    n.kind = kind;
    n.displayLabel = std::move(label);
    return n;
}

}  // namespace

// =====================================================================
// 建模结构树投影
// =====================================================================

std::vector<StructureNode> buildStructureTree(const ModelingWorkingSet& ws)
{
    std::vector<StructureNode> nodes;

    // 节点①模型根：锚＝根对象身份（若已有 project 身份；模板初始草稿为
    // 临时句柄仍可作会话内锚——ModelingWorkingSet 注"消费方不得把临时
    // 句柄当持久身份外泄"，此处只在会话内使用，不落盘不外传）。
    StructureNode root;
    root.kind = StructureNodeKind::ModelRoot;
    root.objectId = ws.rootObjectId;
    root.localName = ws.design.displayName;
    root.displayLabel = "模型 " + root.localName;
    ui::ensureNoInternalIdentity(root.displayLabel);  // UX-02 守卫（哈希形态即抛——非法名不进面板）
    nodes.push_back(std::move(root));

    // 节点②基座安装：MDL-22 的呈现节点（参数编辑走 T11 编辑流——本树只定位）。
    StructureNode base;
    base.kind = StructureNodeKind::BaseInstall;
    base.displayLabel = "基座安装";
    ui::ensureNoInternalIdentity(base.displayLabel);
    nodes.push_back(std::move(base));

    // 节点③④关节链与连杆链：串联序（数组下标即链序——RobotDesign 值模型纪律）。
    for (std::size_t i = 0; i < ws.design.joints.size(); ++i) {
        const JointEntry& j = ws.design.joints[i];
        StructureNode n;
        n.kind = StructureNodeKind::Joint;
        n.objectId = j.objectId;
        n.localName = j.localName;
        n.displayLabel = "关节 " + formatDeterministic(static_cast<double>(i + 1)) + " " + j.localName;
        n.chainIndex = i;
        ui::ensureNoInternalIdentity(n.displayLabel);  // localName 非法字符集在构造边界已拒——此处哈希形态兜底守卫
        nodes.push_back(std::move(n));
    }
    for (std::size_t i = 0; i < ws.design.links.size(); ++i) {
        const LinkEntry& l = ws.design.links[i];
        StructureNode n;
        n.kind = StructureNodeKind::Link;
        n.objectId = l.objectId;
        n.localName = l.localName;
        n.displayLabel = "连杆 " + formatDeterministic(static_cast<double>(i + 1)) + " " + l.localName;
        n.chainIndex = i;
        ui::ensureNoInternalIdentity(n.displayLabel);
        nodes.push_back(std::move(n));
    }

    // 节点⑤工具分组＋组内叶子（leaf 行按 localName 字典序——组内容随引用
    // 表变化时行序不漂移，NFR-COR-02 界面延伸；复本排序不改变工作集）。
    nodes.push_back(makeGroup(StructureNodeKind::ToolsGroup, "工具"));
    std::vector<const ToolDefinition*> tools;
    for (const ToolDefinition& t : ws.toolObjects) { tools.push_back(&t); }
    std::stable_sort(tools.begin(), tools.end(),
                     [](const ToolDefinition* a, const ToolDefinition* b) {
                         return a->localName < b->localName;
                     });
    for (const ToolDefinition* t : tools) {
        StructureNode n;
        n.kind = StructureNodeKind::ToolsGroup;  // 工具叶子复用组类别＋对象锚——widget 层按有无 objectId 区分缩进
        n.objectId = t->objectId;
        n.localName = t->localName;
        n.displayLabel = "工具 " + t->localName;
        ui::ensureNoInternalIdentity(n.displayLabel);
        nodes.push_back(std::move(n));
    }

    // 节点⑥场景分组＋组内叶子（排序纪律同工具组）。
    nodes.push_back(makeGroup(StructureNodeKind::SceneGroup, "场景"));
    std::vector<const SceneObject*> scenes;
    for (const SceneObject& s : ws.sceneObjects) { scenes.push_back(&s); }
    std::stable_sort(scenes.begin(), scenes.end(),
                     [](const SceneObject* a, const SceneObject* b) {
                         return a->localName < b->localName;
                     });
    for (const SceneObject* s : scenes) {
        StructureNode n;
        n.kind = StructureNodeKind::SceneGroup;
        n.objectId = s->objectId;
        n.localName = s->localName;
        n.displayLabel = "场景 " + s->localName;
        ui::ensureNoInternalIdentity(n.displayLabel);
        nodes.push_back(std::move(n));
    }

    // 节点⑦位姿集分组（至多一份——§4.6；位姿集无 localName 字段，呈现用键计数）。
    if (ws.poseSetObject.has_value()) {
        StructureNode n;
        n.kind = StructureNodeKind::PoseSetGroup;
        n.objectId = ws.poseSetObject->objectId;
        n.displayLabel = "命名位姿集";
        ui::ensureNoInternalIdentity(n.displayLabel);
        nodes.push_back(std::move(n));
    }

    // 节点⑧传动分组（至多一份——§4.7；传动无 localName 字段——呈现用
    // 比率行计数，锚仍为对象身份）。
    if (ws.drivetrainObject.has_value()) {
        StructureNode n;
        n.kind = StructureNodeKind::DrivetrainGroup;
        n.objectId = ws.drivetrainObject->objectId;
        n.displayLabel = "传动（" + formatDeterministic(
            static_cast<double>(ws.drivetrainObject->ratioPerJoint.size())) + " 关节)";
        ui::ensureNoInternalIdentity(n.displayLabel);
        nodes.push_back(std::move(n));
    }

    return nodes;
}

// =====================================================================
// 属性编辑区投影
// =====================================================================

std::optional<SelectedTarget> resolveSelection(const ModelingWorkingSet& ws,
                                               const core::ObjectId& selected)
{
    // 根对象：仅当工作集已携带根身份（模板初始草稿 rootObjectId=nullopt——
    // 闭包内身份未回填，根不可被外部锚定）。
    if (ws.rootObjectId.has_value() && *ws.rootObjectId == selected) {
        return SelectedTarget{SelectedTarget::Kind::ModelRoot, 0};
    }
    // 关节/连杆：链序下标。
    for (std::size_t i = 0; i < ws.design.joints.size(); ++i) {
        if (ws.design.joints[i].objectId == selected) {
            return SelectedTarget{SelectedTarget::Kind::Joint, i};
        }
    }
    for (std::size_t i = 0; i < ws.design.links.size(); ++i) {
        if (ws.design.links[i].objectId == selected) {
            return SelectedTarget{SelectedTarget::Kind::Link, i};
        }
    }
    // 部件对象：工具/场景按对象表下标；位姿集/传动至多一份。
    for (std::size_t i = 0; i < ws.toolObjects.size(); ++i) {
        if (ws.toolObjects[i].objectId == selected) {
            return SelectedTarget{SelectedTarget::Kind::Tool, i};
        }
    }
    for (std::size_t i = 0; i < ws.sceneObjects.size(); ++i) {
        if (ws.sceneObjects[i].objectId == selected) {
            return SelectedTarget{SelectedTarget::Kind::Scene, i};
        }
    }
    if (ws.poseSetObject.has_value() && ws.poseSetObject->objectId == selected) {
        return SelectedTarget{SelectedTarget::Kind::PoseSet, 0};
    }
    if (ws.drivetrainObject.has_value() && ws.drivetrainObject->objectId == selected) {
        return SelectedTarget{SelectedTarget::Kind::Drivetrain, 0};
    }
    // 未命中：引用了闭包外身份——属性区空态（不伪造行）。
    return std::nullopt;
}

std::vector<PropertyFieldRow> propertyFieldsFor(const ModelingWorkingSet& ws,
                                                const SelectedTarget& target)
{
    std::vector<PropertyFieldRow> rows;
    // 行构造辅助（字段键词表固定——测试判别面；行序＝词表序）。
    auto add = [&rows](std::string key, std::string value, std::string unit,
                       FieldEnablement en, std::optional<core::ProvenanceKind> prov) {
        PropertyFieldRow r;
        r.fieldKey = std::move(key);
        r.valueText = std::move(value);
        r.unitText = std::move(unit);
        r.enablement = en;
        r.provenance = prov;
        rows.push_back(std::move(r));
    };

    switch (target.kind) {
    case SelectedTarget::Kind::Joint: {
        // 关节（§9.7.1：类型/轴/零位/限位＋工作范围；DH 权威下轴/原点灰显
        // 只读——§7.2 派生只读的呈现半区。速度/加速度限值属传动对象
        // （MDL-16——§4.7），不在此投影——"只显示相关属性"的类别划界）。
        const JointEntry& j = ws.design.joints.at(target.index);
        const bool dhAuthority = ws.design.authority == AuthorityMode::StandardDH;
        const auto dhGrey = dhAuthority ? FieldEnablement::ReadOnlyGrey : FieldEnablement::Editable;

        // 类型：两模式均权威（§7.2 表——不在 C-1/C-2 管辖），恒可编辑。
        add("type", std::string(jointTypeToken(j.type)), "", FieldEnablement::Editable, std::nullopt);
        // 轴向：StandardDH＝派生只读（灰显；DerivedReadOnly 来源徽标直投）。
        {
            auto [text, prov] = sourcedRow(j.axis);
            add("axis", text, "", dhGrey, prov);
        }
        // 原点：同轴的权威/派生语义（§7.2）。
        {
            auto [text, prov] = sourcedRow(j.origin);
            add("origin", text, "m, rad", dhGrey, prov);
        }
        // 零位偏置：两模式均权威（rad/m 随类型——单位标注呈现侧拼接）。
        {
            std::string unit = j.type == JointType::Prismatic ? "m" : "rad";
            add("zero-offset", formatDeterministic(j.zeroOffset), unit, FieldEnablement::Editable,
                std::nullopt);
        }
        // 限位：Continuous＝NotApplicable（I-MDL-4）——sourcedRow 的"不适用"
        // 占位直出；数值行格式 "qmin, qmax"。
        {
            auto [text, prov] = sourcedRow(j.bounds);
            if (j.bounds.state() == core::FieldState::Provided) {
                text = formatDeterministic(j.bounds.value().first) + ", "
                     + formatDeterministic(j.bounds.value().second);
            }
            std::string unit = j.type == JointType::Prismatic ? "m" : "rad";
            add("bounds", text, unit, FieldEnablement::Editable, prov);
        }
        // 工程工作范围（仅 Continuous 合法——§7.3；非法组合进不了工作集，
        // 投影只忠实呈现）。
        {
            auto [text, prov] = sourcedRow(j.workingRange);
            if (j.workingRange.state() == core::FieldState::Provided) {
                text = formatDeterministic(j.workingRange.value().first) + ", "
                     + formatDeterministic(j.workingRange.value().second);
            }
            add("working-range", text, "rad", FieldEnablement::Editable, prov);
        }
        break;
    }
    case SelectedTarget::Kind::Link: {
        // 连杆（§9.7.1：物性＋来源徽标＋几何引用）。
        const LinkEntry& l = ws.design.links.at(target.index);
        {
            auto [text, prov] = sourcedRow(l.body.mass);
            add("mass", text, "kg", FieldEnablement::Editable, prov);
        }
        {
            auto [text, prov] = sourcedRow(l.body.centerOfMass);
            if (l.body.centerOfMass.state() == core::FieldState::Provided) {
                text = formatVector(l.body.centerOfMass.value());
            }
            add("center-of-mass", text, "m", FieldEnablement::Editable, prov);
        }
        {
            auto [text, prov] = sourcedRow(l.body.inertia);
            add("inertia", text, "kg*m^2", FieldEnablement::Editable, prov);
        }
        // 几何引用行（visual/collision 有无——引用资源键，不复制内容）。
        add("visual-geometry", l.visual.has_value() ? l.visual->resourceRefId : "未设置", "",
            FieldEnablement::Editable, std::nullopt);
        add("collision-geometry", l.collision.has_value() ? l.collision->resourceRefId : "未设置",
            "", FieldEnablement::Editable, std::nullopt);
        break;
    }
    case SelectedTarget::Kind::Tool: {
        // 工具（§4.4：安装接口/TCP 列表——逐 TCP 行）。
        const ToolDefinition& t = ws.toolObjects.at(target.index);
        add("mount-interface", "[位姿已提供]", "m, rad", FieldEnablement::Editable, std::nullopt);
        for (const TcpEntry& tcp : t.tcpList) {
            // TCP 行键＝"tcp:<key>"（工具内唯一键——defaultTcp 引用目标）。
            add("tcp:" + tcp.key, tcp.displayName, "m, rad", FieldEnablement::Editable,
                std::nullopt);
        }
        break;
    }
    case SelectedTarget::Kind::Scene: {
        // 场景（§4.5：世界位姿/角色——世界系固连，M-11 呈现注记随 widget 文案）。
        const SceneObject& s = ws.sceneObjects.at(target.index);
        add("world-pose", "[位姿已提供]", "m, rad", FieldEnablement::Editable, std::nullopt);
        add("role", std::string(sceneObjectRoleToken(s.role)), "", FieldEnablement::Editable,
            std::nullopt);
        break;
    }
    case SelectedTarget::Kind::PoseSet: {
        // 位姿集（§4.6：键集合呈现；编辑走 apply-named-poses 命令——本区只读投影）。
        const PoseSet& p = *ws.poseSetObject;
        for (const PoseSetEntry& e : p.entries) {
            add("pose:" + e.key, std::to_string(e.jointConfiguration.size()) + " 轴", "rad, m",
                FieldEnablement::ReadOnlyGrey, std::nullopt);
        }
        break;
    }
    case SelectedTarget::Kind::Drivetrain: {
        // 传动（§4.7：比率/摩擦/力矩——MDL-16 限值层呈现面；值面直投，
        // 判定零参与；逐关节条目与关节序一一对应——Parts.hpp 纪律）。
        const DrivetrainDesign& d = *ws.drivetrainObject;
        add("ratio-per-joint", std::to_string(d.ratioPerJoint.size()) + " 关节", "",
            FieldEnablement::Editable, std::nullopt);
        add("friction-per-joint", std::to_string(d.frictionPerJoint.size()) + " 关节", "",
            FieldEnablement::Editable, std::nullopt);
        add("torque-limits-per-joint", std::to_string(d.torqueLimitsPerJoint.size()) + " 关节",
            "N*m", FieldEnablement::Editable, std::nullopt);
        break;
    }
    case SelectedTarget::Kind::ModelRoot: {
        // 根（显示名/权威模式——权威切换走 L-9 命令流，本区只投影）。
        add("display-name", ws.design.displayName, "", FieldEnablement::Editable, std::nullopt);
        add("authority", std::string(authorityModeToken(ws.design.authority)), "",
            FieldEnablement::ReadOnlyGrey, std::nullopt);
        break;
    }
    case SelectedTarget::Kind::BaseInstall: {
        // 基座安装（§4.3 basePlacement：预设/位置——编辑走 T11 编辑流；
        // 预设中文标签为呈现层固定映射，机器判别仍以枚举为权威）。
        const char* presetLabel = "地面";
        switch (ws.design.basePlacement.preset) {
        case runtime::InstallationPresetToken::Ground: presetLabel = "地面"; break;
        case runtime::InstallationPresetToken::Inverted: presetLabel = "倒挂"; break;
        case runtime::InstallationPresetToken::Wall: presetLabel = "壁装"; break;
        case runtime::InstallationPresetToken::Custom: presetLabel = "自定义"; break;
        }
        add("preset", presetLabel, "", FieldEnablement::Editable, std::nullopt);
        {
            auto [text, prov] = sourcedRow(ws.design.basePlacement.basePosition);
            if (ws.design.basePlacement.basePosition.state() == core::FieldState::Provided) {
                text = formatVector(ws.design.basePlacement.basePosition.value());
            }
            add("base-position", text, "m", FieldEnablement::Editable, prov);
        }
        break;
    }
    }
    return rows;
}

// =====================================================================
// 就绪与诊断条投影
// =====================================================================

ReadinessBarProjection projectReadinessBar(const ModelReadinessReport& report)
{
    ReadinessBarProjection proj;

    // 逐层结果行：LayerDetail 保序直投（下标 0..11 ↔ L0..L11——Readiness.hpp
    // 静态断言钉住的同源数组；层轴单一事实来源＝报告，无第二映射）。
    for (std::size_t layer = 0; layer < proj.layerResults.size(); ++layer) {
        proj.layerResults[layer].layer = static_cast<ReadinessLayer>(layer);
        proj.layerResults[layer].passed = report.layers[layer].passed;
        proj.layerResults[layer].note = report.layers[layer].note;
    }

    // 三组计数（note 组随附——呈现级结论在条上同显）。
    proj.counts.blockers = report.blockers.size();
    proj.counts.warnings = report.warnings.size();
    proj.counts.confirmables = report.confirmables.size();
    proj.counts.notes = report.notes.size();

    // 逐项行（组序 blocking→warning→confirmable→note；组内报告稳定序保序
    // 展开——不二次排序，单一排序权威纪律）。摘要＝报告原文三段拼接
    // （定位/原因/建议），呈现层零业务文案加工。
    for (const core::DiagnosticRecord& b : report.blockers) {
        ReadinessItemRow r;
        r.severity = "blocking";
        r.summary = b.context + "：" + b.cause + "；" + b.recommendedAction;
        r.jumpTarget = b.subject;  // 逐项定位跳转（subject→树 ObjectId 同键）
        proj.items.push_back(std::move(r));
    }
    for (const core::DiagnosticRecord& w : report.warnings) {
        ReadinessItemRow r;
        r.severity = "warning";
        r.summary = w.context + "：" + w.cause + "；" + w.recommendedAction;
        r.jumpTarget = w.subject;
        proj.items.push_back(std::move(r));
    }
    for (const core::ConfirmableFinding& c : report.confirmables) {
        ReadinessItemRow r;
        r.severity = "confirmable";
        r.summary = c.record.context + "：" + c.record.cause + "；" + c.record.recommendedAction;
        r.jumpTarget = c.record.subject;
        proj.items.push_back(std::move(r));
    }
    for (const ReadinessNote& n : report.notes) {
        ReadinessItemRow r;
        r.severity = "note";
        r.summary = n.summary;
        r.jumpTarget = std::nullopt;  // 呈现级结论无对象主体——不伪造定位
        proj.items.push_back(std::move(r));
    }
    return proj;
}

// =====================================================================
// 预览页纪律
// =====================================================================

std::vector<std::string> buildPreviewPage(const AppliedRevisionView& applied)
{
    std::vector<std::string> lines;
    // 空修订＝预览页空态（无已应用修订——不伪造内容；D-MDL-10 的空态半区；
    // 空判定＝与默认构造 RevisionId（全零保留值）逐字节相等）。
    if (applied.revision == core::RevisionId{} || applied.summaryText.empty()) { return lines; }
    // 摘要按行拆分（预览页逐行渲染——呈现素材即已应用快照摘要，零加工）。
    std::size_t begin = 0;
    while (begin <= applied.summaryText.size()) {
        std::size_t end = applied.summaryText.find('\n', begin);
        if (end == std::string::npos) {
            lines.push_back(applied.summaryText.substr(begin));
            break;
        }
        lines.push_back(applied.summaryText.substr(begin, end - begin));
        begin = end + 1;
    }
    return lines;
}

}  // namespace sdurws::ird::modeling
