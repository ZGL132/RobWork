/**
 * @file   Readiness.cpp
 * @brief  IModelReadinessChecker 实现——§8.2 分层检查表 L0→L11 的编排：
 *         逐层评估、短路优先级（高层依赖低层通过）、结果三组稳定排序。
 *         判定逻辑唯一委托 AssertionSuite（物理合法性）与 RobotDesign/
 *         Parts 不变量层（构造合法性）——本文件只做层归属与汇总，零第二
 *         判定式。
 *
 * 设计依据：units/modeling.md §8.2（分层检查表/级别语义/就绪状态图/结果
 * 流向）、§9.4.4（check 纯函数契约＋AssertionSuite 共用——D-MDL-11/
 * NFR-MNT-04）；任务契约 WP-13-T08 acceptance 1～3。
 *
 * 确定性（NFR-COR-02）：层评估序固定；结果组按（层号→对象 id 字典序→
 * 码字典序）稳定排序；同输入重复 check 输出逐字段相等。
 */

#include <sdurws/ird/modeling/Readiness.hpp>

#include <algorithm>
#include <array>
#include <functional>
#include <utility>

#include <sdurws/ird/modeling/Parts.hpp>      // checkInvariants(ToolDefinition/DrivetrainDesign)
#include <sdurws/ird/modeling/RobotDesign.hpp>  // checkInvariants(RobotDesign)/InvariantId

namespace sdurws::ird::modeling {

namespace {

/// 层数（L0..L11）——LayerDetail 数组长度的编译期钉子（防枚举/数组漂移）。
constexpr std::size_t kLayerCount = 12;
static_assert(static_cast<std::size_t>(ReadinessLayer::L11CompileRequestable) + 1u
                  == kLayerCount,
              "ReadinessLayer 枚举与 LayerDetail 数组长度不一致");

/// 层号→数组下标。
std::size_t layerIndex(ReadinessLayer layer)
{
    return static_cast<std::size_t>(layer);
}

/// 就绪校验器侧的行程相关性谓词（与 prepare 侧同一事实面——候选含有限
/// 限位旋转关节；continuous 行程豁免、无界旋转不消费阈值。谓词重复系
/// 3 行存在性检查，判定式零重复——行程比较唯一在 AssertionSuite/评估器）。
bool hasTravelRelevantJoints(const RobotDesign& design)
{
    for (const JointEntry& j : design.joints) {
        if (j.type == JointType::Revolute && j.bounds.tryValue().has_value()) {
            return true;
        }
    }
    return false;
}

}  // namespace

// ---- token 转发表（switch 全枚举无 default——新增值漏登记编译器告警）----

std::string_view readinessLayerToken(ReadinessLayer layer) noexcept
{
    switch (layer) {
    case ReadinessLayer::L0Structure: return "L0";
    case ReadinessLayer::L1References: return "L1";
    case ReadinessLayer::L2UnitsFinite: return "L2";
    case ReadinessLayer::L3JointAxis: return "L3";
    case ReadinessLayer::L4LimitOrder: return "L4";
    case ReadinessLayer::L5InertiaPhysical: return "L5";
    case ReadinessLayer::L6ResourceState: return "L6";
    case ReadinessLayer::L7ToolTcp: return "L7";
    case ReadinessLayer::L8BasePlacement: return "L8";
    case ReadinessLayer::L9Drivetrain: return "L9";
    case ReadinessLayer::L10CanonicalReady: return "L10";
    case ReadinessLayer::L11CompileRequestable: return "L11";
    }
    return "L?";
}

std::string_view readinessStatusToken(ReadinessStatus status) noexcept
{
    switch (status) {
    case ReadinessStatus::NotReady: return "NotReady";
    case ReadinessStatus::ReadyWithNotes: return "ReadyWithNotes";
    case ReadinessStatus::Ready: return "Ready";
    }
    return "Unknown";
}

// =====================================================================
// ModelReadinessChecker——分层编排（唯一产品实现）
// =====================================================================

ModelReadinessChecker::ModelReadinessChecker(const AssertionSuite& suite) noexcept
    : m_suite(&suite)
{}

ModelReadinessReport ModelReadinessChecker::check(const ModelingWorkingSet& ws,
                                                  const CheckContext& ctx) const
{
    ModelReadinessReport report;
    const AssertionSuite& suite = *m_suite;

    // 逐层收集面（层序即追加序——展平时按层拼接保证排序契约）。
    std::vector<core::DiagnosticRecord> layerBlockers[kLayerCount];
    std::vector<core::DiagnosticRecord> layerWarnings[kLayerCount];
    std::vector<core::ConfirmableFinding> confirmables;
    std::vector<ReadinessNote> notes;

    // 呈现级结论登记器（构造层保证域/缺项预告/L11 呈现级——无登记码面）。
    auto addNote = [&notes](ReadinessLayer layer, std::string path, std::string summary,
                            bool blocking) {
        ReadinessNote note;
        note.layer = layer;
        note.subjectPath = std::move(path);
        note.summary = std::move(summary);
        note.blocking = blocking;
        notes.push_back(std::move(note));
    };

    // ---- 根对象不变量一次计算（I-MDL 全量——各层按编号取用）----
    // IMdl4/IMdl5 的判定式与 AssertionSuite 的断言域同源（InertiaMath），
    // 其 coded 记录由套件产出（L4/L5）——此处跳过该两编号，避免同事实
    // 双报（NFR-MNT-04 无重复判定）。
    const std::vector<InvariantViolation> rootViolations = checkInvariants(ws.design);
    auto violationsOf = [&rootViolations](InvariantId id) {
        std::vector<const InvariantViolation*> out;
        for (const InvariantViolation& v : rootViolations) {
            if (v.id == id) { out.push_back(&v); }
        }
        return out;
    };
    auto addViolationNotes = [&](ReadinessLayer layer, InvariantId id,
                                 std::string_view summaryPrefix, bool blocking) {
        for (const InvariantViolation* v : violationsOf(id)) {
            addNote(layer, v->subject,
                    std::string(summaryPrefix) + "：" + v->subject, blocking);
        }
    };

    // ---- 各层执行体（§8.2 分层表逐行——严格顺序执行，见下方主循环）----

    auto runL0 = [&] {
        // L0 结构完整（I-MDL-1/2——构造层保证域的防御面；呈现级阻断）。
        addViolationNotes(ReadinessLayer::L0Structure, InvariantId::IMdl1,
                          "结构不变量违例（链连通/计数关系）", true);
        addViolationNotes(ReadinessLayer::L0Structure, InvariantId::IMdl2,
                          "身份唯一性违例（ObjectId/localName 重复）", true);
    };
    auto runL1 = [&] {
        // L1 引用完整（I-MDL-9 闭包半段＝套件 coded；值模型半段呈现级）。
        suite.assertClosureReferences(
            ws.design, ws, layerBlockers[layerIndex(ReadinessLayer::L1References)]);
        addViolationNotes(ReadinessLayer::L1References, InvariantId::IMdl9,
                          "引用表不变量违例（defaultTcp∈toolRefs/无重复）", true);
    };
    auto runL2 = [&] {
        // L2 单位/数值合法（I-MDL-3/8——非法单位构造层不存在；防御面）。
        addViolationNotes(ReadinessLayer::L2UnitsFinite, InvariantId::IMdl3,
                          "数值有限性违例（Provided 值含 NaN/Inf）", true);
        addViolationNotes(ReadinessLayer::L2UnitsFinite, InvariantId::IMdl8,
                          "权威互斥违例（DH 态派生字段携带非派生来源）", true);
        for (const ToolDefinition& tool : ws.toolObjects) {
            for (const InvariantViolation& v : checkInvariants(tool)) {
                if (v.id == InvariantId::IMdl3) {
                    addNote(ReadinessLayer::L2UnitsFinite,
                            "tool:" + tool.localName + "/" + v.subject,
                            "工具数值有限性违例：" + v.subject, true);
                }
            }
        }
    };
    auto runL3 = [&] {
        // L3 关节轴有效（I-MDL-6——同防御面）。
        addViolationNotes(ReadinessLayer::L3JointAxis, InvariantId::IMdl6,
                          "关节轴违例（零轴/次正规——不可归一化）", true);
    };
    auto runL4 = [&] {
        // L4 限位有序（断言④硬断言——区间错/范围未确认，coded）。
        suite.assertJointLimitIntervals(
            ws.design, layerBlockers[layerIndex(ReadinessLayer::L4LimitOrder)]);
    };
    auto runL5 = [&] {
        // L5 惯量合法（连杆断言①②③ coded＋缺失预告 Warning——DataInsufficient）。
        for (const LinkEntry& link : ws.design.links) {
            suite.assertBodyPhysical(
                link.objectId, link.localName, link.body,
                layerBlockers[layerIndex(ReadinessLayer::L5InertiaPhysical)],
                layerWarnings[layerIndex(ReadinessLayer::L5InertiaPhysical)]);
        }
    };
    auto runL6 = [&] {
        // L6 资源存在（Recorded 未固化 Warning——不阻断；状态机违例防御面）。
        suite.checkResourceStates(
            ws.design, layerWarnings[layerIndex(ReadinessLayer::L6ResourceState)]);
        addViolationNotes(ReadinessLayer::L6ResourceState, InvariantId::IMdl10,
                          "资源状态机违例（Recorded/Solidified 必备字段缺失）", true);
    };
    auto runL7 = [&] {
        // L7 工具与 TCP 完整（工具物性 coded＋defaultTcp/tcpKey 呈现级）。
        for (const ToolDefinition& tool : ws.toolObjects) {
            suite.assertBodyPhysical(
                tool.objectId, tool.localName, tool.body,
                layerBlockers[layerIndex(ReadinessLayer::L7ToolTcp)],
                layerWarnings[layerIndex(ReadinessLayer::L7ToolTcp)]);
        }
        if (!ws.toolObjects.empty()) {
            // "有 tools 则 defaultTcp 已设"（KIN-14/I-MDL-9 呈现半段）。
            if (!ws.design.defaultTcp.has_value()) {
                addNote(ReadinessLayer::L7ToolTcp, "defaultTcp",
                        "已配置工具但未设置默认 TCP（应用前须设置——KIN-14）", true);
            } else {
                // tcpKey 存在性：被引工具的 tcpList 须含 defaultTcp.tcpKey
                //（闭包视图可判半段——工具对象经 v0.9 工作集字段进入）。
                bool keyFound = false;
                for (const ToolDefinition& tool : ws.toolObjects) {
                    if (!(tool.objectId == ws.design.defaultTcp->toolOid)) { continue; }
                    for (const TcpEntry& tcp : tool.tcpList) {
                        if (tcp.key == ws.design.defaultTcp->tcpKey) {
                            keyFound = true;
                            break;
                        }
                    }
                    break;
                }
                if (!keyFound) {
                    addNote(ReadinessLayer::L7ToolTcp, "defaultTcp.tcpKey",
                            "defaultTcp 指向的 tcpKey 不在被引工具 tcpList 中（KIN-14）",
                            true);
                }
            }
        }
    };
    auto runL8 = [&] {
        // L8 基座姿态合法（I-MDL-7 防御面；"preset≠ground 而 R=I"在映射层）。
        addViolationNotes(ReadinessLayer::L8BasePlacement, InvariantId::IMdl7,
                          "基座安装违例（custom 缺 customEaa/旋转非正交）", true);
    };
    auto runL9 = [&] {
        // L9 传动可用（I-MDL-11/12 呈现级＋缺省预告）。
        if (ws.drivetrainObject.has_value()) {
            for (const InvariantViolation& v :
                 checkInvariants(*ws.drivetrainObject, CouplingStage::R1Locked)) {
                addNote(ReadinessLayer::L9Drivetrain, v.subject,
                        std::string("传动不变量违例（I-MDL-11/12）：") + v.subject, true);
            }
        } else {
            addNote(ReadinessLayer::L9Drivetrain, "drivetrainRef",
                    "传动设计未配置（缺省——动力学评估将走缺省/降级预告）", false);
        }
    };
    auto runL10 = [&] {
        // L10 可构造 CanonicalModel（schema 版本 coded＋字段集完整呈现级）。
        suite.assertSchemaVersions(
            ws, layerBlockers[layerIndex(ReadinessLayer::L10CanonicalReady)]);
        if (ws.design.authority == AuthorityMode::Explicit) {
            // 字段集完整半段：显式权威下可动关节 axis/origin 须已提供
            //（Description 映射表要求全 SourcedValue 化——缺失即不可构造；
            // DH 权威的派生重算归 T09 IDhExplicitConverter，不在此要求）。
            for (std::size_t i = 0; i < ws.design.joints.size(); ++i) {
                const JointEntry& j = ws.design.joints[i];
                if (j.type == JointType::Fixed) { continue; }
                const std::string path = "joints[" + std::to_string(i) + "]";
                if (j.axis.state() == core::FieldState::NotProvided) {
                    addNote(ReadinessLayer::L10CanonicalReady, path + ".axis",
                            "显式权威下可动关节 axis 未提供——不可构造 Description"
                            "（编辑边界/导入待确认面应先行解决）", true);
                }
                if (j.origin.state() == core::FieldState::NotProvided) {
                    addNote(ReadinessLayer::L10CanonicalReady, path + ".origin",
                            "显式权威下可动关节 origin 未提供——不可构造 Description",
                            true);
                }
            }
        }
    };
    auto runL11 = [&] {
        // L11 可请求编译（writable/baseRevision 呈现级＋行程策略域）。
        if (!ctx.writable) {
            // 只读模式提示（§9.4.4 字段注释原文"只读模式提示（L11 呈现级）"
            // ——写阻断强制点在 project S1 not-writable，本层仅呈现）。
            addNote(ReadinessLayer::L11CompileRequestable, "writable",
                    "项目处于只读模式——应用路径将被命令服务拒绝（not-writable）",
                    false);
        }
        if (ctx.baseRevision.has_value()) {
            addNote(ReadinessLayer::L11CompileRequestable, "baseRevision",
                    "应用预检口径——基线==tip 由 project S2 并发校验强制"
                    "（过期基线 PRJ-STALE-REVISION-REJECTED；prepare 内防御性复核）",
                    false);
        }
        if (hasTravelRelevantJoints(ws.design)) {
            if (ctx.resolvedPolicy == nullptr) {
                // §9.4.4 非法调用行原文：策略不可解析→Blocking"策略不可解析"
                //（不静默跳过行程校验——行程合法性未确认＝应用被阻止，且
                // 明示"校验未执行"事实）。呈现级阻断（④端口的 POLICY-* 诊断
                // 传导面在 prepare——checker 只持解析结果指针）。
                addNote(ReadinessLayer::L11CompileRequestable, "resolvedPolicy",
                        "策略不可解析——行程校验未执行（不静默跳过；应用前须解析有效工程策略）",
                        true);
            } else {
                std::vector<core::DiagnosticRecord> evalDiags;
                const AssertionSuite::TravelEvaluation travel = suite.evaluateTravelLimits(
                    ws.design, *ctx.resolvedPolicy, confirmables, evalDiags);
                if (travel == AssertionSuite::TravelEvaluation::EvaluationFailed) {
                    // 评估未终态化（名称不可解析等——POLICY-* coded 诊断）：
                    // 行程合法性未确认＝Blocking（不静默跳过）。
                    auto& blockers = layerBlockers[layerIndex(ReadinessLayer::L11CompileRequestable)];
                    blockers.insert(blockers.end(), evalDiags.begin(), evalDiags.end());
                    addNote(ReadinessLayer::L11CompileRequestable, "travel-evaluation",
                            "行程上限评估未终态化——行程校验未执行（不静默跳过）", true);
                }
                // FindingsProduced→confirmables（SA-15 待确认集——不阻断状态）。
            }
        }
    };

    // ---- 主循环：严格顺序执行＋短路（§8.2"顺序即短路优先级"）----
    // 每层执行后即判层阻断（coded blockers 或该层 blocking note）——命中
    // 即短路，后续层标记"未执行"（不产出任何结论——与顺序语义一致）。
    const std::array<std::function<void()>, kLayerCount> layerRuns = {
        runL0, runL1, runL2, runL3, runL4, runL5,
        runL6, runL7, runL8, runL9, runL10, runL11};
    bool shortCircuited = false;
    for (std::size_t i = 0; i < kLayerCount; ++i) {
        const ReadinessLayer layer = static_cast<ReadinessLayer>(i);
        LayerDetail detail;
        if (shortCircuited) {
            detail.passed = false;
            detail.note = "低层阻断短路——本层未执行（§8.2 顺序即短路优先级）";
        } else {
            layerRuns[i]();
            const bool layerBlockingNote = std::any_of(
                notes.begin(), notes.end(),
                [&](const ReadinessNote& n) { return n.layer == layer && n.blocking; });
            const bool hasBlocking = !layerBlockers[i].empty() || layerBlockingNote;
            detail.passed = !hasBlocking;
            detail.note = hasBlocking ? "阻断（见 blockers/notes）" : "通过";
            if (hasBlocking) { shortCircuited = true; }
        }
        report.layers[i] = detail;
    }

    // ---- 结果组展平与稳定排序（层号→对象 id 字典序→码字典序）----
    auto bySubjectThenCode = [](const core::DiagnosticRecord& a,
                                const core::DiagnosticRecord& b) {
        const std::string sa = a.subject.has_value() ? a.subject->toCanonical() : std::string();
        const std::string sb = b.subject.has_value() ? b.subject->toCanonical() : std::string();
        if (sa != sb) { return sa < sb; }
        return a.code < b.code;
    };
    for (std::size_t i = 0; i < kLayerCount; ++i) {
        std::sort(layerBlockers[i].begin(), layerBlockers[i].end(), bySubjectThenCode);
        std::sort(layerWarnings[i].begin(), layerWarnings[i].end(), bySubjectThenCode);
        report.blockers.insert(report.blockers.end(), layerBlockers[i].begin(),
                               layerBlockers[i].end());
        report.warnings.insert(report.warnings.end(), layerWarnings[i].begin(),
                               layerWarnings[i].end());
    }
    // 待确认集：按 subject 字典序（层域唯一——L11）。
    std::sort(confirmables.begin(), confirmables.end(),
              [](const core::ConfirmableFinding& a, const core::ConfirmableFinding& b) {
                  const std::string ta = a.record.subject.has_value()
                                           ? a.record.subject->toCanonical()
                                           : std::string();
                  const std::string tb = b.record.subject.has_value()
                                           ? b.record.subject->toCanonical()
                                           : std::string();
                  if (ta != tb) { return ta < tb; }
                  return a.record.code < b.record.code;
              });
    report.confirmables = std::move(confirmables);
    // 呈现级结论：层号→定位路径字典序。
    std::sort(notes.begin(), notes.end(), [](const ReadinessNote& a, const ReadinessNote& b) {
        if (a.layer != b.layer) { return a.layer < b.layer; }
        if (a.subjectPath != b.subjectPath) { return a.subjectPath < b.subjectPath; }
        return a.summary < b.summary;
    });
    report.notes = std::move(notes);

    // ---- 状态汇总（§8.2 就绪状态图）----
    const bool hasBlocking = !report.blockers.empty()
                          || std::any_of(report.notes.begin(), report.notes.end(),
                                         [](const ReadinessNote& n) { return n.blocking; });
    if (hasBlocking) {
        report.status = ReadinessStatus::NotReady;
    } else if (!report.warnings.empty() || !report.confirmables.empty()
               || !report.notes.empty()) {
        report.status = ReadinessStatus::ReadyWithNotes;
    } else {
        report.status = ReadinessStatus::Ready;
    }
    return report;
}

std::unique_ptr<IModelReadinessChecker> makeModelReadinessChecker(const AssertionSuite& suite)
{
    return std::make_unique<ModelReadinessChecker>(suite);
}

}  // namespace sdurws::ird::modeling
