/**
 * @file   Render.cpp
 * @brief  失败点/薄弱区三维渲染数据（KIN-07）的组装实现——两通道组装点、
 *         统一状态词投影映射与回写/选中联动数据面（契约见 Render.hpp）。
 *
 * 设计依据（节选，全量见 Render.hpp 头注）：
 *   - units/kinematics.md §9.8（KIN-07 数据本单元产、呈现归 ui）、§6.2
 *     （worstBy＝最差项规范来源——本 TU 全部"最差"选择只经该设施）、
 *     §7.1（per-item 状态语义——NotApplicable/NotRun 不渲染为失败）、
 *     §4.5（回写零修订/零失效/不入缓存身份）
 *   - policy.md §4.4/P-POL-2（阈值槽位显式不适用——不发明数值）、D-08
 *     （边界含于合规侧——严格比较）
 *
 * 实现纪律：纯函数、零共享可变状态；全部遍历序固定（确定性 NFR-COR-01/
 * 02）；调用方契约违约 fail-fast（std::invalid_argument）、内部不变式破
 * 坏 std::logic_error（不静默）——§9.1 错误语义两分。
 */

#include "sdurws/ird/kinematics/Render.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace sdurws::ird::kinematics {
namespace {

// =====================================================================
// 内部辅助：非有限校验与共享评估步（两通道唯一实现——防组装口径漂移）
// =====================================================================

/**
 * @brief 校验解级标量指标非 NaN（NFR-COR-03 非有限拒绝的实现点）。
 *
 * +∞ 合法（连续关节裕量/奇异条件数的既定承载值——KinTypes.hpp 字段注）；
 * NaN 是求解器契约排除值，出现即调用方数据违约。
 *
 * @param value [in] 待校验值（无量纲或随语义）
 * @param what  [in] 指标名（异常消息定位用）
 *
 * @throws std::invalid_argument value 为 NaN
 */
void requireNotNan(double value, const char* what)
{
    // std::isnan 覆盖且仅覆盖 NaN——+∞/−∞/有限值全部放行。
    if (std::isnan(value)) {
        throw std::invalid_argument(std::string{"渲染数据组装：解指标 "} + what
                                    + " 为 NaN（求解器契约排除值——NFR-COR-03）");
    }
}

/**
 * @brief 对单个解评估薄弱区（近限位逐关节＋近奇异的**唯一实现**——两
 *        通道共享，防第二套阈值比较式，NFR-MNT-04 精神）。
 *
 * 判定语义（D-08 边界含于合规侧——严格比较，WeakZoneRenderItem 结构注）：
 *   - 近限位：margin[i] < nearLimitRatio（+∞ 裕量恒不命中）；
 *   - 近奇异：conditionNumber > conditionNumberWarning（+∞ 恒命中）。
 *
 * 产出序：近限位条目按链序升序在前，近奇异条目（至多一条）在后——
 * 与 Face A/Face B 的整体序约定一致（各函数注）。
 *
 * @param[in] solution         待评估解
 * @param[in] pointOid         任务点绑定（直通条目字段）
 * @param[in] conditionId      工况绑定（直通条目字段）
 * @param[in] chainJointObjects 链序关节对象（维度已由调用方校验）
 * @param[in] thresholds       策略阈值投影（nullopt 种类整体跳过——P-POL-2）
 * @param[in] solutionIndex    解集通道定位（批量通道传 nullopt）
 * @param[in] workItemIndex    批量通道定位（解集通道传 nullopt）
 * @param[out] zones           条目追加目标（调用方持有）
 * @param[out] nearLimitApplicable   近限位阈值在场标记（或上原值——与逻辑）
 * @param[out] nearSingularApplicable 近奇异阈值在场标记（同上）
 *
 * @throws std::invalid_argument 解指标含 NaN（requireNotNan）
 */
void evaluateWeakZonesForSolution(const KinematicSolution& solution,
                                  const core::ObjectId& pointOid,
                                  const core::ObjectId& conditionId,
                                  const std::vector<core::ObjectId>& chainJointObjects,
                                  const WeakZoneThresholds& thresholds,
                                  std::optional<std::size_t> solutionIndex,
                                  std::optional<std::uint64_t> workItemIndex,
                                  std::vector<WeakZoneRenderItem>& zones,
                                  bool& nearLimitApplicable, bool& nearSingularApplicable)
{
    // ---- 近限位：逐关节（链序升序）严格小于判定 ----
    // 阈值在场才评估（P-POL-2：nullopt＝检查显式不适用，不伪造"无薄弱"）。
    if (thresholds.nearLimitRatio.has_value()) {
        nearLimitApplicable = true;
        const double threshold = *thresholds.nearLimitRatio;
        // 逐关节扫描——链序即产出序；+∞ 裕量（连续关节）恒不命中，由
        // 比较语义自然排除，无需特判分支。
        for (std::size_t i = 0; i < solution.jointMargins.size(); ++i) {
            const double margin = solution.jointMargins[i];
            requireNotNan(margin, "jointMargins（归一化裕量）");
            // 严格小于（§7.4/policy 同则）：r＝阈值不警告——边界含于
            // 合规侧（D-08）；+∞ < threshold 恒 false——连续关节自然跳过。
            if (margin < threshold) {
                WeakZoneRenderItem item;
                item.kind = WeakZoneKind::NearJointLimit;
                item.pointOid = pointOid;
                item.conditionId = conditionId;
                item.jointObject = chainJointObjects[i];  // R-4：关节标注一律 ObjectId
                item.jointIndex = i;
                item.value = margin;        // 归一化裕量（无量纲）
                item.threshold = threshold; // 策略阈值（同轴可比）
                item.solutionIndex = solutionIndex;
                item.workItemIndex = workItemIndex;
                zones.push_back(std::move(item));
            }
        }
    }

    // ---- 近奇异：解级条件数严格大于判定（模型级事实——无单关节归属） ----
    if (thresholds.conditionNumberWarning.has_value()) {
        nearSingularApplicable = true;
        const double threshold = *thresholds.conditionNumberWarning;
        requireNotNan(solution.conditionNumber, "conditionNumber（条件数）");
        // 严格大于（D-08 同则）：条件数＝阈值不警告；+∞（奇异）恒命中。
        if (solution.conditionNumber > threshold) {
            WeakZoneRenderItem item;
            item.kind = WeakZoneKind::NearSingular;
            item.pointOid = pointOid;
            item.conditionId = conditionId;
            item.jointObject = core::ObjectId{};  // 全零保留值＝"未绑定"（模型级事实）
            item.jointIndex = 0;                  // 保留值语义——消费方以 kind 判读
            item.value = solution.conditionNumber;
            item.threshold = threshold;
            item.solutionIndex = solutionIndex;
            item.workItemIndex = workItemIndex;
            zones.push_back(std::move(item));
        }
    }
}

/**
 * @brief 把一个解的碰撞对象对拆为渲染条目（成对展平 [a1,b1,…] 的唯一
 *        拆对点——两通道共享）。
 *
 * @param[in] solution        碰撞评价在场的解（evaluated∧inCollision 由
 *                            调用方先判——本函数只管拆对）
 * @param[in] pointOid        任务点绑定
 * @param[in] conditionId     工况绑定
 * @param[in] fromFilteredRecord 来源标记（false＝解集内碰撞解）
 * @param[in] solutionIndex   解集序定位（无则 nullopt）
 * @param[in] filteredRecordIndex 过滤记录定位（无则 nullopt）
 * @param[in] workItemIndex   批量工作项定位（无则 nullopt）
 * @param[out] pairs          条目追加目标
 *
 * @throws std::invalid_argument objectIdPairs 为奇数长度（成对展平契约
 *         违约——CollisionStatus 结构注的既定不变式，破坏即数据违约）
 */
void appendCollisionPairsOfSolution(const KinematicSolution& solution,
                                    const core::ObjectId& pointOid,
                                    const core::ObjectId& conditionId,
                                    bool fromFilteredRecord,
                                    std::optional<std::size_t> solutionIndex,
                                    std::optional<std::size_t> filteredRecordIndex,
                                    std::optional<std::uint64_t> workItemIndex,
                                    std::vector<CollisionPairRenderItem>& pairs)
{
    // 奇数长度＝展平契约破坏（[a1,b1,a2,b2,…]）——fail-fast 不静默截断。
    if (solution.collisionStatus.objectIdPairs.size() % 2u != 0u) {
        throw std::invalid_argument(
            "渲染数据组装：collisionStatus.objectIdPairs 为奇数长度（成对展平契约违约）");
    }
    // 逐对拆分——保持 policy 会话规范序（A<B），本单元零改写（R-4）。
    for (std::size_t i = 0; i + 1 < solution.collisionStatus.objectIdPairs.size(); i += 2) {
        CollisionPairRenderItem item;
        item.objectA = solution.collisionStatus.objectIdPairs[i];
        item.objectB = solution.collisionStatus.objectIdPairs[i + 1];
        item.configurationSignature = solution.signature;
        item.pointOid = pointOid;
        item.conditionId = conditionId;
        item.fromFilteredRecord = fromFilteredRecord;
        item.solutionIndex = solutionIndex;
        item.filteredRecordIndex = filteredRecordIndex;
        item.workItemIndex = workItemIndex;
        pairs.push_back(std::move(item));
    }
}

/**
 * @brief 把一条硬过滤碰撞记录的对象对拆为渲染条目（成对展平的拆对点
 *        ——两通道共享，防第二套拆对代码）。
 *
 * @param[in] record            碰撞原因的过滤记录（reason 由调用方先判）
 * @param[in] pointOid          任务点绑定
 * @param[in] conditionId       工况绑定
 * @param[in] filteredRecordIndex 记录在其 filteredRecords 序中的下标
 * @param[in] workItemIndex     批量工作项定位（解集通道传 nullopt）
 * @param[out] pairs            条目追加目标
 *
 * @throws std::invalid_argument objectIdPairs 为奇数长度（成对展平契约
 *         违约——CollisionStatus 结构注的既定不变式，破坏即数据违约）
 */
void appendFilteredCollisionPairs(const FilteredSolutionRecord& record,
                                  const core::ObjectId& pointOid,
                                  const core::ObjectId& conditionId,
                                  std::size_t filteredRecordIndex,
                                  std::optional<std::uint64_t> workItemIndex,
                                  std::vector<CollisionPairRenderItem>& pairs)
{
    // 奇数长度＝展平契约破坏（[a1,b1,a2,b2,…]）——fail-fast 不静默截断。
    if (record.objectIdPairs.size() % 2u != 0u) {
        throw std::invalid_argument(
            "渲染数据组装：filteredRecords 对象对为奇数长度（成对展平契约违约）");
    }
    // 逐对拆分——保持 policy 会话规范序（A<B），本单元零改写（R-4）。
    for (std::size_t p = 0; p + 1 < record.objectIdPairs.size(); p += 2) {
        CollisionPairRenderItem item;
        item.objectA = record.objectIdPairs[p];
        item.objectB = record.objectIdPairs[p + 1];
        item.configurationSignature = record.signature;
        item.pointOid = pointOid;
        item.conditionId = conditionId;
        item.fromFilteredRecord = true;           // 诊断解标记（呈现侧区分）
        item.solutionIndex = std::nullopt;        // 过滤记录不在解集序内
        item.filteredRecordIndex = filteredRecordIndex;
        item.workItemIndex = workItemIndex;
        pairs.push_back(std::move(item));
    }
}

/**
 * @brief 解的碰撞评价是否产出对象对（evaluated∧inCollision 的唯一判定
 *        式——evaluated=false 是证据缺失，绝不当作碰撞也不当作无碰撞，
 *        KIN-05）。
 */
bool hasCollisionEvidence(const KinematicSolution& solution) noexcept
{
    // 双条件缺一不可：inCollision 仅在 evaluated=true 时有意义
    // （CollisionStatus 契约原文）；未评价＝证据缺失——不产出对（渲染面
    // 不伪造"有碰撞"，也不伪造成"无碰撞"）。
    return solution.collisionStatus.evaluated && solution.collisionStatus.inCollision;
}

/**
 * @brief 归一化裕量序列的 arg-min 关节下标（链序首个最小值——平局确定
 *        性）。全 +∞ 时返回 nullopt（arg-min 无定义——调用方落保留值）。
 */
std::optional<std::size_t> argMinJointIndex(const std::vector<double>& jointMargins) noexcept
{
    std::optional<std::size_t> best;
    // 线性扫描取严格更小者——平局保留先者（链序确定性，无容差全序比较）。
    for (std::size_t i = 0; i < jointMargins.size(); ++i) {
        requireNotNan(jointMargins[i], "jointMargins（归一化裕量）");
        if (!std::isfinite(jointMargins[i])) {
            continue;  // +∞（连续/无界关节）不参与 arg-min——结构注口径
        }
        if (!best.has_value() || jointMargins[i] < jointMargins[*best]) {
            best = i;
        }
    }
    return best;
}

/**
 * @brief 在 resolvedPoints 中按 pointOid 查找目标位姿（首个命中——调用
 *        方保证 pointOid 唯一；查无＝悬空点，如实返回 nullopt）。
 */
std::optional<rw::math::Transform3D<double>>
lookupTargetInBase(const std::vector<BatchTaskPoint>& resolvedPoints,
                   const core::ObjectId& pointOid) noexcept
{
    for (const BatchTaskPoint& point : resolvedPoints) {
        if (point.pointOid == pointOid) {
            return point.targetInBase;  // 宿主解析投影原值——{B} 系，零改写
        }
    }
    return std::nullopt;  // 悬空点/未解析——不虚构坐标（ARC-04）
}

}  // namespace

// =====================================================================
// 渲染状态投影（acceptance 2）
// =====================================================================

std::optional<core::TaskOutcome> projectBatchItemOutcome(BatchItemStatus status) noexcept
{
    switch (status) {
    case BatchItemStatus::CandidateFound:
        // 计算完成且得到候选——结果轴 Completed（可行性素材在册）。
        return core::TaskOutcome::Completed;
    case BatchItemStatus::NoConvergence:
    case BatchItemStatus::AllFiltered:
    case BatchItemStatus::BoundExceeded:
        // 计算完成但未得可用解（搜索未果/界限素材）——渲染为失败点，
        // 结果轴 Failed（失败点的定义面，acceptance 1）。
        return core::TaskOutcome::Failed;
    case BatchItemStatus::InputInvalid:
        // 输入非法素材（悬空引用等）——数据错误的如实失败呈现。
        return core::TaskOutcome::Failed;
    case BatchItemStatus::NotApplicable:
    case BatchItemStatus::NotRun:
        // 显式不适用/未运行——**不是失败**（acceptance 3：如实区分，
        // 伪造即 ERR-01 违例）。nullopt 是调用方的排除信号。
        return std::nullopt;
    }
    // 枚举穷尽（七值全覆盖）——不可达路径保持防御性返回。
    return std::nullopt;
}

// =====================================================================
// 策略阈值投影（acceptance 1——阈值读 policy 的唯一读取点）
// =====================================================================

WeakZoneThresholds weakZoneThresholdsOf(const policy::JointThresholds& jointThresholds) noexcept
{
    WeakZoneThresholds out;
    // optional 槽位原样透传（siValue 为策略域校验后的 SI 真值——零二次
    // 校验零换算）；nullopt＝显式不适用（P-POL-2），由下游 applicable
    // 标记与空集承载，本函数不发明默认值。
    if (jointThresholds.nearLimitRatio.has_value()) {
        out.nearLimitRatio = jointThresholds.nearLimitRatio->siValue();
    }
    if (jointThresholds.conditionNumberWarning.has_value()) {
        out.conditionNumberWarning = jointThresholds.conditionNumberWarning->siValue();
    }
    return out;
}

// =====================================================================
// 解集通道组装（Face A）
// =====================================================================

SolutionSetRenderData
assembleSolutionSetRenderData(const IKinematicSolutionSet& solutionSet,
                              const core::ObjectId& pointOid, const core::ObjectId& conditionId,
                              const std::vector<core::ObjectId>& chainJointObjects,
                              const WeakZoneThresholds& thresholds)
{
    SolutionSetRenderData out;
    const SolutionSetView& sorted = solutionSet.sorted();

    // ---- 第 1 步：薄弱区（sorted() 解序；每解先近限位后近奇异） ----
    for (std::size_t si = 0; si < sorted.size(); ++si) {
        const KinematicSolution& solution = sorted[si];
        // 维度校验只在有解时执行（空集+错维度列表＝不触发——调用方契约
        // 的核查点在消费处，语义同"用到才查"）。
        if (chainJointObjects.size() != solution.jointMargins.size()) {
            throw std::invalid_argument(
                "渲染数据组装：chainJointObjects 维度与解的 jointMargins 不符（调用方契约违约）");
        }
        evaluateWeakZonesForSolution(solution, pointOid, conditionId, chainJointObjects,
                                     thresholds, si, std::nullopt, out.weakZones.zones,
                                     out.weakZones.nearLimitApplicable,
                                     out.weakZones.nearSingularApplicable);
    }

    // ---- 第 2 步：最差关节裕量（worstBy 唯一来源——T09 设施复用） ----
    // §6.2"最差排序的规范来源：裕量最小"——本 TU 不写任何第二套比较。
    const std::optional<SolutionRef> worstRef =
        solutionSet.worstBy(WorstMetric::MinimumJointMargin);
    if (worstRef.has_value()) {
        const KinematicSolution& worst = sorted[worstRef->solutionIndex];
        requireNotNan(worst.minimumJointMargin, "minimumJointMargin（最差裕量）");
        WorstJointMarginRenderItem item;
        item.pointOid = pointOid;
        item.minimumJointMargin = worst.minimumJointMargin;
        item.configurationSignature = worst.signature;
        item.solutionIndex = worstRef->solutionIndex;
        if (std::isfinite(worst.minimumJointMargin)) {
            // 有界情形：arg-min 关节必存在（最小值有限 ⇒ 至少一个有限裕量
            // ——求解器不变式 minimumJointMargin＝min(jointMargins)）。
            const std::optional<std::size_t> argMin = argMinJointIndex(worst.jointMargins);
            if (!argMin.has_value()) {
                // 防御面：不变式破坏（有限最小值却无有限裕量）＝数据违约。
                throw std::invalid_argument(
                    "渲染数据组装：minimumJointMargin 有限但 jointMargins 全无界（求解器不变式违约）");
            }
            item.jointObject = chainJointObjects[*argMin];
            item.jointIndex = *argMin;
        }
        // 全无界（+∞）情形：arg-min 无定义——保留值如实承载（结构注）。
        out.worstJointMargin = std::move(item);
    }
    // 空解集：worstRef 为 nullopt → worstJointMargin 保持 nullopt（无解
    // 不虚构）。

    // ---- 第 3 步：碰撞对象对（先解内、后过滤记录——确定性序） ----
    for (std::size_t si = 0; si < sorted.size(); ++si) {
        const KinematicSolution& solution = sorted[si];
        // 解集内碰撞解：KIN-05 双条件判定（evaluated=false＝证据缺失，
        // 不产出对）。求解器直接产出的解集上此支恒空（碰撞解在求解期
        // 移入过滤记录——§6.1 生产端不变式），本循环保留以覆盖"解内
        // 携带碰撞标记"的语义演进形态（T09 collisionDiagnosticPredicate
        // 同款场景），消费同一判定式（零第二套比较）。
        if (hasCollisionEvidence(solution)) {
            appendCollisionPairsOfSolution(solution, pointOid, conditionId, false, si,
                                           std::nullopt, std::nullopt, out.collisionPairs);
        }
    }
    // 过滤碰撞记录：诊断解（reason==Collision——阶段③命中）的对象对随
    // T07 记录交付，呈现为"碰撞诊断"（fromFilteredRecord=true）。视图的
    // filteredRecords() 访问器（T11 表尾追加）为明细来源——非碰撞原因
    // （残差/限位）的记录无对象对，跳过。
    const std::vector<FilteredSolutionRecord>& filtered = solutionSet.filteredRecords();
    for (std::size_t fi = 0; fi < filtered.size(); ++fi) {
        const FilteredSolutionRecord& record = filtered[fi];
        if (record.reason != SolutionFilterReason::Collision) {
            continue;
        }
        appendFilteredCollisionPairs(record, pointOid, conditionId, fi, std::nullopt,
                                     out.collisionPairs);
    }
    return out;
}

// =====================================================================
// 批量通道组装（Face B）
// =====================================================================

BatchRenderData
assembleBatchRenderData(const BatchComputation& computation,
                        const std::vector<BatchTaskPoint>& resolvedPoints,
                        const std::vector<core::ObjectId>& chainJointObjects,
                        const WeakZoneThresholds& thresholds, core::TaskState sourceTaskState)
{
    BatchRenderData out;
    out.failurePoints.points.reserve(computation.items.size());

    // ---- 第 1 步：失败点分类（T05 per-item 状态消费——acceptance 3） ----
    for (std::size_t i = 0; i < computation.items.size(); ++i) {
        const BatchWorkItemRecord& item = computation.items[i];
        const std::optional<core::TaskOutcome> outcome = projectBatchItemOutcome(item.status);
        if (!outcome.has_value()) {
            // NotApplicable/NotRun：排除出失败点集并计数（如实区分——
            // acceptance 3；哪个计数由状态本身区分，不合并）。
            if (item.status == BatchItemStatus::NotApplicable) {
                ++out.failurePoints.notApplicableItemCount;
            } else {
                ++out.failurePoints.notRunItemCount;
            }
            continue;
        }
        if (item.status == BatchItemStatus::CandidateFound) {
            // 有解项：非失败，计入对账分母（守恒不变式的一支）。
            ++out.failurePoints.candidateFoundCount;
            continue;
        }
        // 四失败态：入失败点集（投影恒 Failed——projectBatchItemOutcome
        // 的映射面；taskState 随来源运行通道，ui 经 L-K12 供给）。
        FailurePointRenderItem point;
        point.pointOid = item.pointOid;
        point.conditionId = item.conditionId;
        point.failureKind = item.status;
        point.workItemIndex = i;
        point.state.taskState = sourceTaskState;
        point.state.taskOutcome = *outcome;
        point.reason = item.reason;  // 原样透传（T05 不变式：仅预终结态非空）
        point.targetInBase = lookupTargetInBase(resolvedPoints, item.pointOid);
        out.failurePoints.points.push_back(std::move(point));
    }

    // 守恒不变式自检（FailurePointsRenderData 结构注）：每工作项恰归
    // 一类——破坏即内部缺陷（std::logic_error，不静默；T05 组装器同则）。
    const std::uint64_t classified = static_cast<std::uint64_t>(out.failurePoints.points.size())
        + out.failurePoints.candidateFoundCount + out.failurePoints.notApplicableItemCount
        + out.failurePoints.notRunItemCount;
    if (classified != computation.items.size()) {
        throw std::logic_error("渲染数据组装：失败点分类守恒不变式破坏（内部缺陷）");
    }
    if (computation.totalWorkItems != computation.items.size()) {
        // T05 不变式 totalWorkItems == items.size()（BatchComputation 注）
        // ——消费侧复核，防上游半成品记录。
        throw std::logic_error("渲染数据组装：totalWorkItems 与 items 数不符（T05 不变式违约）");
    }

    // ---- 第 2 步：薄弱区＋碰撞对（只评估有解项的最佳解） ----
    for (std::size_t i = 0; i < computation.items.size(); ++i) {
        const BatchWorkItemRecord& item = computation.items[i];
        // 无解项（失败/未运行/不适用）无解级评估对象——跳过（薄弱区/
        // 碰撞对是解级事实，失败点已在其集合承载）。
        if (item.status != BatchItemStatus::CandidateFound || !item.bestSolution.has_value()) {
            continue;
        }
        const KinematicSolution& best = *item.bestSolution;
        if (chainJointObjects.size() != best.jointMargins.size()) {
            throw std::invalid_argument(
                "渲染数据组装：chainJointObjects 维度与最佳解 jointMargins 不符（调用方契约违约）");
        }
        // 逐项最佳解评估——solutionIndex 为 nullopt（批量通道无解集序），
        // workItemIndex 承载解级定位。
        evaluateWeakZonesForSolution(best, item.pointOid, item.conditionId, chainJointObjects,
                                     thresholds, std::nullopt, i, out.weakZones.zones,
                                     out.weakZones.nearLimitApplicable,
                                     out.weakZones.nearSingularApplicable);
        // 最佳解碰撞对（解内形态——solutionIndex/filteredRecordIndex 均
        // nullopt，workItemIndex 定位）。
        if (hasCollisionEvidence(best)) {
            appendCollisionPairsOfSolution(best, item.pointOid, item.conditionId, false,
                                           std::nullopt, std::nullopt, i,
                                           out.collisionPairs);
        }
        // 逐项过滤碰撞记录（诊断形态——与 Face A 共享同一拆对辅助，
        // 零第二套拆对代码）。
        for (std::size_t fi = 0; fi < item.filteredRecords.size(); ++fi) {
            const FilteredSolutionRecord& record = item.filteredRecords[fi];
            if (record.reason != SolutionFilterReason::Collision) {
                continue;  // 非碰撞原因（残差/限位）不产对象对——其记录无对
            }
            appendFilteredCollisionPairs(record, item.pointOid, item.conditionId, fi, i,
                                         out.collisionPairs);
        }
    }
    return out;
}

// =====================================================================
// 可视化点会话回写（acceptance 3——KIN-06）
// =====================================================================

RenderPointWriteback writebackOf(const KinematicSolution& solution)
{
    RenderPointWriteback out;
    out.q = solution.q;  // 按值副本——容器持自己的拷贝（KinSessionPose 同则）
    out.configurationSignature = solution.signature;
    return out;
}

RenderPointWriteback writebackOf(const FilteredSolutionRecord& record)
{
    RenderPointWriteback out;
    out.q = record.q;
    out.configurationSignature = record.signature;
    return out;
}

void writebackToSessionPose(KinSessionPose& session, const RenderPointWriteback& writeback)
{
    // 唯一写点：直调 T08 会话容器写入口——零修订/零失效/不入缓存身份的
    // 结构性保证随 KinSessionPose（其类注"零端口/零身份耦合"）与本值
    // 类型（RenderPointWriteback 结构注）双重承载；签名不写会话（记录
    // 键非会话语义）。非有限 q 的拒绝随写入口既定契约（invalid_argument）。
    session.setJointConfiguration(writeback.q);
}

// =====================================================================
// 选中联动数据（acceptance 4——L-K1）
// =====================================================================

FailurePointSelectionLink
failurePointSelectionLink(const BatchComputation& computation, std::uint64_t workItemIndex,
                          const std::vector<BatchTaskPoint>& resolvedPoints,
                          core::TaskState sourceTaskState)
{
    // 越界＝调用方契约违约（fail-fast——§9.1 调用方错误轨）。
    if (workItemIndex >= computation.items.size()) {
        throw std::invalid_argument("选中联动：workItemIndex 越界（调用方契约违约）");
    }
    const BatchWorkItemRecord& item = computation.items[workItemIndex];
    FailurePointSelectionLink link;
    link.pointOid = item.pointOid;
    link.conditionId = item.conditionId;
    link.failureKind = item.status;  // 如实携带（任意工作项可选中——函数注）
    link.reason = item.reason;
    // 结果轴投影复用唯一映射点：失败态→{sourceTaskState, Failed}（选中
    // 即失败点呈现）；CandidateFound→{sourceTaskState, Completed}；预终
    // 结态（NotApplicable/NotRun）→nullopt（无结果轴投影——ui 中性呈现，
    // 不落失败态，acceptance 3 选中面延伸）。
    const std::optional<core::TaskOutcome> outcome = projectBatchItemOutcome(item.status);
    if (outcome.has_value()) {
        link.state = RenderStateProjection{sourceTaskState, *outcome};
    }
    link.targetInBase = lookupTargetInBase(resolvedPoints, item.pointOid);
    link.workItemIndex = workItemIndex;
    return link;
}

std::optional<std::uint64_t>
findWorkItemIndex(const BatchComputation& computation, const core::ObjectId& pointOid,
                  const core::ObjectId& conditionId) noexcept
{
    // 线性首命中——items 全序（(pointOid, conditionId) 字典序，§8.4），
    // 首命中即唯一命中（集合语义无重复项）。
    for (std::size_t i = 0; i < computation.items.size(); ++i) {
        if (computation.items[i].pointOid == pointOid
            && computation.items[i].conditionId == conditionId) {
            return static_cast<std::uint64_t>(i);
        }
    }
    return std::nullopt;  // 无命中如实返回（不猜测——ARC-04）
}

SolutionSelectionLink solutionSelectionLink(const IKinematicSolutionSet& solutionSet,
                                            const SolutionRef& ref)
{
    const SolutionSetView& sorted = solutionSet.sorted();
    // 越界＝调用方契约违约（fail-fast；不钳制不回绕）。
    if (ref.solutionIndex >= sorted.size()) {
        throw std::invalid_argument("选中联动：solutionIndex 越界（调用方契约违约）");
    }
    SolutionSelectionLink link;
    link.solutionIndex = ref.solutionIndex;
    link.writeback = writebackOf(sorted[ref.solutionIndex]);
    return link;
}

}  // namespace sdurws::ird::kinematics
