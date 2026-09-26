/**
 * @file   Evidence.cpp
 * @brief  批量任务点验证的证据组装器实现——批量计算结果→evidence
 *         EvaluationOutput 的唯一组装点（§8.2 表五行产出逐步对号）＋
 *         批量 canonical 载荷编码。
 *
 * 设计依据：
 *   - units/kinematics.md §7.1（三态素材/NotRun/完整性自检——素材完备、
 *     判定归 evidence）、§8.2（证据生成表：逐项证据/搜索未果记录/证明
 *     素材/DomainVerdictInputs/payload——"只产不判"）、§8.4（证据序＝
 *     工作项全序）、§9.2（IKinematicEvidenceBuilder 契约原文）、§5.6
 *     （结果绑定六要素）、§5.5（比较型/评价级诊断的码面纪律）
 *   - evidence 冻结契约：EvaluationItem 状态五值 presence 纪律（Evidence.hpp
 *     §6.2——Satisfied 必带摘要、Invalid 必带原因、NotApplicable 必带
 *     原因）、SearchExhaustedRecord（§6.3 末——DataInsufficient 素材）、
 *     DeterministicInfeasibilityProof（§6.3——producer 绑定三查）、
 *     DomainVerdictInputs（Verdict.hpp §6.4.1 ⑤级——违例语义归域）、
 *     DomainPayload（Envelope.hpp §7.1——digest 由组装方计算）
 *   - 任务契约 tasks/foundation/WP-15-T05.json acceptance 2/3
 *
 * 组装口径登记（随卡 §14.6 v0.5——实现落值的卡面锚点）：
 *   1. 逐项证据行 artifactDigest＝**批量载荷整体摘要**（逐项明细在载荷
 *      表内，证据行以 digest＋subject＝pointOid＋caseScope={conditionId}
 *      溯源其条目——EvidenceItem 无数值字段，值面只能在载荷）。
 *   2. 搜索未果聚合＝全部 NoConvergence/AllFiltered 项的预算/初值数求和、
 *      逐解过滤记录按工作项全序拼接（同项内保持求解器输出序）。
 *   3. 证明素材 producer 重绑＝批量评估键＋kTaskPointsBatchContractVersion
 *      （对外交付的产生者＝本评估器——validateProof 第③查的注册查证
 *      对象；素材内容与 snapshotId/sliceId 绑定保持求解器原值）。多项
 *      BoundExceeded 时取全序首个（EvaluationOutput.proof 单槽——其余
 *      项的素材在载荷逐项表与 verdictInputs 中完整存活）。
 *   4. verdictInputs＝仅 BoundExceeded 项产生违例条目（itemId＝点对象
 *      规范文本；Must→mustViolations、Should→shouldViolations）——搜索
 *      未果（C5/C8）不产生违例（DataInsufficient 口径，不得升级为不可
 *      行）；满足/不适用不产生条目（DomainVerdictInputs 只承载违例）。
 *   5. 批量通道诊断面收敛＝KIN-RESULT-INCOMPLETE（批量级一条，incomplete
 *      时）＋KIN-POINT-REF-DANGLING（逐 InputInvalid 项）两码；搜索未果
 *      素材经记录通道（output.searchRecord）交付、不出逐项诊断（避免
 *      千点批量诊断洪泛——素材不丢，呈现归 evidence/报告层）。
 *
 * 确定性（NFR-COR-01/02）：组装为纯函数——不读时钟/环境；证据行序＝
 * 工作项全序；载荷编码定宽小端＋字段定序（同输入同字节）；诊断文本为
 * 稳定字面量拼接（对象身份经 toCanonical 规范文本，无 locale 依赖）。
 */

#include <sdurws/ird/kinematics/Evidence.hpp>

#include <sdurws/ird/core/DiagData.hpp>   // DiagnosticRecord::make（C-3 校验工厂）
#include <sdurws/ird/core/Digest.hpp>     // ContentDigester（SHA-256 唯一算法面——CR-02）
#include <sdurws/ird/kinematics/DiagCodes.hpp>  // kKin* 码值常量（唯一书写点——禁拼码）

#include "CanonicalCodec.hpp"  // 私有编码原语（src/ 内共享——R-2 合规）

#include <cstring>
#include <stdexcept>
#include <utility>
#include <vector>

namespace sdurws::ird::kinematics {

namespace {

// =====================================================================
// 内部小工具（确定性组装的辅助面）
// =====================================================================

/// 批量级基础诊断组装（码值经 DiagCodes.hpp 注册常量——禁字符串拼码；
/// subject 仅在语义对象存在时携带——不伪造；C-3 校验前置字段全非空）。
core::DiagnosticRecord makeBatchDiag(std::string_view code,
                                     std::optional<core::ObjectId> subject,
                                     const std::string& context,
                                     const std::string& cause,
                                     const char* recommendedAction)
{
    return core::DiagnosticRecord::make(std::string(code), std::move(subject),
                                        std::nullopt,   // localName 不伪造
                                        std::nullopt,   // runtimeName——⑥端口消费随名称面任务
                                        context, cause, recommendedAction);
}

/// 对字节向量计算 SHA-256 内容身份（CR-02——摘要唯一经 core::ContentDigester）。
core::ContentIdentity digestOf(const std::vector<std::uint8_t>& bytes)
{
    core::ContentDigester digester;
    digester.update(bytes.data(), bytes.size());
    core::ContentIdentity id;
    id.bytes = digester.finalize();
    return id;
}

/// 逐解过滤记录→evidence FilteredSolution 的映射（三值直映——KinTypes.hpp
/// SolutionFilterReason 注的三值对应关系；组装器唯一实现点）。
evidence::FilteredSolution toEvidenceFiltered(const FilteredSolutionRecord& r)
{
    evidence::FilteredSolution fs;
    fs.solutionRef = r.signature;  // 编码安全下限：非空＋无 NUL（十六进制串）
    switch (r.reason) {
    case SolutionFilterReason::ResidualRecheck:
        fs.filterReason = evidence::SearchFilterReason::Residual;
        break;
    case SolutionFilterReason::JointLimit:
        fs.filterReason = evidence::SearchFilterReason::JointLimit;
        break;
    case SolutionFilterReason::Collision:
        fs.filterReason = evidence::SearchFilterReason::Collision;
        break;
    }
    return fs;
}

/// 批量计算结果的不变式校验（BatchComputation 注三条——组装前自检的
/// 组装器侧复核；违例＝内部实现缺陷，logic_error 不静默——AGENTS §3
/// 错误语义表"内部违约轨"）。
void validateComputationInvariants(const BatchComputation& c)
{
    // 不变式 1：工作项按 (pointOid, conditionId) 字典序严格递增（全序、
    // 无重复——§8.4；展开期去重的复核面。ObjectId 只定义 <——严格递增
    // 等价于"后项不小于前项且两键不同时相等"）。
    for (std::size_t i = 1; i < c.items.size(); ++i) {
        const auto& prev = c.items[i - 1];
        const auto& cur = c.items[i];
        const bool strictlyGreater =
            prev.pointOid < cur.pointOid
            || (prev.pointOid == cur.pointOid && prev.conditionId < cur.conditionId);
        if (!strictlyGreater) {
            throw std::logic_error(
                "kin.task-points-batch：批量结果工作项违反 (pointOid,conditionId) "
                "字典序全序/唯一性（§8.4——内部组装缺陷，fail-fast）");
        }
    }
    // 不变式 2：计数守恒——工作项总数＝Σ批项数（已处理）＋NotRun 数
    // （acceptance 2 完整性自检公式；评估器侧已自检，此处复核）。
    if (c.totalWorkItems != c.processedItemCount + c.notRunItemCount
        || c.totalWorkItems != c.items.size()) {
        throw std::logic_error(
            "kin.task-points-batch：批量结果计数守恒自检失败（工作项总数＝"
            "Σ批项数＋NotRun 数——acceptance 2/§7.1 完整性校验；内部缺陷）");
    }
    // 不变式 3：incomplete 标记与 NotRun 计数一致（无伪完成——EVI-02）。
    if (c.incomplete != (c.notRunItemCount > 0)) {
        throw std::logic_error(
            "kin.task-points-batch：incomplete 标记与 NotRun 计数不一致"
            "（无伪完成纪律——内部缺陷）");
    }
    // 不变式 4：完成矩阵素材与 caseSubset 逐一对应（同序同集——素材对账
    // 分母；evidence 覆盖矩阵的消费前提）。
    if (c.caseCompletion.size() != c.caseSubset.size()) {
        throw std::logic_error(
            "kin.task-points-batch：完成矩阵素材条数与 caseSubset 不一致"
            "（acceptance 2——caseSubset 每项有终态标记；内部缺陷）");
    }
    for (std::size_t i = 0; i < c.caseSubset.size(); ++i) {
        if (c.caseCompletion[i].conditionId != c.caseSubset[i]) {
            throw std::logic_error(
                "kin.task-points-batch：完成矩阵素材与 caseSubset 失配 @ "
                + std::to_string(i) + "（素材对账分母——内部缺陷）");
        }
    }
    // 不变式 5：逐项状态与产物 presence 一致（BatchWorkItemRecord 注——
    // ERR-01 不伪造：终态必有因、产物态必有物）。
    for (const BatchWorkItemRecord& item : c.items) {
        switch (item.status) {
        case BatchItemStatus::CandidateFound:
            if (!item.bestSolution.has_value()) {
                throw std::logic_error(
                    "kin.task-points-batch：CandidateFound 项缺最佳解（记录"
                    "不变式——内部缺陷）");
            }
            break;
        case BatchItemStatus::NoConvergence:
        case BatchItemStatus::AllFiltered:
            if (!item.searchRecord.has_value()) {
                throw std::logic_error(
                    "kin.task-points-batch：搜索未果项缺记录（§5.4 结局 2/3 "
                    "必附——内部缺陷）");
            }
            break;
        case BatchItemStatus::BoundExceeded:
            if (!item.proofMaterial.has_value()) {
                throw std::logic_error(
                    "kin.task-points-batch：解析界限项缺证明素材（§5.4 结局 5 "
                    "必附——内部缺陷）");
            }
            break;
        case BatchItemStatus::InputInvalid:
        case BatchItemStatus::NotApplicable:
        case BatchItemStatus::NotRun:
            if (item.reason.empty()) {
                throw std::logic_error(
                    "kin.task-points-batch：显式标记项缺原因文本（ERR-01 不"
                    "伪造——内部缺陷）");
            }
            break;
        }
    }
}

/// 搜索未果证据行的行内容编码（行 digest 的对象——聚合记录的定宽规范
/// 串：budget/guesses u64×2＋逐解 {solutionRef len+bytes＋reason u8}）。
std::vector<std::uint8_t> encodeSearchRowCanonical(
    const evidence::SearchExhaustedRecord& record)
{
    std::vector<std::uint8_t> out;
    detail::putU64(out, record.searchBudgetUsed);
    detail::putU64(out, record.initialGuessesTried);
    detail::putU32(out, static_cast<std::uint32_t>(record.filteredSolutions.size()));
    for (const evidence::FilteredSolution& fs : record.filteredSolutions) {
        const auto len = static_cast<std::uint32_t>(fs.solutionRef.size());
        detail::putU32(out, len);
        detail::putBytes(out, fs.solutionRef.data(), len);
        out.push_back(static_cast<std::uint8_t>(fs.filterReason));
    }
    return out;
}

/// 碰撞证据行的行内容编码实现（布局与条目序见 Evidence.hpp 公开声明注
/// ——acceptance 4 的三元组明细载体；编码器公开面供测试/下游核对）。
std::vector<std::uint8_t> encodeCollisionRowCanonicalImpl(const BatchWorkItemRecord& item)
{
    std::vector<std::uint8_t> out;

    // 先收集条目（计数前置需要——条目序＝来源序，确定性）。
    struct CollisionEntry {
        std::string configurationRef;      ///< 构型引用（I-KIN-3 签名）
        const core::ObjectId* objectA;     ///< 对象对 A 端（规范序 A<B——policy 侧）
        const core::ObjectId* objectB;     ///< 对象对 B 端
        std::uint8_t verdict;              ///< 1=碰撞／0=评价为无碰撞
    };
    std::vector<CollisionEntry> entries;

    // 来源 1：最佳解（评价在场才有条目——KIN-05 不伪造已检）。
    if (item.status == BatchItemStatus::CandidateFound && item.bestSolution.has_value()
        && item.bestSolution->collisionStatus.evaluated) {
        const KinematicSolution& best = *item.bestSolution;
        if (best.collisionStatus.objectIdPairs.size() % 2U == 0U
            && !best.collisionStatus.objectIdPairs.empty()) {
            // 有对象对（理论上最佳解恒无碰撞、对集为空——防御分支：以
            // 成对展开逐对入条目，不静默丢弃明细）。
            for (std::size_t i = 0; i + 1 < best.collisionStatus.objectIdPairs.size();
                 i += 2) {
                entries.push_back(CollisionEntry{best.signature,
                                                 &best.collisionStatus.objectIdPairs[i],
                                                 &best.collisionStatus.objectIdPairs[i + 1],
                                                 best.collisionStatus.inCollision ? 1U : 0U});
            }
        } else {
            // 无对象对＝评价为无碰撞（二元判定的零明细形态——verdict=0
            // 单条目，绑定构型引用）。
            entries.push_back(CollisionEntry{best.signature, nullptr, nullptr,
                                             best.collisionStatus.inCollision ? 1U : 0U});
        }
    }

    // 来源 2：碰撞原因过滤记录（全序逐条——"为什么少了解"的碰撞明细，
    // verdict 恒 1——被过滤即判碰撞）。
    for (const FilteredSolutionRecord& r : item.filteredRecords) {
        if (r.reason != SolutionFilterReason::Collision) {
            continue;
        }
        if (r.objectIdPairs.size() >= 2U) {
            for (std::size_t i = 0; i + 1 < r.objectIdPairs.size(); i += 2) {
                entries.push_back(CollisionEntry{r.signature, &r.objectIdPairs[i],
                                                 &r.objectIdPairs[i + 1], 1U});
            }
        } else {
            entries.push_back(CollisionEntry{r.signature, nullptr, nullptr, 1U});
        }
    }

    detail::putU32(out, static_cast<std::uint32_t>(entries.size()));
    for (const CollisionEntry& e : entries) {
        const auto len =
            static_cast<std::uint32_t>(e.configurationRef.size());
        detail::putU32(out, len);
        detail::putBytes(out, e.configurationRef.data(), len);
        // 对象对（ObjectId×ObjectId——R-4：裸 16B 身份字节，无名称）。
        // 空端（零明细形态）以全零保留值占位——保留值语义与 core Id128
        // 一致（不指称任何对象；verdict 才是条目的判定载荷）。
        static const core::ObjectId kReserved{};
        const core::ObjectId* a = e.objectA != nullptr ? e.objectA : &kReserved;
        const core::ObjectId* b = e.objectB != nullptr ? e.objectB : &kReserved;
        detail::putBytes(out, a->bytes.data(), a->bytes.size());
        detail::putBytes(out, b->bytes.data(), b->bytes.size());
        out.push_back(e.verdict);
    }
    return out;
}

/// 碰撞评价在场的判据（行发射条件——Satisfied 臂）：最佳解评价在场，
/// 或存在 Collision 原因过滤记录（被过滤即评价已发生）。
bool hasCollisionMaterial(const BatchWorkItemRecord& item)
{
    if (item.status == BatchItemStatus::CandidateFound && item.bestSolution.has_value()
        && item.bestSolution->collisionStatus.evaluated) {
        return true;
    }
    for (const FilteredSolutionRecord& r : item.filteredRecords) {
        if (r.reason == SolutionFilterReason::Collision) {
            return true;
        }
    }
    return false;
}

/// 碰撞要求在场而评价未完成的判据（行发射条件——Missing 臂；同时是
/// KIN-COLLISION-UNAVAILABLE 聚合诊断的计数判据）：CollisionFree 要求
/// 检查在场（要求声明即产出）且 collisionNotEvaluated（两臂合并标记
/// ——T07 口径：会话不在场或评价未完成，登记随卡 §14.6 v0.7）。
bool collisionRequiredButMissing(const BatchWorkItemRecord& item)
{
    for (const BatchDemandCheck& check : item.demandChecks) {
        if (check.kind == BatchDemandCheck::Kind::CollisionFree
            && item.collisionNotEvaluated) {
            return true;
        }
    }
    return false;
}

/// 逐项证据行状态映射（组装口径 1——四计算终态＝产物存在（Satisfied）；
/// NotApplicable 显式标记；InputInvalid＝Invalid＋悬空引用诊断；NotRun＝
/// 无产物（Missing——漏验素材交 evidence 覆盖矩阵②级门禁））。
evidence::EvidenceItem makeItemEvidenceRow(const BatchWorkItemRecord& item,
                                           const core::ContentIdentity& payloadDigest)
{
    evidence::EvidenceItem row;
    row.itemId = kKinBatchItemOutcomeRowId;
    row.caseScope = std::vector<evidence::CaseId>{item.conditionId};
    row.subject = item.pointOid;
    switch (item.status) {
    case BatchItemStatus::CandidateFound:
    case BatchItemStatus::NoConvergence:
    case BatchItemStatus::AllFiltered:
    case BatchItemStatus::BoundExceeded:
        row.status = evidence::EvidenceItemStatus::Satisfied;
        row.artifactDigest = payloadDigest.bytes;  // 组装口径 1——载荷整体摘要
        break;
    case BatchItemStatus::NotApplicable:
        row.status = evidence::EvidenceItemStatus::NotApplicable;
        row.notApplicableReason = item.reason;  // C2——不计缺失，原因必填
        break;
    case BatchItemStatus::InputInvalid:
        row.status = evidence::EvidenceItemStatus::Invalid;
        row.invalidReason = makeBatchDiag(
            kKinPointRefDangling, item.pointOid,
            "kin.task-points-batch 批量展开（工况 "
                + item.conditionId.toCanonical() + "）",
            item.reason,
            "修正工况适用范围/事件的任务点引用后重提批量评估");
        break;
    case BatchItemStatus::NotRun:
        row.status = evidence::EvidenceItemStatus::Missing;  // 无产物——漏验素材
        break;
    }
    return row;
}

}  // namespace

// =====================================================================
// 碰撞证据行内容编码（公开面——Evidence.hpp 声明；WP-15-T07）
// =====================================================================

std::vector<std::uint8_t>
encodeCollisionVerdictRowCanonical(const BatchWorkItemRecord& item)
{
    // 委托匿名命名空间实现（布局与条目序单点——公开面仅为可测试与
    // 下游核对，无第二编码路径，NFR-MNT-04）。
    return encodeCollisionRowCanonicalImpl(item);
}

// =====================================================================
// KinematicEvidenceBuilder——组装主流程（§8.2 表五行逐步对号）
// =====================================================================

evidence::EvaluationOutput KinematicEvidenceBuilder::build(
    const BatchComputation& computation, const EvidenceContext& ctx) const
{
    // 调用方契约违约 fail-fast：上下文缺请求引用（绑定事实无从取得）。
    if (ctx.request == nullptr) {
        throw std::invalid_argument(
            "kin.task-points-batch：证据组装上下文缺评估请求指针"
            "（EvidenceContext 契约违约——fail-fast）");
    }
    // 内部不变量复核（组装前自检的组装器侧——validateComputationInvariants
    // 注；违例 logic_error 不静默）。
    validateComputationInvariants(computation);

    evidence::EvaluationOutput out;

    // ---- 行 5：payload（canonical、五元组绑定 §5.6）——先编码：逐项
    // 证据行的 artifactDigest 消费其摘要（组装口径 1）＋SHA-256（CR-02
    // 唯一算法面）。----
    const std::vector<std::uint8_t> payloadBytes =
        encodeBatchPayloadCanonical(computation, ctx.request->task);
    const core::ContentIdentity payloadDigest = digestOf(payloadBytes);
    out.payload = evidence::DomainPayload{kTaskPointsBatchPayloadToken, payloadBytes,
                                          payloadDigest};

    // ---- 行 1：逐项证据（每任务点×工况一条——证据序＝工作项全序，
    // §8.4；状态映射见 makeItemEvidenceRow 注）。----
    out.evidence.reserve(computation.items.size() + 1U);
    for (const BatchWorkItemRecord& item : computation.items) {
        out.evidence.push_back(makeItemEvidenceRow(item, payloadDigest));
    }

    // ---- 碰撞证据行（WP-15-T07 表尾追加——acceptance 4 的明细交付面；
    // 发射三态见 kKinBatchCollisionRowId 注与 makeCollisionEvidenceRow
    // 判据：在场→Satisfied＋行内容摘要；要求在场而缺失→Missing 素材；
    // 不在范围→不出行）。聚合计数供诊断面（KIN-COLLISION-UNAVAILABLE）。----
    std::uint64_t collisionMissingItems = 0;
    for (const BatchWorkItemRecord& item : computation.items) {
        if (collisionRequiredButMissing(item)) {
            ++collisionMissingItems;
            evidence::EvidenceItem row;
            row.itemId = kKinBatchCollisionRowId;
            // 证据缺失素材（Missing——evidence 判 DataInsufficient 的碰撞
            // 臂；绝不伪造 Satisfied——KIN-05 不视为无碰撞）。
            row.status = evidence::EvidenceItemStatus::Missing;
            row.caseScope = std::vector<evidence::CaseId>{item.conditionId};
            row.subject = item.pointOid;
            out.evidence.push_back(std::move(row));
            continue;
        }
        if (hasCollisionMaterial(item)) {
            evidence::EvidenceItem row;
            row.itemId = kKinBatchCollisionRowId;
            row.status = evidence::EvidenceItemStatus::Satisfied;
            // 行内容＝三元组明细规范字节（{subjectPair, configurationRef,
            // verdict} 逐条——encodeCollisionVerdictRowCanonical），digest
            // 绑定其存在与内容（溯源面与逐项/搜索行同构）。
            row.artifactDigest =
                digestOf(encodeCollisionVerdictRowCanonical(item)).bytes;
            row.caseScope = std::vector<evidence::CaseId>{item.conditionId};
            row.subject = item.pointOid;
            out.evidence.push_back(std::move(row));
        }
        // 其余（无要求且未评价）＝碰撞检查不在范围——不出行（V13-01
        // 查询面口径，非降级非缺失）。
    }

    // ---- 行 2：搜索未果记录聚合（组装口径 2——预算/初值数求和、逐解
    // 过滤记录按全序拼接；C5/C8 素材→evidence 判 DataInsufficient，不得
    // 输出不可行）。----
    bool hasSearchContent = false;
    evidence::SearchExhaustedRecord aggregated;
    for (const BatchWorkItemRecord& item : computation.items) {
        if (item.status == BatchItemStatus::NoConvergence
            || item.status == BatchItemStatus::AllFiltered) {
            hasSearchContent = true;
            aggregated.searchBudgetUsed += item.searchRecord->searchBudgetUsed;
            aggregated.initialGuessesTried += item.searchRecord->initialGuessesTried;
            for (const FilteredSolutionRecord& r : item.filteredRecords) {
                aggregated.filteredSolutions.push_back(toEvidenceFiltered(r));
            }
        }
    }
    if (hasSearchContent) {
        out.searchRecord = std::move(aggregated);
        // 搜索未果汇总证据行（Satisfied——记录已产；digest 对行内容编码，
        // 溯源面与逐项行同构）。
        evidence::EvidenceItem searchRow;
        searchRow.itemId = kKinBatchSearchRowId;
        searchRow.status = evidence::EvidenceItemStatus::Satisfied;
        searchRow.artifactDigest =
            digestOf(encodeSearchRowCanonical(*out.searchRecord)).bytes;
        searchRow.caseScope = computation.caseSubset;  // 聚合行覆盖本批全部工况
        out.evidence.push_back(std::move(searchRow));
    }

    // ---- 行 3：证明素材（组装口径 3——全序首个 BoundExceeded 项；producer
    // 重绑为批量评估键。铁律：仅素材不裁定——成立与否归 evidence
    // validateProof 五查，D-09）。----
    for (const BatchWorkItemRecord& item : computation.items) {
        if (item.status == BatchItemStatus::BoundExceeded) {
            evidence::DeterministicInfeasibilityProof proof = item.proofMaterial->proof;
            proof.producer = kTaskPointsBatchEvaluationKey;
            proof.producerContractVersion = kTaskPointsBatchContractVersion;
            out.proof = std::move(proof);
            break;  // 单槽承载——全序首个；其余项素材经载荷/verdictInputs 存活
        }
    }

    // ---- 行 4：DomainVerdictInputs（组装口径 4——REQ-06 口径填报启用
    // Must/Should 条目计算结局：仅解析界限证明素材产生违例条目；判定归
    // evidence ⑤级）。----
    for (const BatchWorkItemRecord& item : computation.items) {
        if (item.status != BatchItemStatus::BoundExceeded) {
            continue;  // 搜索未果不产生违例（C5/C8）；满足/不适用无条目
        }
        evidence::DomainVerdictViolation violation;
        violation.itemId = item.pointOid.toCanonical();  // 域登记条目指称——点规范文本
        violation.detail =
            "任务点在工况 " + item.conditionId.toCanonical()
            + " 下目标位置超出解析工作半径上界（结局 5 证明素材——仅素材，"
              "判定归 evidence aggregateVerdict）";
        if (item.pointLevel == BatchRequirementLevel::Must) {
            out.verdictInputs.mustViolations.push_back(std::move(violation));
        } else {
            out.verdictInputs.shouldViolations.push_back(std::move(violation));
        }
    }

    // ---- 诊断（组装口径 5——批量不完整→KIN-RESULT-INCOMPLETE 一条，
    // NotRun 清单摘要进 cause；InputInvalid 逐项的 KIN-POINT-REF-DANGLING
    // 已随逐项证据行的 invalidReason 产出，此处不再重复收集）。----
    if (computation.incomplete) {
        out.diagnostics.push_back(makeBatchDiag(
            kKinResultIncomplete, std::nullopt,
            "kin.task-points-batch 批量评估完整性自检",
            "批量评估不完整：工作项总数 " + std::to_string(computation.totalWorkItems)
                + " 中 " + std::to_string(computation.notRunItemCount)
                + " 项 NotRun（分批取消/失败——未完成批如实标记，无伪完成；"
                  "已完成批 " + std::to_string(computation.completedBatchCount)
                + "/" + std::to_string(computation.batchCount) + "）",
            "按检查点 watermark 续跑（新 attempt，已完成批不重算）或重提"
            "批量评估后归档"));
    }

    // 碰撞证据缺失诊断（KIN-COLLISION-UNAVAILABLE——§9.6 行 9，产码面随
    // WP-15-T07 落位）：聚合"要求在场＋未评价"项计数一条（两臂同码——
    // 缺检测器/策略未启用臂＋设施异常臂，口径登记随卡 §14.6 v0.7）；
    // 样本级素材＝Missing 碰撞行＋demandCheck evaluated=false。
    if (collisionMissingItems > 0) {
        out.diagnostics.push_back(makeBatchDiag(
            kKinCollisionUnavailable, std::nullopt,
            "kin.task-points-batch 批量评估碰撞证据",
            "碰撞要求在场的工作项中 " + std::to_string(collisionMissingItems)
                + " 项碰撞评价未完成（缺检测器/策略未启用/设施异常——证据缺失"
                  "素材，绝不视为无碰撞；KIN-05）",
            "接入碰撞检测器、启用策略碰撞域或修正设施后按同一冻结输入复评"));
    }
    return out;
}

// =====================================================================
// encodeBatchPayloadCanonical（布局见 Evidence.hpp 头注；字段序即写序）
// =====================================================================

std::vector<std::uint8_t> encodeBatchPayloadCanonical(const BatchComputation& computation,
                                                      const core::TaskIdentity& task)
{
    std::vector<std::uint8_t> out;
    out.reserve(256 + computation.items.size() * 64U);

    // magic "IRDBP01"（7 字节 ASCII）＋codec 版本 u32=1（布局演进即推进）。
    const char magic[] = {'I', 'R', 'D', 'B', 'P', '0', '1'};
    out.insert(out.end(), magic, magic + sizeof(magic));
    detail::putU32(out, 1U);

    // 完整性标记（incomplete——调用侧据此不产 Completed envelope，§7.1）。
    out.push_back(computation.incomplete ? 1U : 0U);

    // 绑定块（§5.6 前半：snapshotId/sliceId/configDigest/mode/seed/
    // referenceQ——D-KIN-4 referenceQ 显式入身份）。
    detail::putBytes(out, computation.snapshotId.bytes.data(),
                     computation.snapshotId.bytes.size());
    detail::putBytes(out, computation.sliceId.bytes.data(),
                     computation.sliceId.bytes.size());
    detail::putBytes(out, computation.configDigest.bytes.data(),
                     computation.configDigest.bytes.size());
    out.push_back(static_cast<std::uint8_t>(computation.mode));
    detail::putU64(out, computation.seed);
    detail::putU32(out, static_cast<std::uint32_t>(computation.referenceQ.size()));
    for (const double v : computation.referenceQ) {
        detail::putF64(out, v);
    }

    // 任务五元组（4×16B 强类型 id＋attempt u64——§5.6 绑定面后半）。
    detail::putBytes(out, task.project.bytes.data(), task.project.bytes.size());
    detail::putBytes(out, task.branch.bytes.data(), task.branch.bytes.size());
    detail::putBytes(out, task.revision.bytes.data(), task.revision.bytes.size());
    detail::putBytes(out, task.run.bytes.data(), task.run.bytes.size());
    detail::putU64(out, task.attempt.value);  // attempt＝u64 序号（非 Id128）

    // evaluationKey＋契约版本（§5.6 绑定六要素——长度前置的 ASCII）。
    const auto keyLen =
        static_cast<std::uint32_t>(std::strlen(kTaskPointsBatchEvaluationKey));
    detail::putU32(out, keyLen);
    detail::putBytes(out, kTaskPointsBatchEvaluationKey, keyLen);
    detail::putU32(out, kTaskPointsBatchContractVersion);

    // caseSubset（去重后字典序——完成矩阵素材的对账分母）。
    detail::putU32(out, static_cast<std::uint32_t>(computation.caseSubset.size()));
    for (const core::ObjectId& caseId : computation.caseSubset) {
        detail::putBytes(out, caseId.bytes.data(), caseId.bytes.size());
    }

    // 批量统计五计数＋标记（acceptance 2 自检公式的三个元都在内）。
    detail::putU64(out, computation.totalWorkItems);
    detail::putU64(out, computation.processedItemCount);
    detail::putU64(out, computation.notRunItemCount);
    detail::putU64(out, computation.batchCount);
    detail::putU64(out, computation.completedBatchCount);

    // 完成矩阵素材（caseSubset 同序——三标记打包 u8 低三位）。
    detail::putU32(out,
                   static_cast<std::uint32_t>(computation.caseCompletion.size()));
    for (const BatchCaseCompletion& cc : computation.caseCompletion) {
        detail::putBytes(out, cc.conditionId.bytes.data(), cc.conditionId.bytes.size());
        const std::uint8_t flags = (cc.executed ? 1U : 0U)
            | (cc.notRun ? 2U : 0U) | (cc.notApplicable ? 4U : 0U);
        out.push_back(flags);
        detail::putU64(out, cc.computedItemCount);
        detail::putU64(out, cc.notRunItemCount);
    }

    // 工作项表（全序——§8.4；字节序即排序结果）。
    detail::putU32(out, static_cast<std::uint32_t>(computation.items.size()));
    for (const BatchWorkItemRecord& item : computation.items) {
        detail::putBytes(out, item.pointOid.bytes.data(), item.pointOid.bytes.size());
        detail::putBytes(out, item.conditionId.bytes.data(),
                         item.conditionId.bytes.size());
        out.push_back(static_cast<std::uint8_t>(item.pointLevel));
        out.push_back(static_cast<std::uint8_t>(item.status));
        out.push_back(item.outcomeKind.has_value() ? 1U : 0U);
        if (item.outcomeKind.has_value()) {
            out.push_back(static_cast<std::uint8_t>(*item.outcomeKind));
        }
        out.push_back(item.collisionNotEvaluated ? 1U : 0U);

        // 要求值对比（实际/要求/单位——§8.2 行 1 内容列的逐项承载）。
        detail::putU32(out,
                       static_cast<std::uint32_t>(item.demandChecks.size()));
        for (const BatchDemandCheck& check : item.demandChecks) {
            out.push_back(static_cast<std::uint8_t>(check.kind));
            out.push_back(check.evaluated ? 1U : 0U);
            out.push_back(check.satisfied ? 1U : 0U);
            detail::putF64(out, check.actual);
            detail::putF64(out, check.required);
            const auto unitLen = static_cast<std::uint32_t>(check.unit.size());
            detail::putU32(out, unitLen);
            detail::putBytes(out, check.unit.data(), unitLen);
        }

        // 最佳解（仅 CandidateFound 非空——稳定排序首位；字段序与 T04
        // 解集编码同构，便于下游共用解码器）。
        out.push_back(item.bestSolution.has_value() ? 1U : 0U);
        if (item.bestSolution.has_value()) {
            const KinematicSolution& s = *item.bestSolution;
            detail::putU32(out, static_cast<std::uint32_t>(s.q.size()));
            for (const double v : s.q) {
                detail::putF64(out, v);
            }
            detail::putF64(out, s.positionResidual);
            detail::putF64(out, s.orientationResidual);
            detail::putU32(out, static_cast<std::uint32_t>(s.jointMargins.size()));
            for (const double v : s.jointMargins) {
                detail::putF64(out, v);
            }
            detail::putF64(out, s.minimumJointMargin);
            detail::putF64(out, s.manipulability);
            detail::putF64(out, s.conditionNumber);
            out.push_back(s.collisionStatus.evaluated ? 1U : 0U);
            out.push_back(s.collisionStatus.inCollision ? 1U : 0U);
            detail::putU32(out, static_cast<std::uint32_t>(
                                    s.collisionStatus.objectIdPairs.size()));
            for (const core::ObjectId& oid : s.collisionStatus.objectIdPairs) {
                detail::putBytes(out, oid.bytes.data(), oid.bytes.size());
            }
            detail::putU32(out, s.sourceInitIndex);
            detail::putU32(out, s.iterations);
            const auto sigLen = static_cast<std::uint32_t>(s.signature.size());
            detail::putU32(out, sigLen);
            detail::putBytes(out, s.signature.data(), sigLen);
            detail::putU32(out, s.solverContractVersion);
        }

        // 硬过滤记录（诊断价值面——签名＋原因即可溯源；完整指标在求解器
        // 侧解集，批量载荷只承载解释"为什么少了解"的最小面）。
        detail::putU32(out,
                       static_cast<std::uint32_t>(item.filteredRecords.size()));
        for (const FilteredSolutionRecord& r : item.filteredRecords) {
            const auto sigLen = static_cast<std::uint32_t>(r.signature.size());
            detail::putU32(out, sigLen);
            detail::putBytes(out, r.signature.data(), sigLen);
            out.push_back(static_cast<std::uint8_t>(r.reason));
        }

        // 搜索未果记录（结局 2/3 有值——present u8）。
        out.push_back(item.searchRecord.has_value() ? 1U : 0U);
        if (item.searchRecord.has_value()) {
            const IkSearchRecord& sr = *item.searchRecord;
            detail::putU64(out, sr.searchBudgetUsed);
            detail::putU64(out, sr.initialGuessesTried);
            detail::putU32(
                out, static_cast<std::uint32_t>(sr.iterationsPerInit.size()));
            for (const std::uint32_t it : sr.iterationsPerInit) {
                detail::putU32(out, it);
            }
        }

        // 证明素材 present 标记（素材本体经 output.proof 交付——不入
        // opaque 载荷；逐项表只登记"该工作项存在结局 5 素材"）。
        out.push_back(item.proofMaterial.has_value() ? 1U : 0U);

        // 原因文本（NotApplicable/InputInvalid/NotRun 必填——ERR-01）。
        const auto reasonLen = static_cast<std::uint32_t>(item.reason.size());
        detail::putU32(out, reasonLen);
        detail::putBytes(out, item.reason.data(), reasonLen);
    }
    return out;
}

}  // namespace sdurws::ird::kinematics
