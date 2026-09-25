/**
 * @file   Readiness.cpp
 * @brief  需求就绪校验器实现——R0~R9 分层短路校验（§8.1 表逐行落位）、
 *         REQ-READY-* 稳定码产码与 ReadinessSummary 投影。
 *
 * 设计依据：
 *   - units/requirements.md §8.1（分层表——逐层检查域与级别；跨聚合浅
 *     引用边界："就绪层仅校验 ObjectId 存在于闭包且 objectTypeToken 匹配
 *     （RevisionView.objectRefs 元数据），语义级有效性由评估时解析"）、
 *     §9.5（IRequirementReadinessChecker 契约）、§9.6（T05 行码表）、
 *     §6.2（必验冻结规则——复用 resolveRequiredCases 单点）、§3.4
 *     （纯函数/确定性总约定）
 *   - Readiness.hpp 文件头"层-检查域-稳定码映射"（本实现的逐层执行序
 *     与码选择权威——两处同步维护，本文件不重复展开）
 *   - 需求 REQ-06（Must/Should 分级）、EVI-01（预览分离）、NFR-MNT-04
 *     （checkSequence/resolveRequiredCases 单点复用——无第二套判定）、
 *     NFR-COR-01/02（确定性）
 *   - 任务契约 tasks/foundation/WP-14-T05.json acceptance 1~5/7
 *
 * 实现要点（对应 Readiness.hpp 文件头的映射表，此处只记执行序事实）：
 *   ① 集合不变量前置（I-REQ-1/2/3）违约 fail-fast——合法生产者保证，
 *      到达即调用方契约违约；
 *   ② R0→R9 顺序执行，任一层出现 Blocking 即停止其后各层（短路优先）；
 *      Warning/NotApplicable 不短路（已执行层的 Warning 保留在报告）；
 *   ③ 浅校验只读 ObjectRef 的 objectId 与 objectTypeToken 两字段
 *      （R-1：不解码 modeling 对象字节）；
 *   ④ 顺序拓扑（R7）与必验解析（R5/R9）复用服务单点实现——点/区域
 *      顺序共用 TaskPointService::checkSequence（区域以同名/同键探针
 *      复用，零算法复制）；必验集合共用 resolveRequiredCases（P-EV-9
 *      唯一实现点）。
 *
 * 线程安全：校验器无成员状态；全部辅助为纯函数——并发可重入。
 * 确定性：条目遍历序＝集合规范序（I-REQ-1）；诊断 context 数值经
 * std::to_string 固定格式化；无 locale/时钟依赖（NFR-COR-01/02）。
 */

#include <sdurws/ird/requirements/Readiness.hpp>

#include <sdurws/ird/requirements/Errors.hpp>      // RequirementError——值面校验错误
#include <sdurws/ird/requirements/RequirementTypes.hpp>  // validate* 值面校验层（R2/R3/R6/R8 复用）
#include <sdurws/ird/requirements/Services.hpp>    // TaskPointService/OperatingConditionService 单点

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <utility>

namespace sdurws::ird::requirements {

// =====================================================================
// 词表转发表（switch 全枚举——新增枚举值漏登记时编译器告警暴露）
// =====================================================================

std::string_view readinessLayerToken(ReadinessCheckLayer layer) noexcept
{
    switch (layer) {
    case ReadinessCheckLayer::R0: return "R0";
    case ReadinessCheckLayer::R1: return "R1";
    case ReadinessCheckLayer::R2: return "R2";
    case ReadinessCheckLayer::R3: return "R3";
    case ReadinessCheckLayer::R4: return "R4";
    case ReadinessCheckLayer::R5: return "R5";
    case ReadinessCheckLayer::R6: return "R6";
    case ReadinessCheckLayer::R7: return "R7";
    case ReadinessCheckLayer::R8: return "R8";
    case ReadinessCheckLayer::R9: return "R9";
    }
    return "?";  // 不可达（全枚举 switch——防御性返回，调用方不可见）
}

std::string_view readinessFindingLevelToken(ReadinessFindingLevel level) noexcept
{
    switch (level) {
    case ReadinessFindingLevel::Blocking: return "Blocking";
    case ReadinessFindingLevel::Warning: return "Warning";
    case ReadinessFindingLevel::NotApplicable: return "NotApplicable";
    }
    return "?";  // 不可达（全枚举 switch）
}

bool RequirementReadinessReport::hasBlocking() const noexcept
{
    // 线性扫描（发现规模＝条目数级，无索引必要）；Blocking 是唯一使
    // 输入未就绪的级别（Warning/NotApplicable 不改变就绪结论）。
    for (const DomainReadinessItem& item : items) {
        if (item.level == ReadinessFindingLevel::Blocking) {
            return true;
        }
    }
    return false;
}

namespace {

// ---------------------------------------------------------------------
// 诊断构造与闭包查找辅助（文件内私有——纯函数）
// ---------------------------------------------------------------------

/// 稳定码诊断构造（core::DiagnosticRecord::make 的 C-3 契约由本处字面
/// 保证——context/cause/action 均非空；码语法经工厂校验，违约即实现
/// 缺陷 fail-fast）。subject/localName 由调用方按定位粒度给出。
core::DiagnosticRecord makeReadyDiag(std::string_view code,
                                     std::optional<core::ObjectId> subject,
                                     std::optional<std::string> localName,
                                     std::string context, std::string cause,
                                     std::string action)
{
    return core::DiagnosticRecord::make(std::string{code}, std::move(subject),
                                        std::move(localName), std::nullopt,
                                        std::move(context), std::move(cause),
                                        std::move(action));
}

/// 闭包引用查找（oid→ObjectRef；线性扫描——闭包规模为对象数级，且保序
/// 遍历确定性优先于哈希加速，同 RequirementTypes 唯一性扫描同款取舍）。
const project::ObjectRef* findClosureRef(const CheckContext& ctx,
                                         const core::ObjectId& oid)
{
    for (const project::ObjectRef& ref : ctx.closureRefs) {
        if (ref.objectId == oid) {
            return &ref;
        }
    }
    return nullptr;
}

/// 闭包浅核对（§8.1 跨闭包半区的单点实现）：目标存在于闭包且登记
/// token 与期望一致。返回 nullopt＝通过；返回字符串＝违例原因（悬空/
/// token 失配——供诊断 cause 复用，机器判别以稳定码为准）。
std::optional<std::string> closureRefViolation(const CheckContext& ctx,
                                               const core::ObjectId& oid,
                                               std::string_view expectedToken)
{
    const project::ObjectRef* ref = findClosureRef(ctx, oid);
    if (ref == nullptr) {
        return std::string("引用目标不在修订闭包中（悬空——") + oid.toCanonical()
               + "）；语义级有效性归评估时解析（§8.1 浅校验边界）";
    }
    if (ref->objectTypeToken != expectedToken) {
        return std::string("闭包登记类型 token 不匹配（登记 ")
               + ref->objectTypeToken + "，期望 " + std::string{expectedToken}
               + "）";
    }
    return std::nullopt;
}

/// 报告追加（items 序＝执行序——短路序，确定性）。
void appendItem(RequirementReadinessReport& report, ReadinessCheckLayer layer,
                ReadinessFindingLevel level, core::DiagnosticRecord diag)
{
    report.items.push_back(DomainReadinessItem{layer, level, std::move(diag)});
}

/// 汇总 Must 门禁清单（check 各返回点的公共收尾）：从 Blocking 发现的
/// subject 中筛出"启用∧Must"条目（REQ-06 口径——enabled=false 与
/// Should 条目不进入判定清单），升序去重输出。
///
/// conservativeMapping 说明：valid（=hasBlocking）面向全部 Blocking
/// ——Should/集合级非法同样使输入不可消费（非法数据不可编码成正式
/// 切片——保守门禁）；Must 清单只列 REQ-06 字面口径的启用 Must 条目
/// ——两层面的分工登记于 Readiness.hpp §readinessSummary 注。
std::vector<std::string> collectInvalidMustItems(
    const RequirementReadinessReport& report,
    const std::set<std::string>& enabledMustIds)
{
    std::set<std::string> unique;  // set 兼做去重＋升序（NFR-COR-02）
    for (const DomainReadinessItem& item : report.items) {
        if (item.level != ReadinessFindingLevel::Blocking) {
            continue;
        }
        if (!item.diag.subject.has_value()) {
            continue;  // 集合级发现（根引用表/schema）不归属具体条目
        }
        const std::string canonical = item.diag.subject->toCanonical();
        if (enabledMustIds.find(canonical) != enabledMustIds.end()) {
            unique.insert(canonical);
        }
    }
    return std::vector<std::string>(unique.begin(), unique.end());
}

/// R0 根引用槽描述（槽名→期望集合 token——§4.2 根对象字段表的机器面；
/// 顺序＝字段表行序，确定性）。
struct RootSlot {
    const char* slotName;             // 槽名（诊断 context 用）
    const std::optional<core::ObjectId>* ref;  // 根对象槽值
    std::string_view expectedToken;   // 该槽期望的对象类型 token
};

/// 单条目位姿值面重估（R3——§8.1 R3 行字面域：数值有限/容差>0/
/// constrainedDof 非空/姿态规则参数）。复用值面校验层单点（validate*
/// ——构造边界/解码链同一套谓词）并映射为就绪定位码；引用面（refFrame/
/// tcpRef 结构）不在本函数——分别归 R1/R2/R8（层-域划分见 Readiness.hpp
/// 映射表）。三段距离谓词与 validateTaskPoint 的三段半区同语义（正有限
/// ——数学同源），此处独立承载是为把引用面从 R3 的入参域中剔除，避免
/// R8 未到的 tcpRef 结构违约误标为位姿非法。
std::optional<RequirementError> poseValueViolation(const TaskPoint& point)
{
    // ①位姿约束（位置有限/掩码非空/姿态规则参数——validatePoseConstraint
    //   的语义单源）；
    if (auto e = validatePoseConstraint(point.pose)) {
        return e;
    }
    // ②容差两分量 >0 且有限（I-REQ-5 后半）；
    if (auto e = validateTolerance(point.tolerance)) {
        return e;
    }
    // ③启用段距离 >0 且有限（m；approach/retract 可关闭——关闭段不核，
    //   work 段恒启用。谓词与 validateTaskPoint 三段半区同源：正有限）。
    const TaskSegment* segs[3] = {&point.approach, &point.work, &point.retract};
    const char* segNames[3] = {"approach", "work", "retract"};
    for (int i = 0; i < 3; ++i) {
        const double d = segs[i]->distanceM;
        if (segs[i]->enabled && (!std::isfinite(d) || d <= 0.0)) {
            RequirementError e;
            e.code = RequirementErrorCode::IllegalTolerance;
            e.params.emplace_back("field", std::string{segNames[i]} + ".distanceM");
            e.detail = "requirements/readiness: 启用段距离须为正有限值（m；§5.1）";
            return e;
        }
    }
    return std::nullopt;
}

/// 工作集条目的"启用∧Must"身份集（Must 门禁清单的过滤基准——三类带
/// 等级条目（点/区域/工况）汇总；计划条目无等级字段不参与）。
std::set<std::string> collectEnabledMustIds(const RequirementWorkingSet& ws)
{
    std::set<std::string> ids;
    for (const TaskPoint& p : ws.points.entries) {
        if (p.enabled && p.level == RequirementLevel::Must) {
            ids.insert(p.objectId.toCanonical());
        }
    }
    for (const WorkRegion& r : ws.regions.entries) {
        if (r.enabled && r.level == RequirementLevel::Must) {
            ids.insert(r.objectId.toCanonical());
        }
    }
    for (const OperatingCondition& c : ws.conditions.entries) {
        if (c.enabled && c.level == RequirementLevel::Must) {
            ids.insert(c.objectId.toCanonical());
        }
    }
    return ids;
}

/// 工作集不变量前置核查（I-REQ-1/2/3——调用方契约违约 fail-fast 面）。
/// 合法生产者（编辑器 applyEdit 拒绝面/Codec 解码校验链④/命令基线重建
/// 同源解码）保证不变量；直接构造工作集绕过生产者的调用＝契约违约，
/// 按错误二分 fail-fast（不产诊断——诊断面只承载输入数据事实）。
void assertWorkingSetInvariants(const RequirementWorkingSet& ws)
{
    const auto fail = [](const RequirementError& e) {
        throw std::invalid_argument(
            "requirements/readiness: 工作集不变量违约（I-REQ-1/2/3——调用方"
            "契约违约；合法生产者保证不变量，详见 Readiness.hpp 文件头）: "
            + e.detail);
    };
    // I-REQ-1：集合条目规范序（canonical 编码/字节面假设的前提）。
    if (!isCanonicalOrder(ws.points.entries) || !isCanonicalOrder(ws.regions.entries)
        || !isCanonicalOrder(ws.conditions.entries) || !isCanonicalOrder(ws.plans.entries)) {
        RequirementError e;
        e.code = RequirementErrorCode::MalformedPayload;
        e.detail = "requirements/readiness: 集合条目未按 ObjectId 规范序（I-REQ-1）";
        fail(e);
    }
    // I-REQ-3＋I-REQ-2（集合内半区）：名称唯一＋id 非空唯一（三个命名
    // 条目集合——计划条目无 name 字段，id 唯一性单独核对）。
    if (auto e = checkEntryNameAndIdUniqueness(ws.points.entries)) { fail(*e); }
    if (auto e = checkEntryNameAndIdUniqueness(ws.regions.entries)) { fail(*e); }
    if (auto e = checkEntryNameAndIdUniqueness(ws.conditions.entries)) { fail(*e); }
    // 计划集合内 id 唯一（I-REQ-2 集合内半区——SamplingPlan 无 name，
    // checkEntryNameAndIdUniqueness 模板不适用；逐对扫描同款确定性口径）。
    for (std::size_t i = 0; i < ws.plans.entries.size(); ++i) {
        for (std::size_t j = 0; j < i; ++j) {
            if (ws.plans.entries[i].objectId == ws.plans.entries[j].objectId) {
                RequirementError e;
                e.code = RequirementErrorCode::MalformedPayload;
                e.params.emplace_back("object-id", ws.plans.entries[i].objectId.toCanonical());
                e.detail = "requirements/readiness: 计划集合内 ObjectId 重复（I-REQ-2）";
                fail(e);
            }
        }
    }
    // I-REQ-2（跨集合半区）：同一 id 不得跨集合复用。
    if (auto e = checkCrossSetIdUniqueness(ws.points.entries, ws.regions.entries,
                                           ws.conditions.entries, ws.plans.entries)) {
        fail(*e);
    }
}

}  // namespace

// =====================================================================
// check——R0→R9 分层短路校验（§8.1 表逐行；层-码映射见 Readiness.hpp）
// =====================================================================

RequirementReadinessReport RequirementReadinessChecker::check(
    const RequirementWorkingSet& ws, const CheckContext& ctx) const
{
    // ---- 前置：集合不变量（违约 fail-fast——见 assertWorkingSetInvariants）----
    assertWorkingSetInvariants(ws);

    // Must 门禁过滤基准（一次收集，各短路返回点共用）。
    const std::set<std::string> enabledMustIds = collectEnabledMustIds(ws);

    // 报告与"本层出现 Blocking 即收尾返回"的公共收尾闭包（短路语义：
    // 填 Must 清单后整报告返回——已执行层的 Warning 保留）。
    RequirementReadinessReport report;
    const auto blockedNow = [&]() {
        report.invalidMustItems = collectInvalidMustItems(report, enabledMustIds);
        return report;
    };

    // ---- R0 结构完整：根引用表四槽解析＋集合 token 一致（§8.1 行 1）----
    {
        // 槽表＝§4.2 根对象字段表的四引用行（行序＝字段表序）。
        const RootSlot slots[4] = {
            {"pointSetRef", &ws.root.pointSetRef, kReqPointSetObjectType},
            {"regionSetRef", &ws.root.regionSetRef, kReqRegionSetObjectType},
            {"conditionSetRef", &ws.root.conditionSetRef, kReqConditionSetObjectType},
            {"planSetRef", &ws.root.planSetRef, kReqPlanSetObjectType},
        };
        bool r0Blocking = false;
        for (const RootSlot& slot : slots) {
            if (!slot.ref->has_value()) {
                continue;  // 槽未设置＝合法（§4.2 可选引用——如无区域任务）
            }
            const core::ObjectId oid = **slot.ref;
            // 悬空：目标不在闭包（根引用表解析失败——Blocking）。
            const project::ObjectRef* ref = findClosureRef(ctx, oid);
            if (ref == nullptr) {
                appendItem(report, ReadinessCheckLayer::R0,
                           ReadinessFindingLevel::Blocking,
                           makeReadyDiag(kReqReadyRefMissing, oid, std::nullopt,
                                         std::string("slot=") + slot.slotName,
                                         "根引用表槽指向的对象不在修订闭包中（悬空）",
                                         "恢复该集合对象或移除根引用表中的该槽引用"));
                r0Blocking = true;
                continue;
            }
            // 集合 token 一致：闭包登记类型与本槽期望集合对象 token 失配。
            if (ref->objectTypeToken != slot.expectedToken) {
                appendItem(report, ReadinessCheckLayer::R0,
                           ReadinessFindingLevel::Blocking,
                           makeReadyDiag(kReqReadyRefMissing, oid, std::nullopt,
                                         std::string("slot=") + slot.slotName,
                                         std::string("闭包登记 token 不匹配（登记 ")
                                             + ref->objectTypeToken + "，期望 "
                                             + std::string{slot.expectedToken} + "）",
                                         "核对根引用表槽与对象类型的对应关系"));
                r0Blocking = true;
            }
        }
        // 四槽互不重复（同一对象被两槽引用＝引用表结构性违约——同一
        // 对象不能既是点集又是区域集）。
        for (std::size_t i = 0; i < 4; ++i) {
            if (!slots[i].ref->has_value()) { continue; }
            for (std::size_t j = 0; j < i; ++j) {
                if (slots[j].ref->has_value() && **slots[i].ref == **slots[j].ref) {
                    appendItem(report, ReadinessCheckLayer::R0,
                               ReadinessFindingLevel::Blocking,
                               makeReadyDiag(kReqReadyRefMissing, **slots[i].ref,
                                             std::nullopt,
                                             std::string("slot=") + slots[i].slotName
                                                 + "+" + slots[j].slotName,
                                             "根引用表两槽引用同一对象（集合对象"
                                             "每需求集至多一份——§4.1）",
                                             "核对根引用表，移除重复槽引用"));
                    r0Blocking = true;
                }
            }
        }
        if (r0Blocking) {
            return blockedNow();  // 短路：结构不完整时后层前提不成立
        }
    }

    // ---- R1 引用完整（浅校验）：refFrame 目标/姿态规则目标闭包核对 ----
    {
        bool r1Blocking = false;
        // 逐条目 refFrame 的 ModelFrame/SceneObject 半区（World 缺省恒合法
        // ——无目标不参与闭包核对，§8.1 R2 行"World 缺省合法"）。
        const auto checkSceneRef = [&](ReadinessCheckLayer layer,
                                       const core::ObjectId& entryOid,
                                       const std::string& entryName,
                                       const RequirementReference& ref) {
            if (ref.kind != RequirementRefKind::ModelFrame
                && ref.kind != RequirementRefKind::SceneObject) {
                return;  // World/Tool/DefaultTcp 不属本半区（Tool 归 R8）
            }
            // 结构守卫（载荷与 kind 匹配——ModelFrame/SceneObject 必携
            // ObjectId）：违约直接拦截，防下方解引用空载荷（防御面——
            // 构造边界已保证）。
            if (!ref.wellFormed() || !ref.objectId.has_value()) {
                appendItem(report, layer, ReadinessFindingLevel::Blocking,
                           makeReadyDiag(kReqReadyRefMissing, entryOid, entryName,
                                         "field=refFrame; kind="
                                             + std::string(requirementRefKindToken(ref.kind)),
                                         "引用结构非法（kind 与载荷不匹配——缺目标 "
                                             "ObjectId）",
                                         "修正引用使载荷与种类匹配，或改用 World"));
                r1Blocking = true;
                return;
            }
            if (auto violation = closureRefViolation(ctx, *ref.objectId,
                                                     expectedTargetToken(ref.kind))) {
                appendItem(report, layer, ReadinessFindingLevel::Blocking,
                           makeReadyDiag(kReqReadyRefMissing, entryOid, entryName,
                                         "field=refFrame; target="
                                             + ref.objectId->toCanonical(),
                                         *violation,
                                         "修正参考系引用或改用 World 缺省"));
                r1Blocking = true;
            }
        };
        for (const TaskPoint& p : ws.points.entries) {
            if (!p.enabled) { continue; }  // 未启用条目不进入就绪判定（§4.3 enabled 行）
            checkSceneRef(ReadinessCheckLayer::R1, p.objectId, p.name, p.refFrame);
            // AlignFrame 目标参考系（ModelFrame/SceneObject——§5.3 第二规则）。
            if (p.pose.orientation.kind == OrientationRuleKind::AlignFrame) {
                const RequirementReference& tf = p.pose.orientation.targetFrame;
                // 结构守卫（validateOrientationRule 构造面已保证——防御）。
                if (!tf.wellFormed() || !tf.objectId.has_value()) {
                    appendItem(report, ReadinessCheckLayer::R1,
                               ReadinessFindingLevel::Blocking,
                               makeReadyDiag(kReqReadyRefMissing, p.objectId, p.name,
                                             "field=pose.orientation.targetFrame",
                                             "对齐目标参考系引用结构非法（§5.3——"
                                                 "须为 ModelFrame/SceneObject 且载荷自洽）",
                                             "修正对齐目标引用"));
                    r1Blocking = true;
                } else if (auto violation = closureRefViolation(
                               ctx, *tf.objectId, expectedTargetToken(tf.kind))) {
                    appendItem(report, ReadinessCheckLayer::R1,
                               ReadinessFindingLevel::Blocking,
                               makeReadyDiag(kReqReadyRefMissing, p.objectId, p.name,
                                             "field=pose.orientation.targetFrame; target="
                                                 + tf.objectId->toCanonical(),
                                             *violation,
                                             "修正对齐目标参考系引用"));
                    r1Blocking = true;
                }
            }
            // AlignGeometryNormal 目标场景对象（scene-object——§5.3 第三规则）。
            if (p.pose.orientation.kind == OrientationRuleKind::AlignGeometryNormal
                && p.pose.orientation.targetSceneObject.has_value()) {
                if (auto violation = closureRefViolation(
                        ctx, *p.pose.orientation.targetSceneObject, "scene-object")) {
                    appendItem(report, ReadinessCheckLayer::R1,
                               ReadinessFindingLevel::Blocking,
                               makeReadyDiag(kReqReadyRefMissing, p.objectId, p.name,
                                             "field=pose.orientation.targetSceneObject; target="
                                                 + p.pose.orientation.targetSceneObject
                                                       ->toCanonical(),
                                             *violation,
                                             "修正对齐目标场景对象引用"));
                    r1Blocking = true;
                }
            }
        }
        for (const WorkRegion& r : ws.regions.entries) {
            if (!r.enabled) { continue; }  // 未启用条目不进入就绪判定（§4.4 enabled 行）
            checkSceneRef(ReadinessCheckLayer::R1, r.objectId, r.name, r.refFrame);
        }
        if (r1Blocking) {
            return blockedNow();  // 短路
        }
    }

    // ---- R2 坐标系有效：refFrame 槽-种类合法（forScene 结构面；防御——
    //      构造边界已保证，直接构造工作集的违约在此显式拦截）----
    {
        bool r2Blocking = false;
        const auto checkRefFrameKind = [&](const core::ObjectId& oid,
                                           const std::string& name,
                                           const RequirementReference& ref) {
            // 场景槽允许 kind＝World/ModelFrame/SceneObject（validateRequirement
            // Reference 的 forScene 半区——语义单源复用）。
            if (auto e = validateRequirementReference(ref, /*forScene=*/true)) {
                appendItem(report, ReadinessCheckLayer::R2,
                           ReadinessFindingLevel::Blocking,
                           makeReadyDiag(kReqReadyRefMissing, oid, name,
                                         "field=refFrame; kind="
                                             + std::string(requirementRefKindToken(ref.kind)),
                                         "refFrame 引用种类与场景槽不匹配（"
                                             + e->detail + "）",
                                         "改用 World/ModelFrame/SceneObject 引用"));
                r2Blocking = true;
            }
        };
        for (const TaskPoint& p : ws.points.entries) {
            if (!p.enabled) { continue; }  // 未启用条目不进入就绪判定（§4.3 enabled 行）
            checkRefFrameKind(p.objectId, p.name, p.refFrame);
        }
        for (const WorkRegion& r : ws.regions.entries) {
            if (!r.enabled) { continue; }  // 同上
            checkRefFrameKind(r.objectId, r.name, r.refFrame);
        }
        if (r2Blocking) {
            return blockedNow();  // 短路
        }
    }

    // ---- R3 位姿合法：任务点逐条值面重估（I-REQ-5——§8.1 R3 行字面域）----
    {
        bool r3Blocking = false;
        for (const TaskPoint& p : ws.points.entries) {
            if (!p.enabled) { continue; }  // 未启用条目不进入就绪判定（§4.3 enabled 行）
            if (auto e = poseValueViolation(p)) {
                appendItem(report, ReadinessCheckLayer::R3,
                           ReadinessFindingLevel::Blocking,
                           makeReadyDiag(kReqReadyPoseIllegal, p.objectId, p.name,
                                         "field=" + (e->params.empty()
                                                         ? std::string{"pose"}
                                                         : e->params.front().second),
                                         e->detail,
                                         "修正该任务点的位姿/容差/约束分量参数"));
                r3Blocking = true;
            }
        }
        if (r3Blocking) {
            return blockedNow();  // 短路
        }
    }

    // ---- R4 工况绑定完整：appliesTo/stationRef 指向存在的任务点条目 ----
    {
        // 任务点条目 id 索引（子条目 ObjectId——O-36 模型内锚，不入闭包，
        // 在点集内定位；集合规范序遍历＝确定性）。
        std::set<std::string> pointIds;
        for (const TaskPoint& p : ws.points.entries) {
            pointIds.insert(p.objectId.toCanonical());
        }
        bool r4Blocking = false;
        for (const OperatingCondition& c : ws.conditions.entries) {
            if (!c.enabled) { continue; }  // 未启用条目不进入就绪判定（§4.5 enabled 行）
            const auto requireStation = [&](const core::ObjectId& stationOid,
                                            std::string_view field) {
                if (pointIds.find(stationOid.toCanonical()) == pointIds.end()) {
                    appendItem(report, ReadinessCheckLayer::R4,
                               ReadinessFindingLevel::Blocking,
                               makeReadyDiag(kReqReadyRefMissing, c.objectId, c.name,
                                             std::string{"field="} + std::string{field}
                                                 + "; station=" + stationOid.toCanonical(),
                                             "工况绑定的任务点条目不存在（悬空——"
                                             "appliesTo/stationRef 指向点集条目）",
                                             "修正绑定到存在的任务点，或改用 "
                                                 "AllStations/None 范围"));
                    r4Blocking = true;
                }
            };
            // Stations 空清单＝§4.7 非法组合（构造边界已拒绝——防御面）。
            if (c.appliesTo.scope == AppliesToScope::Stations
                && c.appliesTo.stations.empty()) {
                appendItem(report, ReadinessCheckLayer::R4,
                           ReadinessFindingLevel::Blocking,
                           makeReadyDiag(kReqReadyRefMissing, c.objectId, c.name,
                                         "field=appliesTo.stations",
                                         "appliesTo.scope=Stations 且清单为空"
                                         "（§4.7 非法组合）",
                                         "补全工位清单或改用 AllStations/None"));
                r4Blocking = true;
            }
            if (c.appliesTo.scope == AppliesToScope::Stations) {
                for (const core::ObjectId& s : c.appliesTo.stations) {
                    requireStation(s, "appliesTo.stations");
                }
            }
            for (const ConditionEvent& ev : c.events) {
                requireStation(ev.stationRef, "events.stationRef");
            }
        }
        if (r4Blocking) {
            return blockedNow();  // 短路
        }
    }

    // ---- R5 必验范围明确：必验集合（enabled∧Must）为空→Warning ----
    //（经 resolveRequiredCases 单点复用——P-EV-9 唯一实现点，NFR-MNT-04；
    //  空集不阻断应用，正式拦截归 evidence P-EV-7——§8.1 R5 行原文。）
    {
        const OperatingConditionService conditionService;
        const RequiredCaseResolution resolution =
            conditionService.resolveRequiredCases(ws.conditions.entries);
        if (resolution.requiredCount == 0) {
            appendItem(report, ReadinessCheckLayer::R5,
                       ReadinessFindingLevel::Warning,
                       makeReadyDiag(kReqReadyNoRequiredCase, std::nullopt,
                                     std::nullopt,
                                     "conditions=" + std::to_string(
                                         ws.conditions.entries.size())
                                         + "; required=0",
                                     "无启用必验工况（必验集合＝enabled∧Must，"
                                     "§6.2 冻结规则；正式拦截归 evidence）",
                                     "如需正式覆盖判定，启用或新增 Must 级工况"));
        }
    }

    // ---- R6 采样计划有效：区域参数/计划形态/计划-区域一一对应 ----
    {
        const bool noRegions = ws.regions.entries.empty();
        const bool noPlans = ws.plans.entries.empty();
        if (noRegions && noPlans) {
            // 显式标记（§8.1 级别行原文示例"无区域任务"——检查域缺席，
            // 非违例非警告）。
            appendItem(report, ReadinessCheckLayer::R6,
                       ReadinessFindingLevel::NotApplicable,
                       makeReadyDiag(kReqReadyPlanDegenerate, std::nullopt,
                                     std::nullopt, "regions=0; plans=0",
                                     "本工作集无区域任务——采样计划面不适用"
                                     "（显式标记，非违例）",
                                     "无需处理（如需覆盖率评估请定义工作区域）"));
        } else {
            bool r6Blocking = false;
            // 区域侧：盒非退化/覆盖率∈[0,1]/区域采样定义（I-REQ-6 值面
            // 单点复用；refFrame/tcpRef 结构归 R1/R2/R8——不在本层重复）。
            for (const WorkRegion& r : ws.regions.entries) {
                if (auto e = validateBoundingBox(r.box)) {
                    appendItem(report, ReadinessCheckLayer::R6,
                               ReadinessFindingLevel::Blocking,
                               makeReadyDiag(kReqReadyPlanDegenerate, r.objectId,
                                             r.name, "field=box",
                                             e->detail,
                                             "修正区域盒尺寸（三分量为正有限值，m）"));
                    r6Blocking = true;
                }
                if (auto e = validateCoverageTargets(r.coverageTargets)) {
                    appendItem(report, ReadinessCheckLayer::R6,
                               ReadinessFindingLevel::Blocking,
                               makeReadyDiag(kReqReadyPlanDegenerate, r.objectId,
                                             r.name, "field=coverageTargets",
                                             e->detail,
                                             "修正覆盖率目标（∈[0,1]）"));
                    r6Blocking = true;
                }
                // GridBySpacing 间距三分量>0 有限（区域侧编辑态表达——
                // 规范化在 buildPlan；间距非法则规范化无定义，§5.2）。
                if (r.positionSampling.method == PositionSamplingMethod::GridBySpacing) {
                    for (int i = 0; i < 3; ++i) {
                        const double s = r.positionSampling.spacing[static_cast<std::size_t>(i)];
                        if (!std::isfinite(s) || s <= 0.0) {
                            appendItem(report, ReadinessCheckLayer::R6,
                                       ReadinessFindingLevel::Blocking,
                                       makeReadyDiag(kReqReadyPlanDegenerate,
                                                     r.objectId, r.name,
                                                     "field=positionSampling.spacing",
                                                     "间距式采样间距须为正有限值"
                                                     "（m；§5.2 GridBySpacing）",
                                                     "修正采样间距或改用显式计数 Grid"));
                            r6Blocking = true;
                        }
                    }
                }
                // 姿态采样两组计数 ≥1（§5.2——零样本仅位置侧表达）。
                if (r.orientationSampling.directionSamples < 1
                    || r.orientationSampling.rollSamples < 1) {
                    appendItem(report, ReadinessCheckLayer::R6,
                               ReadinessFindingLevel::Blocking,
                               makeReadyDiag(kReqReadyPlanDegenerate, r.objectId,
                                             r.name, "field=orientationSampling",
                                             "姿态采样计数须 ≥1（§5.2）",
                                             "修正姿态采样计数"));
                    r6Blocking = true;
                }
                // 位置 Grid 计数乘积=0 合法（零样本由评估判定——V-02，
                // 不产发现；uint32 类型面保证计数非负）。
            }
            // 计划侧：条目形态（规范化 Grid/姿态 ≥1/regionRef 有效）＋
            // 计划-区域一一对应（validateSamplingPlan 值面单点复用）。
            std::multiset<std::string> planRegionRefs;  // 一一对应核对面
            for (const SamplingPlan& plan : ws.plans.entries) {
                if (auto e = validateSamplingPlan(plan)) {
                    appendItem(report, ReadinessCheckLayer::R6,
                               ReadinessFindingLevel::Blocking,
                               makeReadyDiag(kReqReadyPlanDegenerate, plan.objectId,
                                             std::nullopt, "field=plan",
                                             e->detail,
                                             "修正计划条目（规范化 Grid 形态/"
                                             "姿态采样 ≥1/区域引用有效）"));
                    r6Blocking = true;
                    continue;
                }
                planRegionRefs.insert(plan.regionRef.toCanonical());
            }
            // 悬空区域引用（计划指向不存在的区域条目——跨集合模型内锚，
            // O-36：在区域集合内定位，非闭包对象）。
            {
                std::set<std::string> regionIds;
                for (const WorkRegion& r : ws.regions.entries) {
                    regionIds.insert(r.objectId.toCanonical());
                }
                for (const SamplingPlan& plan : ws.plans.entries) {
                    if (regionIds.find(plan.regionRef.toCanonical()) == regionIds.end()) {
                        appendItem(report, ReadinessCheckLayer::R6,
                                   ReadinessFindingLevel::Blocking,
                                   makeReadyDiag(kReqReadyPlanDegenerate, plan.objectId,
                                                 std::nullopt,
                                                 "field=regionRef; region="
                                                     + plan.regionRef.toCanonical(),
                                                 "计划指向的区域条目不存在"
                                                 "（计划-区域失配）",
                                                 "修正计划的区域引用"));
                        r6Blocking = true;
                    }
                }
            }
            // 一区多计划（一一对应违例——同一区域被两个计划声明）。
            for (const SamplingPlan& plan : ws.plans.entries) {
                if (planRegionRefs.count(plan.regionRef.toCanonical()) > 1) {
                    appendItem(report, ReadinessCheckLayer::R6,
                               ReadinessFindingLevel::Blocking,
                               makeReadyDiag(kReqReadyPlanDegenerate, plan.objectId,
                                             std::nullopt,
                                             "field=regionRef; region="
                                                 + plan.regionRef.toCanonical(),
                                             "同一区域被多个计划声明（计划-区域"
                                             "须一一对应——§8.1 R6）",
                                             "合并计划或移除多余计划条目"));
                    r6Blocking = true;
                }
            }
            // 零计划预告（区域非空而计划集为空——§8.1 R6"Warning（零计划）"
            // 分支；正式判定归评估/KIN-04，本码为预告登记面）。
            if (!noRegions && noPlans) {
                appendItem(report, ReadinessCheckLayer::R6,
                           ReadinessFindingLevel::Warning,
                           makeReadyDiag(kReqReadyPlanMissing, std::nullopt,
                                         std::nullopt,
                                         "regions=" + std::to_string(
                                             ws.regions.entries.size())
                                             + "; plans=0",
                                         "区域已定义而采样计划集为空（评估期"
                                         "零样本——DataInsufficient 预告）",
                                         "为区域建立采样计划，或确认零样本评估"
                                         "意图"));
            }
            if (r6Blocking) {
                return blockedNow();  // 短路（R5 Warning 保留在报告中）
            }
        }
    }

    // ---- R7 任务顺序无环：点/区域顺序键拓扑（复用 checkSequence 单点）----
    {
        const TaskPointService pointService;
        // 任务点顺序（I-REQ-7 单一实现——NFR-MNT-04）；仅启用条目进入
        // 判定（§4.3 enabled 行——探针过滤未启用条目）。
        std::vector<TaskPoint> pointProbes;
        pointProbes.reserve(ws.points.entries.size());
        for (const TaskPoint& p : ws.points.entries) {
            if (!p.enabled) { continue; }
            pointProbes.push_back(p);
        }
        const SequenceCheckResult pointsSeq = pointService.checkSequence(pointProbes);
        // 区域顺序（§4.4"同任务点语义；R7 校验面归 T05"）：以同名/同键
        // 探针复用同一算法实现——零算法复制（checkSequence 只读 name 与
        // sequenceKey 两字段，探针构造不引入语义漂移）。
        std::vector<TaskPoint> regionProbes;
        regionProbes.reserve(ws.regions.entries.size());
        for (const WorkRegion& r : ws.regions.entries) {
            if (!r.enabled) { continue; }
            TaskPoint probe;
            probe.name = r.name;
            probe.sequenceKey = r.sequenceKey;
            regionProbes.push_back(std::move(probe));
        }
        const SequenceCheckResult regionsSeq = pointService.checkSequence(regionProbes);

        const bool noDeclarations = [&] {
            for (const TaskPoint& p : ws.points.entries) {
                if (p.enabled && p.sequenceKey.has_value()) { return false; }
            }
            for (const WorkRegion& r : ws.regions.entries) {
                if (r.enabled && r.sequenceKey.has_value()) { return false; }
            }
            return true;
        }();
        if (noDeclarations) {
            // 显式标记：无顺序声明（并行/无序条目集——检查域缺席）。
            appendItem(report, ReadinessCheckLayer::R7,
                       ReadinessFindingLevel::NotApplicable,
                       makeReadyDiag(kReqReadySeqCycle, std::nullopt, std::nullopt,
                                     "sequenceDeclarations=0",
                                     "本工作集无顺序键声明——顺序拓扑面不适用"
                                     "（显式标记，非违例）",
                                     "无需处理（顺序执行类任务请声明 sequenceKey）"));
        } else {
            bool r7Blocking = false;
            // 三类违例逐项定位（§9.4 SequenceCheckResult 清单已升序去重
            // ——确定性序；悬空前驱非字面"成环/重复"，按层-码对齐原则
            // 以 R7 层码承载、cause 区分——Readiness.hpp 映射表登记）。
            const auto emitKeys = [&](const std::vector<std::string>& keys,
                                      const char* kind, const char* action) {
                for (const std::string& key : keys) {
                    appendItem(report, ReadinessCheckLayer::R7,
                               ReadinessFindingLevel::Blocking,
                               makeReadyDiag(kReqReadySeqCycle, std::nullopt, key,
                                             std::string{"kind="} + kind + "; key=" + key,
                                             std::string{"顺序键"} + kind
                                                 + "（I-REQ-7；前驱名引用悬空/重复/"
                                                   "成环详见 kind）",
                                             action));
                    r7Blocking = true;
                }
            };
            emitKeys(pointsSeq.duplicateKeys,
                     "duplicate", "消除同一前驱的多重后继声明");
            emitKeys(pointsSeq.danglingKeys,
                     "dangling", "修正前驱名拼写或补建前驱条目");
            emitKeys(pointsSeq.cycleNodes,
                     "cycle", "打断顺序环（成环条目清单见 localName）");
            emitKeys(regionsSeq.duplicateKeys,
                     "region-duplicate", "消除区域顺序键重复声明");
            emitKeys(regionsSeq.danglingKeys,
                     "region-dangling", "修正区域顺序前驱名");
            emitKeys(regionsSeq.cycleNodes,
                     "region-cycle", "打断区域顺序环");
            if (r7Blocking) {
                return blockedNow();  // 短路
            }
        }
    }

    // ---- R8 工具/模型引用存在：tcpRef/工具/环境引用浅有效 ----
    {
        bool r8Blocking = false;
        bool hadRefFace = false;  // "存在工具/环境引用面"观测（NotApplicable 判据）
        // tcpRef（Tool→tool-definition；DefaultTcp 恒合法缺省——无目标）。
        const auto checkTcpRef = [&](const core::ObjectId& oid, const std::string& name,
                                     const std::optional<RequirementReference>& tcpRef) {
            if (!tcpRef.has_value()) { return; }
            hadRefFace = true;
            // 结构面（tcp 槽允许 Tool/DefaultTcp——语义单源复用）。
            if (auto e = validateRequirementReference(*tcpRef, /*forScene=*/false)) {
                appendItem(report, ReadinessCheckLayer::R8,
                           ReadinessFindingLevel::Blocking,
                           makeReadyDiag(kReqReadyRefMissing, oid, name,
                                         "field=tcpRef; kind="
                                             + std::string(requirementRefKindToken(tcpRef->kind)),
                                         "tcpRef 引用种类非法（" + e->detail + "）",
                                         "改用 Tool/DefaultTcp 引用"));
                r8Blocking = true;
                return;
            }
            if (tcpRef->kind == RequirementRefKind::Tool) {
                if (auto violation = closureRefViolation(ctx, *tcpRef->objectId,
                                                         expectedTargetToken(tcpRef->kind))) {
                    appendItem(report, ReadinessCheckLayer::R8,
                               ReadinessFindingLevel::Blocking,
                               makeReadyDiag(kReqReadyRefMissing, oid, name,
                                             "field=tcpRef; target="
                                                 + tcpRef->objectId->toCanonical(),
                                             *violation,
                                             "修正工具引用（悬空或类型失配）"));
                    r8Blocking = true;
                }
            }
        };
        for (const TaskPoint& p : ws.points.entries) {
            if (!p.enabled) { continue; }  // 未启用条目不进入就绪判定（§4.3 enabled 行）
            checkTcpRef(p.objectId, p.name, p.tcpRef);
        }
        for (const WorkRegion& r : ws.regions.entries) {
            if (!r.enabled) { continue; }  // 同上
            checkTcpRef(r.objectId, r.name, r.tcpRef);
        }
        // 工况侧：toolRefs/payload toolRef（Tool）＋environmentRefs
        // （scene-object——环境引用浅有效，§8.1 R8 行原文）。
        for (const OperatingCondition& c : ws.conditions.entries) {
            if (!c.enabled) { continue; }  // 未启用条目不进入就绪判定（§4.5 enabled 行）
            for (const RequirementReference& tool : c.toolRefs) {
                hadRefFace = true;
                // 结构守卫先行（防御——构造边界已保证 Tool 载荷自洽）。
                if (!tool.wellFormed() || !tool.objectId.has_value()) {
                    appendItem(report, ReadinessCheckLayer::R8,
                               ReadinessFindingLevel::Blocking,
                               makeReadyDiag(kReqReadyRefMissing, c.objectId, c.name,
                                             "field=toolRefs",
                                             "工具引用结构非法（Tool 须携目标 "
                                                 "ObjectId 与非空 tcpKey）",
                                             "修正工况工具引用"));
                    r8Blocking = true;
                    continue;
                }
                if (auto violation = closureRefViolation(ctx, *tool.objectId,
                                                         expectedTargetToken(tool.kind))) {
                    appendItem(report, ReadinessCheckLayer::R8,
                               ReadinessFindingLevel::Blocking,
                               makeReadyDiag(kReqReadyRefMissing, c.objectId, c.name,
                                             "field=toolRefs; target="
                                                 + tool.objectId->toCanonical(),
                                             *violation,
                                             "修正工况工具引用"));
                    r8Blocking = true;
                }
            }
            for (const ConditionPayload& payload : c.payloads) {
                if (payload.toolRef.kind != RequirementRefKind::Tool) {
                    continue;  // 缺省（World）载荷挂载＝无工具面
                }
                hadRefFace = true;
                if (!payload.toolRef.wellFormed()
                    || !payload.toolRef.objectId.has_value()) {
                    appendItem(report, ReadinessCheckLayer::R8,
                               ReadinessFindingLevel::Blocking,
                               makeReadyDiag(kReqReadyRefMissing, c.objectId, c.name,
                                             "field=payloads.toolRef",
                                             "负载挂载工具引用结构非法",
                                             "修正负载的挂载工具引用"));
                    r8Blocking = true;
                    continue;
                }
                if (auto violation = closureRefViolation(
                        ctx, *payload.toolRef.objectId,
                        expectedTargetToken(payload.toolRef.kind))) {
                    appendItem(report, ReadinessCheckLayer::R8,
                               ReadinessFindingLevel::Blocking,
                               makeReadyDiag(kReqReadyRefMissing, c.objectId, c.name,
                                             "field=payloads.toolRef; target="
                                                 + payload.toolRef.objectId
                                                       ->toCanonical(),
                                             *violation,
                                             "修正负载挂载工具引用"));
                    r8Blocking = true;
                }
            }
            for (const core::ObjectId& env : c.environmentRefs) {
                hadRefFace = true;
                if (auto violation = closureRefViolation(ctx, env, "scene-object")) {
                    appendItem(report, ReadinessCheckLayer::R8,
                               ReadinessFindingLevel::Blocking,
                               makeReadyDiag(kReqReadyRefMissing, c.objectId, c.name,
                                             "field=environmentRefs; target="
                                                 + env.toCanonical(),
                                             *violation,
                                             "修正环境障碍引用（悬空或非场景对象）"));
                    r8Blocking = true;
                }
            }
        }
        if (!hadRefFace) {
            // 显式标记：无工具/环境引用面（全部条目用缺省工具且无环境
            // 障碍——检查域缺席）。
            appendItem(report, ReadinessCheckLayer::R8,
                       ReadinessFindingLevel::NotApplicable,
                       makeReadyDiag(kReqReadyRefMissing, std::nullopt, std::nullopt,
                                     "toolOrEnvRefs=0",
                                     "本工作集无工具/环境引用——工具模型引用面"
                                     "不适用（显式标记，非违例）",
                                     "无需处理"));
        }
        if (r8Blocking) {
            return blockedNow();  // 短路
        }
    }

    // ---- R9 可生成 evidence 输入切片：schema 受支持＋canonical 可编码
    //      ＋必验解析确定（§8.1 R9 行）----
    {
        bool r9Blocking = false;
        // 五对象 schemaVersion 单点核对（NFR-DEP-04：主版本不识别→稳定
        // 拒绝；REQ-SCHEMA-UNSUPPORTED＝§9.6 T02/T03 行已注册码）。
        const auto checkSchema = [&](std::string_view objectType,
                                     std::uint32_t actual, std::uint32_t supported) {
            if (actual != supported) {
                appendItem(report, ReadinessCheckLayer::R9,
                           ReadinessFindingLevel::Blocking,
                           makeReadyDiag(kReqSchemaUnsupported, std::nullopt,
                                         std::nullopt,
                                         std::string{"object-type="}
                                             + std::string{objectType}
                                             + "; schema-version="
                                             + std::to_string(actual)
                                             + "; supported-major="
                                             + std::to_string(supported),
                                         "对象 schema 主版本超出本程序支持"
                                         "（未来版本/历史版本不可读）",
                                         "升级程序或重新编辑该需求对象"));
                r9Blocking = true;
            }
        };
        checkSchema(kReqSetObjectType, ws.root.schemaVersion, kReqSetSchemaVersion);
        checkSchema(kReqPointSetObjectType, ws.points.schemaVersion, kReqPointSetSchemaVersion);
        checkSchema(kReqRegionSetObjectType, ws.regions.schemaVersion, kReqRegionSetSchemaVersion);
        checkSchema(kReqConditionSetObjectType, ws.conditions.schemaVersion,
                    kReqConditionSetSchemaVersion);
        checkSchema(kReqPlanSetObjectType, ws.plans.schemaVersion, kReqPlanSetSchemaVersion);
        if (r9Blocking) {
            return blockedNow();  // 短路（无后层）
        }
        // canonical 可编码＋必验解析确定＝防御面：经 R3/R6 值校验与集合
        // 不变量前置后不可达——到达即实现缺陷 fail-fast（modeling 基线
        // 解码失败同轨；不产诊断，不静默吞）。
        const RequirementCodec codec;
        const auto encodeOrThrow = [&](auto&& object, std::string_view what) {
            const auto encoded = codec.encode(
                RequirementObjectVariant(std::forward<decltype(object)>(object)),
                kCurrentRequirementFormatVersion);
            if (!encoded.ok()) {
                throw std::logic_error(
                    "requirements/readiness: R9 防御面失守——canonical 编码失败（"
                    + std::string{what} + "）: " + encoded.error().detail);
            }
        };
        encodeOrThrow(ws.root, "req-set");
        encodeOrThrow(ws.points, "req-point-set");
        encodeOrThrow(ws.regions, "req-region-set");
        encodeOrThrow(ws.conditions, "req-condition-set");
        encodeOrThrow(ws.plans, "req-plan-set");
        // 必验解析确定性防御（requiredCount 与字面 enabled∧Must 计数一致
        // ——P-EV-9 单点实现的就绪侧重核对）。
        const OperatingConditionService conditionService;
        const RequiredCaseResolution resolution =
            conditionService.resolveRequiredCases(ws.conditions.entries);
        std::size_t literalCount = 0;
        for (const OperatingCondition& c : ws.conditions.entries) {
            if (c.enabled && c.level == RequirementLevel::Must) { ++literalCount; }
        }
        if (resolution.requiredCount != literalCount) {
            throw std::logic_error(
                "requirements/readiness: R9 防御面失守——必验解析不确定（"
                "requiredCount=" + std::to_string(resolution.requiredCount)
                + "，字面计数=" + std::to_string(literalCount) + "）");
        }
    }

    // ---- 全层通过：Must 门禁清单照常投影（空 Blocking→空清单）----
    report.invalidMustItems = collectInvalidMustItems(report, enabledMustIds);
    return report;
}

// =====================================================================
// readinessSummary——evidence ①级输入门禁投影（§9.5 行原文签名）
// =====================================================================

evidence::ReadinessSummary RequirementReadinessChecker::readinessSummary(
    const RequirementReadinessReport& report) const
{
    // 两字段逐字段投影（evidence §6.4① 数据形状——PA-1 对端值类型直接
    // 消费，不再包装）：
    //   valid＝无 Blocking 发现（保守门禁——Should/集合级非法同样使输入
    //   不可消费；REQ-06 的 Must 句是 primary 面，映射分工登记于
    //   Readiness.hpp §readinessSummary 注）；
    //   invalidMustItems＝check 已按 enabled∧Must 过滤的全量清单（本函数
    //   零重算——NFR-MNT-04：判定在 check 单点完成，投影只搬值）。
    evidence::ReadinessSummary summary;
    summary.valid = !report.hasBlocking();
    summary.invalidMustItems = report.invalidMustItems;
    return summary;
}

}  // namespace sdurws::ird::requirements
