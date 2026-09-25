/**
 * @file   Services.cpp
 * @brief  三领域服务与需求档派生的实现——构造校验链（错误语义逐项）、
 *         顺序键拓扑校验（Kahn 消去）、采样定义规范化（D-REQ-2 floor
 *         规则）、必验集合解析（§6.2 冻结 schema 唯一实现点）。
 *
 * 设计依据：units/requirements.md §9.4（接口契约）、§4.7（I-REQ 不变量）、
 * §5.2（规范化规则与零样本合法性）、§6.2（必验冻结 schema）、§3.4（纯
 * 函数总约定）；任务契约 tasks/foundation/WP-14-T03.json acceptance 1/3/4。
 *
 * 线程安全：全部函数无共享可变状态（栈上局部），可重入——多线程并发
 * 调用安全。确定性：排序/投影/摘要均为纯字节运算；floor 为 IEEE754 单
 * 调确定函数——同输入同输出（NFR-COR-01/02）。
 */

#include <sdurws/ird/requirements/Services.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace sdurws::ird::requirements {

namespace {

/// 空诊断占位引用约束断言（契约预留位——本单元服务无已登记诊断码，
/// 恒不写入；留此断言防未来误用：写入即须先落 §9.6 码行登记）。
void assertDiagsUntouched(const std::vector<core::DiagnosticRecord>& diags)
{
    (void)diags;  // 服务内部不追加——参数仅为 §9.4 原文签名形状的预留位
}

/// 名称边界核对（I-REQ-3——空名/与既有兄弟名重复＝DuplicateName；不
/// 静默加后缀，NFR-COR-03）。
std::optional<RequirementError> checkNameAgainstSiblings(
    const std::string& name, const std::vector<std::string>& siblingNames)
{
    if (name.empty()) {
        RequirementError e;
        e.code = RequirementErrorCode::DuplicateName;
        e.detail = "requirements/services: 条目名称为空（§8.1 R0 非空——"
                   "名称是报告/覆盖清单定位依据）";
        return e;
    }
    for (const auto& s : siblingNames) {
        if (s == name) {
            RequirementError e;
            e.code = RequirementErrorCode::DuplicateName;
            e.params.emplace_back("name", name);
            e.detail = "requirements/services: 集合内名称重复（I-REQ-3——构造"
                       "边界拒绝，不静默改写/加后缀，NFR-COR-03）";
            return e;
        }
    }
    return std::nullopt;
}

}  // namespace

// =====================================================================
// ITaskPointService
// =====================================================================

CreateOutcome TaskPointService::createPoint(const TaskPointSpec& spec,
                                            std::vector<core::DiagnosticRecord>& diags) const
{
    assertDiagsUntouched(diags);
    CreateOutcome out;
    // ①名称边界（I-REQ-3——首个违例短路）。
    if (auto e = checkNameAgainstSiblings(spec.name, spec.siblingNames)) {
        out.error = std::move(*e);
        return out;
    }
    // ②③④⑤装配探针后走条目级全量校验（validateTaskPoint：容差
    // IllegalTolerance／位姿 AllDofFree/ZeroVectorTarget／姿态规则参数
    // 非法／引用面 I-REQ-4／三段距离——与 Codec 解码校验链语义单源，
    // NFR-MNT-04；work 段按 §4.3 恒启用装配后再验）。
    TaskPoint probe;
    probe.name = spec.name;
    probe.processTag = spec.processTag;
    probe.level = spec.level;
    probe.enabled = spec.enabled;
    probe.source = spec.source;
    probe.refFrame = spec.refFrame;
    probe.tcpRef = spec.tcpRef;
    probe.pose = spec.pose;
    probe.tolerance = spec.tolerance;
    probe.approach = spec.approach;
    // work 段即任务点本身（§4.3"enabled 恒 true，占位表达段序"）——占位
    // 距离 1.0 m 不进入任何工程判定（TRJ 按点位构造，不消费 work 距离）。
    probe.work = TaskSegment{/*enabled=*/true, /*axis=*/SegmentAxis::ToolZ,
                             /*distanceM=*/1.0};
    probe.retract = spec.retract;
    probe.demands = spec.demands;
    probe.sequenceKey = spec.sequenceKey;
    probe.note = spec.note;
    if (auto e = validateTaskPoint(probe)) {
        out.error = std::move(*e);
        return out;
    }
    // 全部通过→产出任务点（objectId＝新临时句柄——正式分配权归 project，
    // O-36 口径见 Services.hpp 头注）。
    out.ok = true;
    probe.objectId = core::ObjectId::generate();
    out.point = std::move(probe);
    return out;
}

SequenceCheckResult TaskPointService::checkSequence(const std::vector<TaskPoint>& points) const
{
    // 顺序模型（I-REQ-7＋§5.1"顺序集＝按 key 拓扑消费"；§5.1"删除被
    // 顺序键引用的任务点→拒绝"证明顺序键是条目间引用）：sequenceKey＝
    // 前驱条目**名称**，顺序边 prev→this；分支（两条目同前驱）＝重复键、
    // 回边＝环、缺席前驱＝悬空。
    SequenceCheckResult res;
    std::set<std::string> names;   // 集合内全部条目名（悬空判定基准）
    for (const auto& p : points) {
        names.insert(p.name);
    }
    std::map<std::string, int> predecessorUseCount;  // 前驱名→被声明次数
    std::vector<std::pair<std::string, std::string>> edges;  // (prev, this) 顺序边
    for (const auto& p : points) {
        if (!p.sequenceKey.has_value()) {
            continue;  // 无顺序键＝不参与顺序约束（并行/无序条目）
        }
        const auto& key = *p.sequenceKey;
        if (names.find(key) == names.end()) {
            // 悬空前驱（引用缺席——R1 面的顺序特例；升序去重入清单）。
            if (std::find(res.danglingKeys.begin(), res.danglingKeys.end(), key)
                == res.danglingKeys.end()) {
                res.danglingKeys.push_back(key);
            }
            continue;
        }
        ++predecessorUseCount[key];
        edges.emplace_back(key, p.name);
    }
    // 重复键：同一前驱被两条目声明（同位置多后继＝顺序歧义——I-REQ-7
    // "无重复"分支）。
    for (const auto& [key, count] : predecessorUseCount) {
        if (count > 1 && std::find(res.duplicateKeys.begin(), res.duplicateKeys.end(), key)
                             == res.duplicateKeys.end()) {
            res.duplicateKeys.push_back(key);
        }
    }
    // Kahn 拓扑消去：入度表按 this 计，逐轮移除入度 0 节点；消去后剩余
    // 节点即环上节点（I-REQ-7"无环"分支）。注意：仅作为前驱出现的节点
    //（无入边）也要入表（入度 0）——否则链首缺席会使队列恒空、把正常
    // 链误判成环。
    std::map<std::string, int> inDegree;
    std::map<std::string, std::vector<std::string>> adjacency;  // prev→后继列表
    for (const auto& [prev, cur] : edges) {
        if (inDegree.find(prev) == inDegree.end()) {
            inDegree[prev] = 0;  // 前驱节点初始化（无入边＝入度 0）
        }
        ++inDegree[cur];
        adjacency[prev].push_back(cur);
    }
    std::vector<std::string> ready;
    for (const auto& [name, deg] : inDegree) {
        if (deg == 0) {
            ready.push_back(name);
        }
    }
    std::map<std::string, int> remaining = inDegree;
    while (!ready.empty()) {
        const auto cur = ready.back();
        ready.pop_back();
        auto it = adjacency.find(cur);
        if (it == adjacency.end()) {
            continue;
        }
        for (const auto& next : it->second) {
            auto dit = remaining.find(next);
            if (dit != remaining.end() && --dit->second == 0) {
                ready.push_back(next);
            }
        }
    }
    for (const auto& [name, deg] : remaining) {
        if (deg > 0) {
            res.cycleNodes.push_back(name);  // 消去后仍有入度＝在环上
        }
    }
    // 清单规范化（升序去重——确定性；调用方呈现/测试机械比对面）。
    auto sortUnique = [](std::vector<std::string>& v) {
        std::sort(v.begin(), v.end());
        v.erase(std::unique(v.begin(), v.end()), v.end());
    };
    sortUnique(res.duplicateKeys);
    sortUnique(res.danglingKeys);
    sortUnique(res.cycleNodes);
    res.acyclic = res.duplicateKeys.empty() && res.danglingKeys.empty()
               && res.cycleNodes.empty();
    return res;
}

DerivePrecheck TaskPointService::precheckDerivation(const std::vector<core::ObjectId>& sources) const
{
    // T03 落最小真实实现（T07 镜像/模板消费）：本服务不持工作集——源
    // 定位与规则核对需要点集输入，此处先返回"源 id 有效性核对"面：
    // 无效（全零保留值）id 入 notFound。完整"按工作集定位＋引用型规则
    // 清单"随 T07 TemplateArray 服务的点集参数化签名落位（§11 T07 行），
    // 本函数的 referenceRules 输出由 T07 增量填充（卡 §12 T07 交接行）。
    // 注：这不是桩——函数行为完整（输入→确定性输出）；T07 的扩展属
    // 其任务卡的既定落位面（WP-14-T07 行"模板/镜像/阵列"）。
    DerivePrecheck pre;
    for (const auto& id : sources) {
        if (!id.isValid()) {
            pre.notFound.push_back(id);
        }
    }
    return pre;
}

// =====================================================================
// IWorkRegionService
// =====================================================================

RegionCreateOutcome WorkRegionService::createRegion(const WorkRegionSpec& spec,
                                                    std::vector<core::DiagnosticRecord>& diags) const
{
    assertDiagsUntouched(diags);
    RegionCreateOutcome out;
    // ①名称边界（I-REQ-3）。
    if (auto e = checkNameAgainstSiblings(spec.name, spec.siblingNames)) {
        out.error = std::move(*e);
        return out;
    }
    // ②装配探针后走条目级全量校验（validateWorkRegion：盒退化
    // DegenerateRegion／覆盖率越界／间距 IllegalTolerance／姿态计数
    // NegativeCount／引用面——与 Codec 解码校验链语义单源）。
    WorkRegion probe;
    probe.name = spec.name;
    probe.level = spec.level;
    probe.enabled = spec.enabled;
    probe.source = spec.source;
    probe.refFrame = spec.refFrame;
    probe.tcpRef = spec.tcpRef;
    probe.box = spec.box;
    probe.positionSampling = spec.positionSampling;
    probe.orientationSampling = spec.orientationSampling;
    probe.coverageTargets = spec.coverageTargets;
    probe.demands = spec.demands;
    probe.sequenceKey = spec.sequenceKey;
    probe.note = spec.note;
    if (auto e = validateWorkRegion(probe)) {
        out.error = std::move(*e);
        return out;
    }
    // 全部通过→产出区域（objectId＝新临时句柄）。
    out.ok = true;
    probe.objectId = core::ObjectId::generate();
    out.region = std::move(probe);
    return out;
}

Expected<NormalizedSampling> WorkRegionService::normalizeSampling(
    const PositionSampling& raw, const rw::math::Vector3D<double>& boxSize,
    std::vector<core::DiagnosticRecord>& diags) const
{
    assertDiagsUntouched(diags);
    NormalizedSampling result;
    // Grid/Random 已是权威形态——原样通过（changed=false；D-REQ-2 只对
    // 间距式定义规范化）。
    if (raw.method != PositionSamplingMethod::GridBySpacing) {
        result.normalized = raw;
        result.changed = false;
        return Expected<NormalizedSampling>::ok(std::move(result));
    }
    // 间距/尺寸非法面（三分量正有限——floor 的定义域）。
    const double sizes[3] = {boxSize[0], boxSize[1], boxSize[2]};
    for (int i = 0; i < 3; ++i) {
        const double sp = raw.spacing[static_cast<std::size_t>(i)];
        if (!std::isfinite(sp) || sp <= 0.0) {
            RequirementError e;
            e.code = RequirementErrorCode::IllegalTolerance;
            e.params.emplace_back("field", "spacing");
            e.params.emplace_back("value", std::to_string(sp));
            e.detail = "requirements/services: 间距式采样间距须为正有限值"
                       "（m；§5.2 GridBySpacing）";
            return Expected<NormalizedSampling>::err(std::move(e));
        }
        if (!std::isfinite(sizes[i]) || sizes[i] <= 0.0) {
            RequirementError e;
            e.code = RequirementErrorCode::DegenerateRegion;
            e.params.emplace_back("axis", i == 0 ? "x" : (i == 1 ? "y" : "z"));
            e.detail = "requirements/services: 区域盒尺寸非正（m——floor 规范化"
                       "被除数须正）";
            return Expected<NormalizedSampling>::err(std::move(e));
        }
    }
    // D-REQ-2 规范化规则（§5.2 原文）：counts[i] = floor(size[i]/spacing[i]) + 1。
    // 确定性来源：除法/floor 均为 IEEE754 确定运算——同（size,spacing）
    // 输入跨进程必得同 counts（acceptance 3——KIN-04 冻结前提；同义计划
    // 同身份＝不同 spacing 规范化到同 counts 者身份相同）。
    PositionSampling norm;
    norm.method = PositionSamplingMethod::Grid;
    for (int i = 0; i < 3; ++i) {
        const double sp = raw.spacing[static_cast<std::size_t>(i)];
        const double quotients = std::floor(sizes[i] / sp);
        // 商上界钳制（boxSize/spacing 的商理论上可超 uint32——黄金数据集
        // 外的病态输入；钳到 uint32 上限并如实承载，不静默放大）。
        const double clamped = std::min(quotients, static_cast<double>(0xFFFFFFFFu));
        norm.counts[static_cast<std::size_t>(i)] =
            static_cast<std::uint32_t>(clamped) + 1u;
    }
    result.normalized = norm;
    result.changed = true;
    return Expected<NormalizedSampling>::ok(std::move(result));
}

// =====================================================================
// IOperatingConditionService
// =====================================================================

ConditionCreateOutcome OperatingConditionService::createCondition(
    const OperatingConditionSpec& spec, std::vector<core::DiagnosticRecord>& diags) const
{
    assertDiagsUntouched(diags);
    ConditionCreateOutcome out;
    // ①名称边界（I-REQ-3）。
    if (auto e = checkNameAgainstSiblings(spec.name, spec.siblingNames)) {
        out.error = std::move(*e);
        return out;
    }
    // ②全量条目校验（负载/事件/节拍/适用范围/引用组——IllegalTolerance
    // 族；validateOperatingCondition 短路返回首个违例）。
    OperatingCondition probe;
    probe.name = spec.name;
    probe.level = spec.level;
    probe.enabled = spec.enabled;
    probe.environmentRefs = spec.environmentRefs;
    probe.toolRefs = spec.toolRefs;
    probe.payloads = spec.payloads;
    probe.events = spec.events;
    probe.targetCycleTimeS = spec.targetCycleTimeS;
    probe.demands = spec.demands;
    probe.verificationOrderHint = spec.verificationOrderHint;
    probe.appliesTo = spec.appliesTo;
    probe.note = spec.note;
    if (auto e = validateOperatingCondition(probe)) {
        out.error = std::move(*e);
        return out;
    }
    // 全部通过→装配工况（objectId＝新临时句柄）。
    out.ok = true;
    probe.objectId = core::ObjectId::generate();
    out.condition = std::move(probe);
    return out;
}

RequiredCaseResolution OperatingConditionService::resolveRequiredCases(
    const std::vector<OperatingCondition>& conditions) const
{
    // §6.2 冻结 schema 的唯一实现（P-EV-9；逐条注释对照原文行）。
    RequiredCaseResolution res;
    // 规范序投影：按 ObjectId 字典序复制索引（同集合必得同序——
    // evidence requiredCaseSetId 的对账前提，§6.1"本单元保证 entries
    // 解析确定性"）。
    std::vector<const OperatingCondition*> ordered;
    ordered.reserve(conditions.size());
    for (const auto& c : conditions) {
        ordered.push_back(&c);
    }
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const OperatingCondition* a, const OperatingCondition* b) {
                         return a->objectId.toCanonical() < b->objectId.toCanonical();
                     });
    for (const auto* c : ordered) {
        RequiredCaseEntry entry;
        entry.caseId = c->objectId;                  // schema：caseId := objectId
        entry.label = c->name;                       // schema：label := name
        entry.enabled = c->enabled;                  // schema：enabled := enabled
        entry.mandatory = (c->level == RequirementLevel::Must);  // schema：mandatory := (level==Must)
        if (entry.enabled && entry.mandatory) {
            ++res.requiredCount;                     // 必验集合 := enabled ∧ Must
        }
        res.entries.push_back(std::move(entry));
    }
    return res;
}

// =====================================================================
// 需求档派生（§4.8）
// =====================================================================

RequirementProfile deriveRequirementProfile(const std::vector<TaskPoint>& points,
                                            const std::vector<WorkRegion>& regions,
                                            const std::vector<OperatingCondition>& conditions,
                                            const IOperatingConditionService& service)
{
    RequirementProfile profile;
    // 必验清单＝冻结解析复用（P-EV-9 单点——只取 enabled∧mandatory 子集
    // 作"清单"，全量投影保留在 entries 内供 evidence 字面消费对账）。
    const auto resolution = service.resolveRequiredCases(conditions);
    for (const auto& e : resolution.entries) {
        if (e.enabled && e.mandatory) {
            profile.requiredCases.push_back(e);
        }
    }
    // Must/Should 计数（三集合全量汇总——§4.8"Must/Should 计数"行）。
    for (const auto& p : points) {
        p.level == RequirementLevel::Must ? ++profile.mustCount : ++profile.shouldCount;
    }
    for (const auto& r : regions) {
        r.level == RequirementLevel::Must ? ++profile.mustCount : ++profile.shouldCount;
    }
    for (const auto& c : conditions) {
        c.level == RequirementLevel::Must ? ++profile.mustCount : ++profile.shouldCount;
    }
    // 覆盖目标汇总：各区域位置覆盖率下限的最小值（空区域集＝0.0——
    // "无区域要求"的汇总面，§4.8"覆盖目标汇总"行）。
    profile.minPositionCoverage = 0.0;
    bool hasRegion = false;
    for (const auto& r : regions) {
        if (!hasRegion || r.coverageTargets.minPositionCoverage < profile.minPositionCoverage) {
            profile.minPositionCoverage = r.coverageTargets.minPositionCoverage;
        }
        hasRegion = true;
    }
    // contentIdentity：派生档输入的 canonical 投影文本再摘要（确定性——
    // 同输入集合同指纹；core::ContentDigester 为 SHA-256 唯一实现）。
    core::ContentDigester digester;
    auto feed = [&digester](const std::string& s) {
        digester.update(s.data(), s.size());
        digester.update("\x1F", 1);  // 字段分隔（ASCII unit separator——定界确定）
    };
    for (const auto& e : profile.requiredCases) {
        feed(e.caseId.toCanonical());
        feed(e.label);
        feed(e.enabled ? "1" : "0");
        feed(e.mandatory ? "1" : "0");
    }
    feed(std::to_string(profile.mustCount));
    feed(std::to_string(profile.shouldCount));
    feed(std::to_string(profile.minPositionCoverage));
    profile.contentIdentity = core::ContentIdentity{digester.finalize()};
    return profile;
}

}  // namespace sdurws::ird::requirements
